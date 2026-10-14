// The startup marker is emitted only after the platform sandbox is applied.
// macOS enters through sandbox-exec; Linux establishes Landlock and seccomp here.
#include <cerrno>
#include <cstdio>
#include <unistd.h>
#ifdef __linux__
#include "LinuxAnalysisSandbox.h"
#endif

int main(int argc, char** argv) {
    if (argc < 2) return 125;
    int targetIndex = 1;
#ifdef __linux__
    std::string diagnostic;
    if (!topo::platform::detail::establishLinuxAnalysisSandbox(argc, argv, targetIndex, diagnostic)) {
        std::fprintf(stderr, "analysis isolation: %s\n", diagnostic.c_str());
        return 125;
    }
#endif
    constexpr char marker[] = "TOPO_ANALYSIS_SANDBOX_ENTERED_V1\n";
    size_t offset = 0;
    while (offset < sizeof(marker) - 1) {
        const ssize_t count = ::write(STDOUT_FILENO, marker + offset, sizeof(marker) - 1 - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 125;
        offset += static_cast<size_t>(count);
    }
    ::execv(argv[targetIndex], argv + targetIndex);
    std::perror("analysis target exec");
    return 126;
}
