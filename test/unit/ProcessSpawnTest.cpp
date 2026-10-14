#include "topo/Platform/Platform.h"
#include "topo/Platform/Process.h"
#include "topo/Platform/TempFile.h"
#include "topo/Platform/ToolResolution.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h> // _getpid
#else
#include <unistd.h> // getpid
#endif

// Spawn-path coverage that must run — not skip — on Linux, macOS AND
// Windows, spawning the dedicated stdio helper (test/helpers/
// ProcessTestHelper.cpp, path injected by CMake):
//   * runProcess exit-code retrieval (raw-Win32 backend on Windows);
//   * argv quoting round-trips through runProcessCapture and PipedProcess;
//   * bare-name launcher resolution: a `<name>.cmd` staged on PATH must be
//     found and executed on Windows (CreateProcessW alone appends only
//     ".exe", the bug behind the npm/jdtls/topo-extract-* launchers), with
//     the equivalent extensionless shell script staged on POSIX so the same
//     assertions exercise findSpawnableOnPath + execvp there.

namespace fs = std::filesystem;
namespace plat = topo::platform;

namespace {

const char* helperPath() { return TOPO_PROCESS_TEST_HELPER; }

int currentPid() {
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(::getpid());
#endif
}

void setPathEnv(const std::string& value) {
#ifdef _WIN32
    _putenv_s("PATH", value.c_str());
#else
    setenv("PATH", value.c_str(), 1);
#endif
}

// Stages a bare-name launcher that forwards all arguments to the helper
// binary, in a fresh temp directory prepended to PATH for the fixture's
// lifetime:
//   Windows: `<dir>/<name>.cmd`   — `@"<helper>" %*`
//   POSIX:   `<dir>/<name>`       — `#!/bin/sh` + `exec "<helper>" "$@"`
// This is the production shape of topo-extract-{typescript,python,java}
// (and the npm/jdtls shims): a script launcher resolved by bare name.
class LauncherOnPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        launcherName_ =
            "topo-test-spawn-launcher-" + std::to_string(currentPid());
        dir_ = plat::tempDirectory() /
               ("topo-spawn-test-" + std::to_string(currentPid()));
        std::error_code ec;
        fs::create_directories(dir_, ec);
        ASSERT_FALSE(ec) << "failed to create " << dir_;

        // Native separators in the staged script: cmd.exe is only reliable
        // with backslashed program paths.
        const std::string helper =
            fs::path(helperPath()).make_preferred().string();
        if (plat::IsWindows) {
            launcherPath_ = dir_ / (launcherName_ + ".cmd");
            std::ofstream out(launcherPath_, std::ios::binary);
            out << "@\"" << helper << "\" %*\r\n";
        } else {
            launcherPath_ = dir_ / launcherName_;
            {
                std::ofstream out(launcherPath_, std::ios::binary);
                out << "#!/bin/sh\nexec \"" << helper << "\" \"$@\"\n";
            }
            fs::permissions(launcherPath_,
                            fs::perms::owner_all | fs::perms::group_read |
                                fs::perms::group_exec | fs::perms::others_read |
                                fs::perms::others_exec,
                            ec);
            ASSERT_FALSE(ec);
        }

        const char* old = std::getenv("PATH");
        oldPath_ = old ? old : "";
        setPathEnv(dir_.string() + std::string(plat::PathSeparator) + oldPath_);
    }

    void TearDown() override {
        setPathEnv(oldPath_);
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    std::string launcherName_;
    fs::path dir_;
    fs::path launcherPath_;
    std::string oldPath_;
};

std::string drainStdout(plat::PipedProcess& proc) {
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

// runProcess must spawn a real tool and report its exit code — the
// fire-and-forget path every build driver rides (raw Win32 on Windows).
TEST(ProcessSpawn, RunProcessReturnsExitCode) {
    auto ok = plat::runProcess(helperPath(), {"exit", "0"});
    EXPECT_EQ(ok.exitCode, 0);

    auto fail = plat::runProcess(helperPath(), {"exit", "7"});
    EXPECT_EQ(fail.exitCode, 7);
}

// A missing tool must fail cleanly with -1, not hang.
TEST(ProcessSpawn, RunProcessMissingToolFails) {
    auto r = plat::runProcess("topo-definitely-missing-tool-xyz", {});
    EXPECT_EQ(r.exitCode, -1);
}

// Arguments with spaces, tabs, embedded quotes and trailing backslashes must
// round-trip byte-exactly into the child's argv. On Windows this pins the
// CommandLineToArgvW quoting in buildCommandLineW; on POSIX argv passes
// through verbatim, so the expectation is identical.
TEST(ProcessSpawn, CaptureArgsRoundTripQuoting) {
    const std::vector<std::string> args = {
        "plain", "with space", "tab\there", "quote\"mark", "trail\\", ""};

    std::vector<std::string> argv = {"echo-args"};
    argv.insert(argv.end(), args.begin(), args.end());
    auto r = plat::runProcessCapture(helperPath(), argv);
    ASSERT_EQ(r.exitCode, 0) << r.stderrOutput;

    std::string expected;
    for (const auto& a : args) expected += a + "\n";
    EXPECT_EQ(r.stdoutOutput, expected);
}

// Same round-trip through PipedProcess — the path the extractors and LSP
// bridges use (on Windows a different spawn site than capture).
TEST(ProcessSpawn, PipedArgsRoundTripQuoting) {
    const std::vector<std::string> args = {"alpha", "beta gamma",
                                           "quote\"mark"};

    plat::PipedProcess proc;
    std::vector<std::string> argv = {"echo-args"};
    argv.insert(argv.end(), args.begin(), args.end());
    ASSERT_TRUE(proc.start(helperPath(), argv));
    proc.closeStdin();

    std::string expected;
    for (const auto& a : args) expected += a + "\n";
    EXPECT_EQ(drainStdout(proc), expected);
    proc.stop();
    EXPECT_EQ(proc.exitCode(), 0);
}

// findSpawnableOnPath must locate the staged launcher by bare name and
// return an absolute path (on Windows that means probing the ".cmd" suffix
// CreateProcessW never tries).
TEST_F(LauncherOnPathTest, FindSpawnableResolvesStagedLauncher) {
    std::string found = plat::findSpawnableOnPath(launcherName_);
    ASSERT_FALSE(found.empty());
    EXPECT_TRUE(fs::path(found).is_absolute()) << found;
    std::error_code ec;
    EXPECT_TRUE(fs::equivalent(fs::path(found), launcherPath_, ec))
        << found << " vs " << launcherPath_;

    EXPECT_EQ(plat::findSpawnableOnPath("topo-definitely-missing-tool-xyz"),
              "");
}

// The production extractor flow end to end, by bare name: PipedProcess
// spawns the launcher (via cmd.exe /c on Windows), writes stdin, closes it,
// drains stdout. This is exactly what TranspileDriver::extractFunctions does
// with topo-extract-{typescript,python,java}, which are staged as .cmd on
// Windows.
TEST_F(LauncherOnPathTest, PipedRoundTripThroughBareNameLauncher) {
    plat::PipedProcess proc;
    ASSERT_TRUE(proc.start(launcherName_, {"cat"}));

    const std::string payload = "launcher round trip\n";
    ASSERT_TRUE(proc.write(payload.data(), payload.size()));
    proc.closeStdin();

    EXPECT_EQ(drainStdout(proc), payload);
    proc.stop();
    EXPECT_EQ(proc.exitCode(), 0);
}

// Capture through the bare-name launcher, with a spaced argument — pins the
// cmd.exe /d /s /c force-quoting contract: the argument must survive the
// batch file's %* forwarding intact.
TEST_F(LauncherOnPathTest, CaptureThroughBareNameLauncherForwardsArgs) {
    auto r = plat::runProcessCapture(launcherName_,
                                     {"echo-args", "alpha", "beta gamma"});
    ASSERT_EQ(r.exitCode, 0) << r.stderrOutput;
    EXPECT_EQ(r.stdoutOutput, "alpha\nbeta gamma\n");
}

TEST(ProcessBoundedCapture, NormalStreamsAndExitStatus) {
    plat::ProcessCaptureLimits limits;
    limits.maxStdoutBytes = 100003;
    limits.maxStderrBytes = 70001;
    const auto r = plat::runProcessCaptureBounded(
        helperPath(), {"emit-bytes", "100003", "70001"}, limits);
    EXPECT_EQ(r.failure, plat::ProcessCaptureFailure::None) << r.diagnostic;
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(r.stdoutOutput, std::string(100003, 'o'));
    EXPECT_EQ(r.stderrOutput, std::string(70001, 'e'));
    const auto failed = plat::runProcessCaptureBounded(helperPath(), {"exit", "7"}, limits);
    EXPECT_EQ(failed.failure, plat::ProcessCaptureFailure::None);
    EXPECT_EQ(failed.exitCode, 7);
}

TEST(ProcessBoundedCapture, BothStreamsHaveIndependentCaps) {
    plat::ProcessCaptureLimits limits;
    limits.maxStdoutBytes = 17003;
    limits.maxStderrBytes = 17003;
    for (const bool stderrFlood : {false, true}) {
        const auto r = plat::runProcessCaptureBounded(helperPath(),
            {"emit-bytes", stderrFlood ? "0" : "67108864",
                           stderrFlood ? "67108864" : "0"}, limits);
        EXPECT_EQ(r.failure, plat::ProcessCaptureFailure::OutputLimitExceeded)
            << r.diagnostic;
        EXPECT_EQ(r.exitCode, -1);
        EXPECT_EQ(stderrFlood ? r.stderrOutput : r.stdoutOutput,
                  std::string(17003, stderrFlood ? 'e' : 'o'));
        EXPECT_TRUE((stderrFlood ? r.stdoutOutput : r.stderrOutput).empty());
    }
}

TEST(ProcessBoundedCapture, ZeroLimitAllowsOnlyEmptyOutput) {
    plat::ProcessCaptureLimits limits;
    limits.maxStdoutBytes = 0;
    limits.maxStderrBytes = 0;
    const auto empty = plat::runProcessCaptureBounded(helperPath(), {"exit", "0"}, limits);
    EXPECT_EQ(empty.failure, plat::ProcessCaptureFailure::None);
    EXPECT_EQ(empty.exitCode, 0);
    const auto one = plat::runProcessCaptureBounded(helperPath(), {"emit-bytes", "1", "0"}, limits);
    EXPECT_EQ(one.failure, plat::ProcessCaptureFailure::OutputLimitExceeded);
    EXPECT_EQ(one.exitCode, -1);
    EXPECT_TRUE(one.stdoutOutput.empty());
}

TEST(ProcessBoundedCapture, StdinRoundTripAndUnreadInputDeadline) {
    plat::ProcessCaptureLimits limits;
    limits.stdinInput = std::string(512 * 1024, 'x');
    limits.stdinInput[32] = '\0';
    limits.stdinInput[64] = '\n';
    const auto echoed = plat::runProcessCaptureBounded(helperPath(), {"cat"}, limits);
    EXPECT_EQ(echoed.failure, plat::ProcessCaptureFailure::None) << echoed.diagnostic;
    EXPECT_EQ(echoed.exitCode, 0);
    EXPECT_EQ(echoed.stdoutOutput, limits.stdinInput);
    limits.timeoutMs = 100;
    const auto start = std::chrono::steady_clock::now();
    const auto stalled = plat::runProcessCaptureBounded(helperPath(), {"sleep", "30000"}, limits);
    EXPECT_EQ(stalled.failure, plat::ProcessCaptureFailure::TimedOut);
    EXPECT_EQ(stalled.exitCode, -1);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(6));
}

TEST(ProcessBoundedCapture, RejectsInvalidLimitsBeforeSpawn) {
    plat::ProcessCaptureLimits limits;
    limits.timeoutMs = 0;
    const auto noDeadline = plat::runProcessCaptureBounded(helperPath(), {"echo-args", "ran"}, limits);
    EXPECT_EQ(noDeadline.failure, plat::ProcessCaptureFailure::InvalidLimits);
    EXPECT_EQ(noDeadline.exitCode, -1);
    EXPECT_TRUE(noDeadline.stdoutOutput.empty());
    limits.timeoutMs = 100;
    limits.maxStdinBytes = 1;
    limits.stdinInput = "xx";
    const auto input = plat::runProcessCaptureBounded(helperPath(), {"echo-args", "ran"}, limits);
    EXPECT_EQ(input.failure, plat::ProcessCaptureFailure::InvalidLimits);
    EXPECT_EQ(input.exitCode, -1);
    EXPECT_TRUE(input.stdoutOutput.empty());
}

TEST(ProcessBoundedCapture, MissingToolFailsCleanly) {
    const auto r = plat::runProcessCaptureBounded(
        "topo-definitely-missing-tool-xyz", {}, plat::ProcessCaptureLimits{});
    EXPECT_EQ(r.failure, plat::ProcessCaptureFailure::StartFailed);
    EXPECT_EQ(r.exitCode, -1);
    EXPECT_FALSE(r.diagnostic.empty());
}

TEST_F(LauncherOnPathTest, BoundedCaptureThroughBareNameLauncher) {
    const auto r = plat::runProcessCaptureBounded(
        launcherName_, {"echo-args", "alpha", "beta gamma"}, plat::ProcessCaptureLimits{});
    EXPECT_EQ(r.failure, plat::ProcessCaptureFailure::None) << r.diagnostic;
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(r.stdoutOutput, "alpha\nbeta gamma\n");
}

TEST_F(LauncherOnPathTest, TimeoutRemainsTheFailureAfterLauncherTreeTerminates) {
    // A Windows batch launcher is a cmd.exe leader plus the real tool. Repeat
    // actual termination to exercise the job-accounting/process-signal race.
    for (int attempt = 0; attempt < 8; ++attempt) {
        SCOPED_TRACE(attempt);
        plat::ProcessCaptureLimits limits;
        limits.timeoutMs = 150;
        const auto result = plat::runProcessCaptureBounded(
            launcherName_, {"sleep", "30000"}, limits);
        EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::TimedOut) << result.diagnostic;
        EXPECT_EQ(result.exitCode, -1);
        EXPECT_NE(result.diagnostic.find("deadline"), std::string::npos);
    }
}

TEST_F(LauncherOnPathTest, OutputLimitRemainsTheFailureAfterLauncherTreeTerminates) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        SCOPED_TRACE(attempt);
        plat::ProcessCaptureLimits limits;
        limits.maxStderrBytes = 64;
        const auto result = plat::runProcessCaptureBounded(
            launcherName_, {"emit-bytes", "0", "16777216"}, limits);
        EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::OutputLimitExceeded)
            << result.diagnostic;
        EXPECT_EQ(result.exitCode, -1);
        EXPECT_EQ(result.stderrOutput, std::string(64, 'e'));
        EXPECT_NE(result.diagnostic.find("byte limit"), std::string::npos);
    }
}

class DescendantCaptureTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = plat::tempDirectory() / ("topo-descendant-test-" + std::to_string(currentPid()));
        fs::create_directories(dir_);
        ready_ = dir_ / "child.ready";
        sentinel_ = dir_ / "child.sentinel";
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    std::vector<std::string> args(const std::string& action, int delay = 1800) {
        return {"spawn-descendant", ready_.string(), sentinel_.string(), std::to_string(delay), action};
    }
    void checkStopped() {
        // The ready file proves that the descendant really ran before its
        // parent was stopped. Observe beyond its write deadline so a mere
        // successful kill call cannot masquerade as lifecycle containment.
        ASSERT_TRUE(fs::exists(ready_));
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        EXPECT_FALSE(fs::exists(sentinel_));
    }
    fs::path dir_, ready_, sentinel_;
};

TEST_F(DescendantCaptureTest, FixtureReallyWritesSentinelWithoutContainment) {
    const auto r = plat::runProcessCapture(helperPath(), args("exit", 50));
    ASSERT_EQ(r.exitCode, 0);
    ASSERT_TRUE(fs::exists(ready_));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!fs::exists(sentinel_) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(fs::exists(sentinel_));
}

TEST_F(DescendantCaptureTest, TimeoutStopsOrdinaryDescendant) {
    plat::ProcessCaptureLimits limits;
    limits.timeoutMs = 1000;
    const auto r = plat::runProcessCaptureBounded(helperPath(), args("wait"), limits);
    EXPECT_EQ(r.failure, plat::ProcessCaptureFailure::TimedOut) << r.diagnostic;
    EXPECT_EQ(r.exitCode, -1);
    EXPECT_EQ(r.stdoutOutput, "descendant-ready\n");
    checkStopped();
}

TEST_F(DescendantCaptureTest, ExitedLeaderStillStopsOrdinaryDescendant) {
    const auto r = plat::runProcessCaptureBounded(helperPath(), args("exit"), plat::ProcessCaptureLimits{});
    EXPECT_EQ(r.failure, plat::ProcessCaptureFailure::None) << r.diagnostic;
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(r.stdoutOutput, "descendant-ready\n");
    checkStopped();
}

TEST_F(DescendantCaptureTest, OutputLimitStopsOrdinaryDescendant) {
    plat::ProcessCaptureLimits limits;
    limits.maxStdoutBytes = 4096;
    const auto r = plat::runProcessCaptureBounded(helperPath(), args("flood"), limits);
    EXPECT_EQ(r.failure, plat::ProcessCaptureFailure::OutputLimitExceeded) << r.diagnostic;
    EXPECT_EQ(r.exitCode, -1);
    EXPECT_EQ(r.stdoutOutput.size(), 4096u);
    checkStopped();
}

TEST(ProcessBoundedCapture, FixedEnvironmentReplacesInheritedValuesAtSpawn) {
    constexpr const char* name = "TOPO_BOUNDED_ENVIRONMENT_TEST";
    const char* original = std::getenv(name);
    const std::string saved = original ? original : "";
#ifdef _WIN32
    _putenv_s(name, "ambient");
#else
    setenv(name, "ambient", 1);
#endif
    plat::ProcessCaptureLimits limits;
    const auto inherited = plat::runProcessCaptureBounded(helperPath(), {"environment", name}, limits);
    limits.environment = std::map<std::string, std::string>{{name, "fixed"}};
    const auto fixed = plat::runProcessCaptureBounded(helperPath(), {"environment", name}, limits);
    limits.environment = std::map<std::string, std::string>{};
    const auto cleared = plat::runProcessCaptureBounded(helperPath(), {"environment", name}, limits);
#ifdef _WIN32
    _putenv_s(name, original ? saved.c_str() : "");
#else
    if (original) setenv(name, saved.c_str(), 1); else unsetenv(name);
#endif
    EXPECT_EQ(inherited.stdoutOutput, "ambient\n");
    EXPECT_EQ(fixed.stdoutOutput, "fixed\n");
    EXPECT_EQ(cleared.exitCode, 7);
    EXPECT_TRUE(cleared.stdoutOutput.empty());
}

TEST(ProcessBoundedCapture, InvalidEnvironmentFailsBeforeSpawn) {
    for (const auto& entry : std::vector<std::pair<std::string, std::string>>{
            {"", "value"}, {"NAME=BAD", "value"}, {std::string("N\0X", 3), "value"},
            {"NAME", std::string("v\0x", 3)}}) {
        plat::ProcessCaptureLimits limits;
        limits.environment = std::map<std::string, std::string>{entry};
        const auto result = plat::runProcessCaptureBounded(helperPath(), {"echo-args", "executed"}, limits);
        EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::InvalidLimits);
        EXPECT_TRUE(result.stdoutOutput.empty());
    }
}
