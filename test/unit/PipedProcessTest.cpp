#include "topo/Platform/Process.h"

#include <gtest/gtest.h>
#include <string>

#ifdef _WIN32
namespace {
constexpr bool kPipedProcessTestEnabled = false;
}
#else
namespace {
constexpr bool kPipedProcessTestEnabled = true;
const char* kCatPath = "/bin/cat";
}
#endif

using topo::platform::PipedProcess;

// Regression test for the transpile-driver extractor stdin deadlock:
// Before closeStdin() existed, writing to a child that reads stdin to EOF
// and then reading its stdout would deadlock. cat is the minimal reproducer
// because it reads stdin to EOF before writing anything to stdout.
TEST(PipedProcess, CloseStdinUnblocksReadToEofChild) {
    if (!kPipedProcessTestEnabled) {
        GTEST_SKIP() << "PipedProcess cat-based test is POSIX-only";
        return;
    }
#ifndef _WIN32
    PipedProcess proc;
    ASSERT_TRUE(proc.start(kCatPath, {}));

    const std::string payload = "hello world\n";
    ASSERT_TRUE(proc.write(payload.data(), payload.size()));

    // Without closeStdin(), the read loop below would hang indefinitely
    // because cat is still waiting for more stdin data.
    proc.closeStdin();

    std::string out;
    char buf[256];
    while (true) {
        size_t n = proc.read(buf, sizeof(buf));
        if (n == 0) break;
        out.append(buf, n);
    }
    proc.stop();

    EXPECT_EQ(out, payload);
#endif
}

// Verify closeStdin() is idempotent — calling it twice should not crash
// or corrupt state. This matters because TranspileDriver::extractFunctions
// and subsequent stop() both try to close stdin.
TEST(PipedProcess, CloseStdinIdempotent) {
    if (!kPipedProcessTestEnabled) {
        GTEST_SKIP() << "PipedProcess cat-based test is POSIX-only";
        return;
    }
#ifndef _WIN32
    PipedProcess proc;
    ASSERT_TRUE(proc.start(kCatPath, {}));

    const std::string payload = "idempotent\n";
    ASSERT_TRUE(proc.write(payload.data(), payload.size()));

    proc.closeStdin();
    proc.closeStdin();  // Second call must be a no-op.

    std::string out;
    char buf[64];
    while (true) {
        size_t n = proc.read(buf, sizeof(buf));
        if (n == 0) break;
        out.append(buf, n);
    }
    proc.stop();

    EXPECT_EQ(out, payload);
#endif
}

// After closeStdin(), subsequent write() must fail cleanly (no crash, no
// hang). This guards against accidental double-write patterns.
TEST(PipedProcess, WriteAfterCloseStdinFails) {
    if (!kPipedProcessTestEnabled) {
        GTEST_SKIP() << "PipedProcess cat-based test is POSIX-only";
        return;
    }
#ifndef _WIN32
    PipedProcess proc;
    ASSERT_TRUE(proc.start(kCatPath, {}));

    const std::string first = "first\n";
    ASSERT_TRUE(proc.write(first.data(), first.size()));

    proc.closeStdin();

    const std::string second = "second\n";
    EXPECT_FALSE(proc.write(second.data(), second.size()));

    // Drain the first payload and ensure the child terminates cleanly.
    std::string out;
    char buf[64];
    while (true) {
        size_t n = proc.read(buf, sizeof(buf));
        if (n == 0) break;
        out.append(buf, n);
    }
    proc.stop();

    EXPECT_EQ(out, first);
#endif
}
