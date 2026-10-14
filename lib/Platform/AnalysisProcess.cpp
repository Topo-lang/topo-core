#include "topo/Platform/AnalysisProcess.h"

#include <array>
#include <fstream>
#include <limits>
#include <sstream>
#if defined(__linux__) && defined(TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS)
#include "LinuxAnalysisSandbox.h"
#include <elf.h>
#endif
#if defined(__APPLE__) || (defined(__linux__) && defined(TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS))
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#endif

namespace topo::platform {
namespace {
namespace fs = std::filesystem;

AnalysisProcessResult rejected(const std::string& reason) {
    AnalysisProcessResult result;
    result.exitCode = -1;
    result.failure = ProcessCaptureFailure::StartFailed;
    result.diagnostic = reason;
    return result;
}

#if defined(__APPLE__) || (defined(__linux__) && defined(TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS))
constexpr const char* handshake = "TOPO_ANALYSIS_SANDBOX_ENTERED_V1\n";

bool nativeExecutable(const fs::path& file) {
    if (!file.is_absolute() || ::access(file.c_str(), X_OK) != 0) return false;
    std::ifstream in(file, std::ios::binary);
    std::array<unsigned char, 4> magic{};
    if (!in.read(reinterpret_cast<char*>(magic.data()), magic.size())) return false;
#ifdef __linux__
    if (magic != std::array<unsigned char, 4>{0x7f, 0x45, 0x4c, 0x46}) return false;
    std::array<unsigned char, 16> header{};
    if (!in.read(reinterpret_cast<char*>(header.data()), header.size())) return false;
    // Native 64-bit little-endian ELF only: a foreign ELF could invoke a
    // binfmt interpreter that was not part of the bound execution tool chain.
    if (header[0] != 2 || header[1] != 1 || header[2] != 1) return false;
    const unsigned type = header[12] | (header[13] << 8);
    const unsigned machine = header[14] | (header[15] << 8);
#if defined(__x86_64__)
    constexpr unsigned nativeMachine = 62;
#elif defined(__aarch64__)
    constexpr unsigned nativeMachine = 183;
#else
    constexpr unsigned nativeMachine = 0;
#endif
    return (type == 2 || type == 3) && machine == nativeMachine;
#else
    return magic == std::array<unsigned char, 4>{0xcf, 0xfa, 0xed, 0xfe} ||
           magic == std::array<unsigned char, 4>{0xce, 0xfa, 0xed, 0xfe} ||
           magic == std::array<unsigned char, 4>{0xfe, 0xed, 0xfa, 0xcf} ||
           magic == std::array<unsigned char, 4>{0xfe, 0xed, 0xfa, 0xce} ||
           magic == std::array<unsigned char, 4>{0xca, 0xfe, 0xba, 0xbe} ||
           magic == std::array<unsigned char, 4>{0xca, 0xfe, 0xba, 0xbf};
#endif
}

fs::path launcher() {
    std::vector<fs::path> candidates;
    std::error_code ec;
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> bytes(size);
    if (_NSGetExecutablePath(bytes.data(), &size) == 0) {
        const auto executable = fs::canonical(bytes.data(), ec);
        if (!ec) candidates.push_back(executable.parent_path() / "topo-analysis-launcher");
    }
#else
    const auto executable = fs::canonical("/proc/self/exe", ec);
    if (!ec) candidates.push_back(executable.parent_path() / "topo-analysis-launcher");
#endif
#ifdef TOPO_ANALYSIS_LAUNCHER_PATH
    candidates.emplace_back(TOPO_ANALYSIS_LAUNCHER_PATH);
#endif
#ifdef TOPO_ANALYSIS_INSTALLED_LAUNCHER_PATH
    candidates.emplace_back(TOPO_ANALYSIS_INSTALLED_LAUNCHER_PATH);
#endif
    for (const auto& candidate : candidates) {
        const auto resolved = fs::canonical(candidate, ec);
        if (!ec && nativeExecutable(resolved)) return resolved;
    }
    return {};
}

#ifdef __APPLE__
std::string quote(const fs::path& path) {
    std::string out = "\"";
    for (const char c : path.string()) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out + "\"";
}
#endif

bool resolved(const fs::path& input, bool directory, fs::path& output) {
    const auto text = input.string();
    if (!input.is_absolute() || text.find('\0') != std::string::npos || text.find('\n') != std::string::npos ||
        text.find('\r') != std::string::npos)
        return false;
    std::error_code ec;
    output = fs::canonical(input, ec);
    if (ec) return false;
    return directory ? fs::is_directory(output, ec) && !ec : fs::is_regular_file(output, ec) && !ec;
}

#ifdef __linux__
bool nativeInterpreter(const fs::path& executable, fs::path& interpreter) {
    interpreter.clear();
    std::error_code error;
    const auto size = fs::file_size(executable, error);
    if (error) return false;
    std::ifstream input(executable, std::ios::binary);
    Elf64_Ehdr header{};
    if (!input.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
        header.e_phentsize != sizeof(Elf64_Phdr) || header.e_phnum == 0 || header.e_phnum > 1024 ||
        header.e_phoff > size || header.e_phnum > (size - header.e_phoff) / sizeof(Elf64_Phdr))
        return false;
    for (unsigned i = 0; i < header.e_phnum; ++i) {
        Elf64_Phdr segment{};
        input.seekg(static_cast<std::streamoff>(header.e_phoff + i * sizeof(segment)));
        if (!input.read(reinterpret_cast<char*>(&segment), sizeof(segment))) return false;
        if (segment.p_type != PT_INTERP) continue;
        if (!interpreter.empty() || segment.p_filesz < 2 || segment.p_filesz > 4096 ||
            segment.p_offset > size || segment.p_filesz > size - segment.p_offset)
            return false;
        std::string path(static_cast<size_t>(segment.p_filesz), '\0');
        input.seekg(static_cast<std::streamoff>(segment.p_offset));
        if (!input.read(path.data(), static_cast<std::streamsize>(path.size())) ||
            path.back() != '\0' || path.find('\0') != path.size() - 1)
            return false;
        path.pop_back();
        if (!resolved(path, false, interpreter) || !nativeExecutable(interpreter)) return false;
    }
    return true;
}
#endif

struct PrivateDirectory {
    fs::path path;
    ~PrivateDirectory() {
        if (!path.empty()) clean();
    }
    bool clean() {
        std::error_code ec;
        const auto rootStatus = fs::symlink_status(path, ec);
        if (ec) return false;
        if (!fs::is_directory(rootStatus)) {
            fs::remove(path, ec);
            if (ec) return false;
            path.clear();
            return true;
        }
        // A target may chmod a directory to 000. Restore traversal rights on
        // owned directories without following symlinks outside the private tree.
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ec);
        if (ec) return false;
        fs::recursive_directory_iterator entry(path, ec), end;
        while (!ec && entry != end) {
            const auto status = entry->symlink_status(ec);
            if (ec) break;
            if (fs::is_directory(status)) {
                fs::permissions(entry->path(), fs::perms::owner_all, fs::perm_options::add, ec);
                if (ec) break;
            }
            entry.increment(ec);
        }
        if (ec) return false;
        fs::remove_all(path, ec);
        if (ec) return false;
        path.clear();
        return true;
    }
    bool create() {
        // The inherited TMPDIR is untrusted and never chooses this writable scope.
#ifdef __APPLE__
        char pattern[] = "/private/tmp/topo-analysis-XXXXXX";
#else
        char pattern[] = "/tmp/topo-analysis-XXXXXX";
#endif
        const char* created = ::mkdtemp(pattern);
        if (!created) return false;
        path = created;
        return ::chmod(path.c_str(), 0700) == 0;
    }
};

std::string expand(std::string text, const fs::path& temporary) {
    const std::string token = AnalysisTemporaryDirectoryToken;
    size_t offset = 0;
    while ((offset = text.find(token, offset)) != std::string::npos) {
        text.replace(offset, token.size(), temporary.string());
        offset += temporary.string().size();
    }
    return text;
}
#endif
} // namespace

AnalysisIsolationCapability analysisProcessCapability() {
#ifdef __APPLE__
    std::error_code error;
    const auto backend = fs::canonical("/usr/bin/sandbox-exec", error);
    if (error || !nativeExecutable(backend))
        return {false, "macos-seatbelt", "system sandbox-exec is unavailable", {}, {}};
    const auto helper = launcher();
    if (helper.empty()) return {false, "macos-seatbelt", "installed topo-analysis-launcher is unavailable", {}, {}};
    std::array<char, 256> build{};
    size_t buildSize = build.size();
    struct utsname system{};
    if (::sysctlbyname("kern.osversion", build.data(), &buildSize, nullptr, 0) != 0 || buildSize < 2 ||
        buildSize > build.size() || build[buildSize - 1] != '\0' || ::uname(&system) != 0 || system.machine[0] == '\0')
        return {false, "macos-seatbelt", "cannot establish kernel build and architecture identity", {}, {}};
    return {true,
            "macos-seatbelt",
            "system backend and native launcher available; application is verified per invocation",
            {backend, helper},
            "seatbelt-analysis-v2:" + std::string(build.data()) + ":" + system.machine};
#elif defined(__linux__)
    return {false, "linux-audit-unsupported",
            "strict Linux audit isolation is unsupported: exact executable-image confinement is not established; "
            "experimental resource isolation is a separate API", {}, {}};
#else
    return {false, "", "OS analysis isolation is unsupported on this platform", {}, {}};
#endif
}

namespace experimental {
AnalysisIsolationCapability linuxAnalysisProcessCapability() {
#if defined(__linux__) && defined(TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS)
    std::string diagnostic;
    const int abi = detail::linuxAnalysisLandlockAbi(diagnostic);
    if (!abi) return {false, "experimental-linux-landlock-seccomp", diagnostic, {}, {}};
    const auto helper = launcher();
    if (helper.empty())
        return {false, "experimental-linux-landlock-seccomp", "installed topo-analysis-launcher is unavailable", {}, {}};
    fs::path interpreter;
    if (!nativeInterpreter(helper, interpreter))
        return {false, "experimental-linux-landlock-seccomp", "cannot bind native launcher's ELF interpreter", {}, {}};
    std::vector<fs::path> executionTools{helper};
    if (!interpreter.empty()) {
        fs::path nestedInterpreter;
        if (!nativeInterpreter(interpreter, nestedInterpreter) || !nestedInterpreter.empty())
            return {false, "experimental-linux-landlock-seccomp", "ELF interpreter has an unsupported startup chain", {}, {}};
        executionTools.push_back(interpreter);
    }
    struct utsname system{};
    if (::uname(&system) != 0 || system.release[0] == '\0' || system.machine[0] == '\0')
        return {false, "experimental-linux-landlock-seccomp", "cannot establish kernel and architecture identity", {}, {}};
    return {true, "experimental-linux-landlock-seccomp",
            "experimental resource isolation only, not a strict audit guarantee; application is verified per invocation; "
            "bound ELF loader can run readable images; readable files may be mapped executable; "
            "metadata mutations are denied globally",
            executionTools, "experimental-landlock-seccomp-resource-v1:" + std::string(system.release) + ":" + system.version +
                ":" + system.machine + ":abi" + std::to_string(abi)};
#else
    return {false, "experimental-linux-landlock-seccomp",
            "experimental Linux resource isolation is disabled or unavailable on this platform", {}, {}};
#endif
}
} // namespace experimental

namespace {
AnalysisProcessResult runAnalysisProcessWithCapability(const AnalysisIsolationCapability& capability,
                                         const std::string& executable,
                                         const std::vector<std::string>& args,
                                         const AnalysisProcessPolicy& policy,
                                         const ProcessCaptureLimits& limits,
                                         bool verbose) {
    if (!capability.supported) return rejected(capability.diagnostic);
#if !defined(__APPLE__) && !(defined(__linux__) && defined(TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS))
    (void)executable;
    (void)args;
    (void)policy;
    (void)limits;
    (void)verbose;
    return rejected("OS analysis isolation is unsupported on this platform; target was not started");
#else
    fs::path target;
    if (!resolved(executable, false, target) || !nativeExecutable(target))
        return rejected("analysis target must be an absolute native executable");
#ifdef __APPLE__
    const auto& backend = capability.executionTools[0];
    const auto& helper = capability.executionTools[1];
    std::ostringstream profile;
    // deny default does not close the process-info operation family, and a
    // wildcard sysctl-read rule can reopen it. Explicitly restrict process
    // information to this process and enumerate hardware/OS queries.
    profile << "(version 1)\n(deny default)\n(deny process-info*)\n(allow process-info* (target self))\n"
               "(allow sysctl-read (sysctl-name "
               "\"kern.osrelease\" \"kern.osversion\" \"kern.ostype\" \"kern.osproductversion\" "
               "\"hw.ncpu\" \"hw.activecpu\" \"hw.logicalcpu\" \"hw.logicalcpu_max\" "
               "\"hw.physicalcpu\" \"hw.physicalcpu_max\" \"hw.memsize\" \"hw.pagesize\" "
               "\"hw.pagesize_compat\" \"hw.cachelinesize\" \"hw.cpufamily\" "
               "\"hw.cputype\" \"hw.cpusubtype\" \"hw.cpufrequency\" "
               "\"hw.cpufrequency_min\" \"hw.cpufrequency_max\" \"hw.l1icachesize\" "
               "\"hw.l1dcachesize\" \"hw.l2cachesize\" \"hw.l3cachesize\" "
               "\"hw.optional.armv8_crc32\" \"hw.optional.arm.FEAT_LSE\" "
               "\"hw.optional.armv8_1_atomics\" \"hw.optional.arm.FEAT_SHA512\" "
               "\"hw.optional.armv8_2_sha512\" \"hw.optional.arm.FEAT_SHA3\" "
               "\"hw.optional.armv8_2_sha3\" \"hw.optional.arm.FEAT_SB\"))\n"
               "(deny sysctl-read (sysctl-name-prefix \"kern.proc\"))\n"
               "(allow file-read-metadata)\n(allow syscall-unix)\n"
               "(deny syscall-unix (syscall-number SYS_setsid SYS_setpgid))\n";
    auto grant = [&](const fs::path& path, bool directory, bool executableImage) {
        profile << "(allow file-read*" << (executableImage ? " file-map-executable" : "") << " ("
                << (directory ? "subpath " : "literal ") << quote(path) << "))\n";
    };
    // Keep /System/Volumes/Data out of the runtime roots: it aliases user data.
    for (const char* path : {"/System/Library",
                             "/usr/lib",
                             "/Library/Apple/System/Library",
                             "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld",
                             "/System/Volumes/Preboot/Cryptexes/OS/usr/lib"}) {
        grant(path, true, true);
    }
    std::vector<std::string> sandboxArgs;
#else
    const auto& helper = capability.executionTools[0];
    const auto& backend = helper;
    std::vector<std::string> sandboxArgs;
    fs::path interpreter;
    if (capability.executionTools.size() == 2) {
        interpreter = capability.executionTools[1];
        // The kernel opens PT_INTERP with execute access. Binding this exact
        // loader is part of the startup chain, never a directory-wide grant.
        sandboxArgs.insert(sandboxArgs.end(), {"--execute", interpreter.string()});
    }
    auto grant = [&](const fs::path& path, bool directory, bool) {
        sandboxArgs.push_back(directory ? "--read-directory" : "--read-file");
        sandboxArgs.push_back(path.string());
    };
    // Read-only system runtimes. Project-selected tools remain exact exec
    // grants; reading a library never grants permission to exec its pathname.
    for (const char* path : {"/lib", "/lib64", "/usr/lib", "/usr/lib64"}) {
        std::error_code error;
        if (fs::is_directory(path, error) && !error) grant(fs::canonical(path), true, true);
    }
    for (const char* path : {"/etc/ld.so.cache", "/proc/cpuinfo", "/proc/meminfo"}) {
        std::error_code error;
        if (fs::is_regular_file(path, error) && !error) grant(path, false, false);
    }
#endif
    bool permitted = false;
    for (const auto& item : policy.readOnlyFiles) {
        fs::path path;
        if (!resolved(item, false, path)) return rejected("invalid analysis input file: " + item.string());
        grant(path, false, false);
    }
    for (const auto& item : policy.readOnlyDirectories) {
        fs::path path;
        if (!resolved(item, true, path)) return rejected("invalid analysis input directory: " + item.string());
        grant(path, true, false);
    }
    for (const auto& item : policy.toolchainFiles) {
        fs::path path;
        if (!resolved(item, false, path)) return rejected("invalid analysis runtime file: " + item.string());
        grant(path, false, true);
    }
    for (const auto& item : policy.toolchainDirectories) {
        fs::path path;
        if (!resolved(item, true, path)) return rejected("invalid analysis runtime directory: " + item.string());
        grant(path, true, true);
    }
    for (const auto& item : policy.allowedExecutables) {
        fs::path path;
        if (!resolved(item, false, path) || !nativeExecutable(path))
            return rejected("invalid allowed native executable: " + item.string());
        if (path == target) permitted = true;
        grant(path, false, true);
#ifdef __APPLE__
        profile << "(allow process-exec (literal " << quote(path) << "))\n";
#else
        fs::path selectedInterpreter;
        if (!nativeInterpreter(path, selectedInterpreter) ||
            (!selectedInterpreter.empty() && selectedInterpreter != interpreter))
            return rejected("native analysis executable does not use the bound ELF interpreter: " + path.string());
        sandboxArgs.insert(sandboxArgs.end(), {"--execute", path.string()});
#endif
    }
    if (!permitted) return rejected("analysis target is absent from exact executable allowlist");
#ifdef __APPLE__
    grant(helper, false, true);
    profile << "(allow process-exec (literal " << quote(helper) << "))\n";
    // dyld opens the root and cwd themselves; these literals grant no child files.
    std::error_code ec;
    const auto currentDirectory = fs::canonical(fs::current_path(ec), ec);
    if (ec) return rejected("cannot resolve analysis working directory");
    grant("/", false, false);
    grant(currentDirectory, false, false);
#endif
    for (const char* path : {"/dev/null", "/dev/random", "/dev/urandom"})
        grant(path, false, false);
#ifdef __APPLE__
    profile << "(allow file-write-data (literal \"/dev/null\"))\n";
#else
    sandboxArgs.insert(sandboxArgs.end(), {"--null-device", "/dev/null"});
#endif
    PrivateDirectory temporary;
    if (!temporary.create()) return rejected("cannot create private analysis directory");
#ifdef __APPLE__
    profile << "(allow file-read* file-write* (subpath " << quote(temporary.path) << "))\n";
    // Seatbelt's dynamic-code-generation operation gates MAP_JIT, not every
    // RW-to-RX mapping. In-process code generation is explicitly permitted;
    // resource access is governed by the filesystem/network/process policy.
    profile << "(allow dynamic-code-generation)\n";
#else
    sandboxArgs.insert(sandboxArgs.end(), {"--temporary", temporary.path.string()});
#endif
    auto boundedLimits = limits;
    boundedLimits.environment = policy.environment;
    for (auto& entry : *boundedLimits.environment)
        entry.second = expand(entry.second, temporary.path);
    for (const char* name : {"HOME", "TMPDIR", "TMP", "TEMP"})
        (*boundedLimits.environment)[name] = temporary.path.string();
    const size_t markerBytes = std::char_traits<char>::length(handshake);
    if (boundedLimits.maxStdoutBytes > std::numeric_limits<size_t>::max() - markerBytes)
        return rejected("analysis stdout limit overflows handshake capacity");
    boundedLimits.maxStdoutBytes += markerBytes;
#ifdef __APPLE__
    sandboxArgs = {"-p", profile.str(), helper.string(), target.string()};
#else
    sandboxArgs.insert(sandboxArgs.end(), {"--", target.string()});
#endif
    for (const auto& arg : args)
        sandboxArgs.push_back(expand(arg, temporary.path));
    auto captured = runProcessCaptureBounded(backend.string(), sandboxArgs, boundedLimits, verbose);
    AnalysisProcessResult result;
    static_cast<BoundedProcessResult&>(result) = std::move(captured);
    result.isolation = capability.backend;
    result.executionTools = capability.executionTools;
    result.platformIdentity = capability.platformIdentity;
    if (result.stdoutOutput.compare(0, markerBytes, handshake) == 0) {
        result.stdoutOutput.erase(0, markerBytes);
        result.isolationEstablished = true;
    } else {
        result.failure = ProcessCaptureFailure::StartFailed;
        result.exitCode = -1;
        result.diagnostic = "OS analysis isolation did not establish the trusted launcher handshake";
        if (result.stdoutOutput.size() > limits.maxStdoutBytes) result.stdoutOutput.resize(limits.maxStdoutBytes);
    }
    if (!temporary.clean()) {
        result.exitCode = -1;
        result.failure = ProcessCaptureFailure::CleanupFailed;
        result.diagnostic = "cannot remove private analysis directory: " + temporary.path.string();
    }
    return result;
#endif
}
} // namespace

AnalysisProcessResult runAnalysisProcess(const std::string& executable,
                                         const std::vector<std::string>& args,
                                         const AnalysisProcessPolicy& policy,
                                         const ProcessCaptureLimits& limits,
                                         bool verbose) {
    return runAnalysisProcessWithCapability(analysisProcessCapability(), executable, args, policy, limits, verbose);
}

namespace experimental {
AnalysisProcessResult runLinuxAnalysisProcess(const std::string& executable,
                                              const std::vector<std::string>& args,
                                              const AnalysisProcessPolicy& policy,
                                              const ProcessCaptureLimits& limits,
                                              bool verbose) {
    return runAnalysisProcessWithCapability(linuxAnalysisProcessCapability(), executable, args, policy, limits, verbose);
}
} // namespace experimental
} // namespace topo::platform
