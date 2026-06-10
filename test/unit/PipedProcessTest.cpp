#include "topo/Platform/Process.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

// All tests spawn the dedicated stdio helper (test/helpers/
// ProcessTestHelper.cpp, path injected by CMake) instead of /bin/cat, so the
// suite runs — without skips — on Linux, macOS AND Windows. Windows coverage
// is the point: PipedProcess there is a raw-Win32 pipe backend (reproc's
// socket-based EOF/exit detection hangs for MinGW children), and these tests
// are what proves write/read/closeStdin/stop/exitCode behave identically to
// the POSIX reproc backend.

using topo::platform::PipedProcess;

namespace {

const char* helperPath() { return TOPO_PROCESS_TEST_HELPER; }

// Drain the child's stdout to EOF and return everything read.
std::string drainStdout(PipedProcess& proc) {
    std::string out;
    char buf[256];
    while (true) {
        size_t n = proc.read(buf, sizeof(buf));
        if (n == 0) break;
        out.append(buf, n);
    }
    return out;
}

} // namespace

// Regression test for the transpile-driver extractor stdin deadlock:
// Before closeStdin() existed, writing to a child that reads stdin to EOF
// and then reading its stdout would deadlock. The helper's cat mode is the
// minimal reproducer because it reads stdin to EOF before its final output
// flush. Also pins the clean-exit contract: after the child drains and
// exits on its own, stop() reports exit code 0.
TEST(PipedProcess, CloseStdinUnblocksReadToEofChild) {
    PipedProcess proc;
    ASSERT_TRUE(proc.start(helperPath(), {"cat"}));

    const std::string payload = "hello world\n";
    ASSERT_TRUE(proc.write(payload.data(), payload.size()));

    // Without closeStdin(), the read loop below would hang indefinitely
    // because cat is still waiting for more stdin data.
    proc.closeStdin();

    std::string out = drainStdout(proc);
    proc.stop();

    EXPECT_EQ(out, payload);
    EXPECT_EQ(proc.exitCode(), 0);
}

// Verify closeStdin() is idempotent — calling it twice should not crash
// or corrupt state. This matters because TranspileDriver::extractFunctions
// and subsequent stop() both try to close stdin.
TEST(PipedProcess, CloseStdinIdempotent) {
    PipedProcess proc;
    ASSERT_TRUE(proc.start(helperPath(), {"cat"}));

    const std::string payload = "idempotent\n";
    ASSERT_TRUE(proc.write(payload.data(), payload.size()));

    proc.closeStdin();
    proc.closeStdin();  // Second call must be a no-op.

    std::string out = drainStdout(proc);
    proc.stop();

    EXPECT_EQ(out, payload);
}

// After closeStdin(), subsequent write() must fail cleanly (no crash, no
// hang). This guards against accidental double-write patterns.
TEST(PipedProcess, WriteAfterCloseStdinFails) {
    PipedProcess proc;
    ASSERT_TRUE(proc.start(helperPath(), {"cat"}));

    const std::string first = "first\n";
    ASSERT_TRUE(proc.write(first.data(), first.size()));

    proc.closeStdin();

    const std::string second = "second\n";
    EXPECT_FALSE(proc.write(second.data(), second.size()));

    // Drain the first payload and ensure the child terminates cleanly.
    std::string out = drainStdout(proc);
    proc.stop();

    EXPECT_EQ(out, first);
}

// The LSP-bridge liveness pattern: the child must answer while the parent
// still holds stdin open (request → response → request → response). The
// helper's echo-lines mode flushes after every line, so each readByte()
// round below completes without an intervening stdin EOF — this is the
// behavior every framed-stdio consumer (LSPBridge, debug adapters) relies
// on, and exactly what a one-shot cat test cannot cover.
TEST(PipedProcess, DialogueRoundTripsBeforeStdinClose) {
    PipedProcess proc;
    ASSERT_TRUE(proc.start(helperPath(), {"echo-lines"}));

    auto readLine = [&proc]() {
        std::string line;
        while (true) {
            int c = proc.readByte();
            if (c < 0) break;  // EOF/error — surface as a short line
            line.push_back(static_cast<char>(c));
            if (c == '\n') break;
        }
        return line;
    };

    const std::string first = "first request\n";
    ASSERT_TRUE(proc.write(first.data(), first.size()));
    EXPECT_EQ(readLine(), first);

    const std::string second = "second request\n";
    ASSERT_TRUE(proc.write(second.data(), second.size()));
    EXPECT_EQ(readLine(), second);

    proc.closeStdin();
    EXPECT_EQ(drainStdout(proc), "");
    proc.stop();
    EXPECT_EQ(proc.exitCode(), 0);
}

// isRunning() must reflect the lifecycle and reap the exit status: true
// while the child lives, false after it exits, with exitCode() carrying the
// child's own exit code even when stop() is never the one to observe it.
TEST(PipedProcess, IsRunningReapsExitStatus) {
    PipedProcess proc;
    ASSERT_TRUE(proc.start(helperPath(), {"exit", "5"}));

    // The child exits on its own (no stdin involvement). Poll bounded:
    // 100 * 50ms = 5s worst case, far below the ctest timeout.
    bool running = true;
    for (int i = 0; i < 100 && (running = proc.isRunning()); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_FALSE(running);
    EXPECT_EQ(proc.exitCode(), 5);

    // stop() after the reap is a clean no-op that preserves the status.
    proc.stop();
    EXPECT_EQ(proc.exitCode(), 5);
}

// stop() must terminate a child that never exits on its own, within the
// escalation bounds (wait → terminate/kill) — i.e. return promptly instead
// of hanging, and leave the process reported as not running. Force-killed
// children report exitCode() == -1 per the header contract; the assertion
// stays at "not 0" because the POSIX backend derives the code from the
// termination signal.
TEST(PipedProcess, StopTerminatesUnresponsiveChild) {
    PipedProcess proc;
    ASSERT_TRUE(proc.start(helperPath(), {"sleep", "30000"}));
    EXPECT_TRUE(proc.isRunning());

    const auto before = std::chrono::steady_clock::now();
    proc.stop(/*timeoutMs=*/200);
    const auto elapsed = std::chrono::steady_clock::now() - before;

    EXPECT_FALSE(proc.isRunning());
    EXPECT_NE(proc.exitCode(), 0);
    // Generous bound: 200ms wait + termination grace, not the 30s sleep.
    EXPECT_LT(elapsed, std::chrono::seconds(15));
}

// A stop()ed PipedProcess must be start()able again with a fresh child —
// the rust-analyzer restart cycle. Exercises impl replacement (each start
// creates a new process/pipe set) on every platform.
TEST(PipedProcess, RestartAfterStopCycle) {
    PipedProcess proc;

    for (int round = 0; round < 2; ++round) {
        ASSERT_TRUE(proc.start(helperPath(), {"cat"})) << "round " << round;
        const std::string payload = "round " + std::to_string(round) + "\n";
        ASSERT_TRUE(proc.write(payload.data(), payload.size()));
        proc.closeStdin();
        EXPECT_EQ(drainStdout(proc), payload);
        proc.stop();
        EXPECT_EQ(proc.exitCode(), 0);
    }
}

// Starting a nonexistent tool must fail cleanly (false), not hang or crash —
// the TranspileDriver "extractor not found" diagnostic path.
TEST(PipedProcess, StartMissingToolFails) {
    PipedProcess proc;
    EXPECT_FALSE(proc.start("topo-definitely-missing-tool-xyz", {}));
    EXPECT_FALSE(proc.isRunning());
}
