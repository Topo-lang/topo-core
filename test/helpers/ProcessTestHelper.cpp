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
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

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

    if (mode == "exit") {
        return argc > 2 ? std::atoi(argv[2]) : 0;
    }

    if (mode == "sleep") {
        const long ms = argc > 2 ? std::atol(argv[2]) : 1000;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return 0;
    }

    std::fprintf(stderr, "process-test-helper: unknown mode '%s'\n",
                 mode.c_str());
    return 2;
}
