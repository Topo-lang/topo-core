#include "topo/Platform/Platform.h"
#include "topo/Platform/Process.h"
#include "topo/Platform/TempFile.h"
#include "topo/Platform/ToolResolution.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
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
