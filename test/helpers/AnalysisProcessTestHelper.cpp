#include "topo/Platform/AnalysisProcess.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <spawn.h>
extern "C" char** environ;
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sched.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#endif
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#endif

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string action = argv[1];
    if (action == "write" && argc == 3) {
        std::ofstream out(argv[2]);
        out << "executed\n";
        return out ? 0 : 13;
    }
    if (action == "write-report" && argc == 3) {
        {
            std::ofstream out(argv[2]);
            out << "loader image executed\n";
            if (!out) return 13;
        }
        std::ifstream in(argv[2]);
        std::string bytes;
        std::getline(in, bytes);
        std::puts(bytes.c_str());
        return in && bytes == "loader image executed" ? 0 : 14;
    }
    if (action == "read" && argc == 3) {
        std::ifstream in(argv[2]);
        if (!in) return 13;
        std::string value;
        std::getline(in, value);
        std::puts(value.c_str());
        return 0;
    }
    if (action == "environment" && argc == 3) {
        const char* value = std::getenv(argv[2]);
        if (value) std::puts(value);
        return value ? 0 : 7;
    }
    if (action == "sleep") {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        return 0;
    }
    if (action == "flood") {
        for (;;)
            std::fputs("abcdefghijklmnopqrstuvwxyz\n", stdout);
    }
    if (action == "exit") return 17;
    if (action == "thread") {
        int value = 0;
        std::thread thread([&] { value = 23; });
        thread.join();
        std::printf("%d\n", value);
        return value == 23 ? 0 : 1;
    }
#ifndef _WIN32
    if (action == "read-fd" && argc == 3) {
        char bytes[32]{};
        const auto count = ::read(std::stoi(argv[2]), bytes, sizeof(bytes));
        if (count < 0) {
            std::printf("errno=%d\n", errno);
            return 13;
        }
        std::fwrite(bytes, 1, static_cast<size_t>(count), stdout);
        return 0;
    }
    if (action == "write-fd" && argc == 3) {
        constexpr char replacement[] = "unexpected descriptor write\n";
        const auto count = ::write(std::stoi(argv[2]), replacement, sizeof(replacement) - 1);
        if (count < 0) {
            std::printf("errno=%d\n", errno);
            return 13;
        }
        return count == static_cast<ssize_t>(sizeof(replacement) - 1) ? 0 : 14;
    }
    if (action == "locked-temporary") {
        const char* temporary = std::getenv("TMPDIR");
        if (!temporary) return 3;
        const std::string locked = std::string(temporary) + "/locked";
        if (::mkdir(locked.c_str(), 0700) != 0) return 4;
        std::ofstream(locked + "/sentinel") << "owned";
        if (::chmod(locked.c_str(), 0) != 0) return 5;
        std::puts(temporary);
        return 0;
    }
    if (action == "connect" && argc == 3) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            std::printf("socket errno=%d\n", errno);
            return errno == EPERM ? 13 : 20;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(std::stoi(argv[2])));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const int status = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        const int error = errno;
        ::close(fd);
        std::printf("connect=%d errno=%d\n", status, error);
        return status == 0 ? 0 : 13;
    }
    if (action == "spawn") {
        pid_t child = -1;
        char mode[] = "exit";
        char* arguments[] = {argv[0], mode, nullptr};
        const int error = ::posix_spawn(&child, argv[0], nullptr, nullptr, arguments, environ);
        if (error) {
            std::printf("spawn errno=%d\n", error);
            return 13;
        }
        ::waitpid(child, nullptr, 0);
        return 0;
    }
    if (action == "fork") {
        const pid_t pid = ::fork();
        if (pid < 0) {
            std::printf("errno=%d\n", errno);
            return 13;
        }
        if (pid == 0) _exit(0);
        ::waitpid(pid, nullptr, 0);
        return 0;
    }
    if (action == "setsid") {
        // A process group leader cannot call setsid even without a sandbox.
        // First join the live parent's group, so the unisolated control proves success.
        if (::setpgid(0, ::getpgid(::getppid())) != 0) return 13;
        const auto status = ::setsid();
        return status >= 0 ? 0 : 13;
    }
    if (action == "setpgid") return ::setpgid(0, ::getpgid(::getppid())) == 0 ? 0 : 13;
    if (action == "exec" && argc == 4) {
        ::execl(argv[2], argv[2], "write", argv[3], static_cast<char*>(nullptr));
        std::printf("exec errno=%d\n", errno);
        return 13;
    }
#ifdef __APPLE__
    if (action == "sysctl-parent" && argc == 3) {
        // The parent receives its sentinel at exec, not setenv: Darwin reports
        // the initial argv/environment image through KERN_PROCARGS2.
        namespace plat = topo::platform;
        plat::ProcessCaptureLimits limits;
        limits.environment = std::map<std::string, std::string>{};
        const std::vector<std::string> childArgs{"sysctl-procargs", std::to_string(::getpid())};
        plat::BoundedProcessResult result;
        if (std::string(argv[2]) == "isolated") {
            plat::AnalysisProcessPolicy policy;
            policy.allowedExecutables.emplace_back(argv[0]);
            auto isolated = plat::runAnalysisProcess(argv[0], childArgs, policy, limits);
            if (!isolated.isolationEstablished) {
                std::fprintf(stderr, "%s %s", isolated.diagnostic.c_str(), isolated.stderrOutput.c_str());
                return 50;
            }
            result = std::move(isolated);
        } else {
            result = plat::runProcessCaptureBounded(argv[0], childArgs, limits);
        }
        std::fputs(result.stdoutOutput.c_str(), stdout);
        return result.exitCode;
    }
    if (action == "sysctl-procargs" && argc == 3) {
        int mib[] = {CTL_KERN, KERN_PROCARGS2, std::stoi(argv[2])};
        size_t size = 0;
        if (::sysctl(mib, 3, nullptr, &size, nullptr, 0) != 0) {
            std::printf("sysctl denied errno=%d\n", errno);
            return 13;
        }
        std::string bytes(size, '\0');
        if (::sysctl(mib, 3, bytes.data(), &size, nullptr, 0) != 0) {
            std::printf("sysctl denied errno=%d\n", errno);
            return 13;
        }
        const bool found = bytes.find("TOPO_ANALYSIS_PARENT_SECRET=controlled-sysctl-value") != std::string::npos;
        std::printf("parent sentinel=%s child-env=%s\n",
                    found ? "yes" : "no",
                    std::getenv("TOPO_ANALYSIS_PARENT_SECRET") ? "present" : "absent");
        return found ? 0 : 14;
    }
    if (action == "sysctl-os-build") {
        char value[256]{};
        size_t size = sizeof(value);
        if (::sysctlbyname("kern.osversion", value, &size, nullptr, 0) != 0) return 13;
        std::puts(value);
        return 0;
    }
#endif
    if (action == "jit-execute") {
        void* memory = ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (memory == MAP_FAILED) return 13;
#if defined(__aarch64__)
        const unsigned int instructions[] = {0x52800540u, 0xD65F03C0u};
#else
        const unsigned char instructions[] = {0xb8, 0x2a, 0, 0, 0, 0xc3};
#endif
        std::memcpy(memory, instructions, sizeof(instructions));
        if (::mprotect(memory, 4096, PROT_READ | PROT_EXEC) != 0) {
            std::printf("mprotect errno=%d\n", errno);
            ::munmap(memory, 4096);
            return 13;
        }
        __builtin___clear_cache(static_cast<char*>(memory), static_cast<char*>(memory) + sizeof(instructions));
        const int value = reinterpret_cast<int (*)()>(memory)();
        ::munmap(memory, 4096);
        std::printf("executed=%d\n", value);
        return value == 42 ? 0 : 1;
    }
    if (action == "jit") {
#ifndef MAP_JIT
#define MAP_JIT 0
#endif
        void* memory =
            ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
        if (memory == MAP_FAILED) {
            std::printf("errno=%d\n", errno);
            return 13;
        }
        ::munmap(memory, 4096);
        return 0;
    }
#ifdef __linux__
    if (action == "exec-loader" && argc == 5) {
        ::execl(argv[2], argv[2], argv[3], "write-report", argv[4], static_cast<char*>(nullptr));
        std::printf("loader exec errno=%d\n", errno);
        return 13;
    }
    if (action == "chmod" && argc == 3) return ::chmod(argv[2], 0777) == 0 ? 0 : 13;
    if (action == "truncate" && argc == 3) return ::truncate(argv[2], 0) == 0 ? 0 : 13;
    if (action == "utimens" && argc == 3)
        return ::utimensat(AT_FDCWD, argv[2], nullptr, 0) == 0 ? 0 : 13;
    if (action == "unshare") return ::unshare(CLONE_NEWUSER) == 0 ? 0 : (errno == EPERM ? 13 : 14);
    if (action == "signal" && argc == 3) return ::kill(std::stoi(argv[2]), 0) == 0 ? 0 : 13;
    if (action == "process-memory" && argc == 3) {
        // Read our own byte through the cross-process interface. The address
        // is valid, so denial is independent of incidental inaccessible data.
        char source = 'x', destination = 0;
        iovec local{&destination, 1}, remote{&source, 1};
        const auto count = ::process_vm_readv(::getpid(), &local, 1, &remote, 1, 0);
        return count == 1 && destination == source ? 0 : 13;
    }
    if (action == "clone-process") {
        const long child = ::syscall(SYS_clone, SIGCHLD, nullptr, nullptr, nullptr, 0);
        if (child == 0) _exit(0);
        if (child < 0) return 13;
        ::waitpid(static_cast<pid_t>(child), nullptr, 0);
        return 0;
    }
    if (action == "proc-parent" && argc == 3) {
        namespace plat = topo::platform;
        plat::ProcessCaptureLimits limits;
        limits.environment = std::map<std::string, std::string>{};
        const std::vector<std::string> childArgs{"proc-environment", std::to_string(::getpid())};
        plat::BoundedProcessResult result;
        if (std::string(argv[2]) == "isolated") {
            plat::AnalysisProcessPolicy policy;
            policy.allowedExecutables.emplace_back(argv[0]);
            auto isolated = plat::experimental::runLinuxAnalysisProcess(argv[0], childArgs, policy, limits);
            if (!isolated.isolationEstablished) {
                std::fprintf(stderr, "%s %s", isolated.diagnostic.c_str(), isolated.stderrOutput.c_str());
                return 50;
            }
            result = std::move(isolated);
        } else {
            result = plat::runProcessCaptureBounded(argv[0], childArgs, limits);
        }
        std::fputs(result.stdoutOutput.c_str(), stdout);
        return result.exitCode;
    }
    if (action == "proc-environment" && argc == 3) {
        std::ifstream input(std::string("/proc/") + argv[2] + "/environ", std::ios::binary);
        if (!input) { std::printf("proc denied errno=%d\n", errno); return 13; }
        std::string bytes(std::istreambuf_iterator<char>(input), {});
        const bool found = bytes.find("TOPO_ANALYSIS_PARENT_SECRET=controlled-proc-value") != std::string::npos;
        std::printf("parent sentinel=%s child-env=%s\n", found ? "yes" : "no",
                    std::getenv("TOPO_ANALYSIS_PARENT_SECRET") ? "present" : "absent");
        return found ? 0 : 14;
    }
#endif
#endif
    return 2;
}
