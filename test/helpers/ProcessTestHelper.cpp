// Tiny stdio helper spawned by the process/platform tests
// (PipedProcessTest, ProcessSpawnTest). It exists so those suites can run —
// not skip — on all three platforms: unlike /bin/cat it is guaranteed to be
// present wherever the test binary was built, and its behavior is identical
// everywhere (stdin/stdout are forced to binary mode on Windows so payload
// round-trips are byte-exact).
//
// Modes (argv[1], default "cat"):
//   cat               copy stdin to stdout until EOF
//   echo-lines        read stdin line by line, echo each line back
//                     immediately (response-before-stdin-EOF, the LSP
//                     framed-stdio liveness pattern)
//   echo-args ...     print each remaining argument on its own
//                     '\n'-terminated line (argv quoting round-trips)
//   exit <code>       exit immediately with <code>
//   sleep <ms>        sleep <ms> milliseconds, then exit 0 (an
//                     "unresponsive" child for stop()-escalation tests)

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

int emitBytes(size_t outBytes, size_t errBytes) {
    const std::string out(4096, 'o'), err(4096, 'e');
    while (outBytes || errBytes) {
        const size_t on = std::min(outBytes, out.size());
        const size_t en = std::min(errBytes, err.size());
        if (on && std::fwrite(out.data(), 1, on, stdout) != on) return 1;
        if (en && std::fwrite(err.data(), 1, en, stderr) != en) return 1;
        outBytes -= on;
        errBytes -= en;
    }
    std::fflush(stdout);
    std::fflush(stderr);
    return 0;
}

bool spawnDelayedWriter(char** argv) {
#ifdef _WIN32
    // Fixture paths never contain quotes, and all tokens are quoted so
    // temporary directories containing spaces work on Windows as well.
    std::string command = "\"" + std::string(argv[0]) + "\" delayed-write";
    for (int i = 2; i < 5; ++i) command += " \"" + std::string(argv[i]) + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE,
                        0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    const pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        execl(argv[0], argv[0], "delayed-write", argv[2], argv[3], argv[4],
              static_cast<char*>(nullptr));
        _exit(127);
    }
    return true;
#endif
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Byte-exact stdio: without this the CRT translates '\n' <-> "\r\n" and
    // the cat/echo-lines round-trip payloads would not compare equal.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    const std::string mode = argc > 1 ? argv[1] : "cat";

    if (mode == "cat") {
        char buf[4096];
        size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) {
            if (std::fwrite(buf, 1, n, stdout) != n) return 1;
            std::fflush(stdout);
        }
        return 0;
    }

    if (mode == "echo-lines") {
        int c = 0;
        while ((c = std::fgetc(stdin)) != EOF) {
            std::fputc(c, stdout);
            if (c == '\n') std::fflush(stdout);
        }
        std::fflush(stdout);
        return 0;
    }

    if (mode == "echo-args") {
        for (int i = 2; i < argc; ++i) {
            std::fputs(argv[i], stdout);
            std::fputc('\n', stdout);
        }
        std::fflush(stdout);
        return 0;
    }

    if (mode == "environment" && argc == 3) {
        const char* value = std::getenv(argv[2]);
        if (value) std::puts(value);
        return value ? 0 : 7;
    }

    if (mode == "exit") {
        return argc > 2 ? std::atoi(argv[2]) : 0;
    }

    if (mode == "sleep") {
        const long ms = argc > 2 ? std::atol(argv[2]) : 1000;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return 0;
    }

    if (mode == "emit-bytes" && argc == 4) {
        return emitBytes(std::strtoull(argv[2], nullptr, 10),
                         std::strtoull(argv[3], nullptr, 10));
    }

    if (mode == "delayed-write" && argc == 5) {
        { std::ofstream ready(argv[2]); ready << "running\n"; }
        std::this_thread::sleep_for(std::chrono::milliseconds(std::atol(argv[4])));
        std::ofstream sentinel(argv[3]);
        sentinel << "descendant executed\n";
        return sentinel ? 0 : 1;
    }

    if (mode == "spawn-descendant" && argc == 6) {
        if (!spawnDelayedWriter(argv)) return 3;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!std::filesystem::exists(argv[2])) {
            if (std::chrono::steady_clock::now() >= until) return 4;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::fputs("descendant-ready\n", stdout);
        std::fflush(stdout);
        const std::string action = argv[5];
        if (action == "exit") return 0;
        if (action == "flood") return emitBytes(64 * 1024 * 1024, 0);
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    }

    std::fprintf(stderr, "process-test-helper: unknown mode '%s'\n",
                 mode.c_str());
    return 2;
}
