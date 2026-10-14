#include "topo/Platform/AnalysisProcess.h"
#include "topo/Platform/TempFile.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
namespace plat = topo::platform;
const std::string helper = TOPO_ANALYSIS_TEST_HELPER;

class AnalysisProcessTest : public ::testing::Test {
protected:
    fs::path directory;
    plat::AnalysisProcessPolicy policy;
    plat::ProcessCaptureLimits limits;
    void SetUp() override {
        plat::TempFile name("analysis-test");
        directory = name.path().string() + ".dir";
        fs::create_directories(directory);
        policy.allowedExecutables.emplace_back(helper);
        limits.timeoutMs = 5000;
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(directory, ec);
    }
    virtual plat::AnalysisIsolationCapability selectedCapability() {
        return plat::analysisProcessCapability();
    }
    virtual plat::AnalysisProcessResult invoke(const std::string& executable, const std::vector<std::string>& args) {
        return plat::runAnalysisProcess(executable, args, policy, limits);
    }
    plat::AnalysisProcessResult run(const std::vector<std::string>& args) {
        return invoke(helper, args);
    }
};

#ifndef __APPLE__
TEST_F(AnalysisProcessTest, UnsupportedPlatformNeverStartsTarget) {
    const auto sentinel = directory / "sentinel";
    const auto control = plat::runProcessCapture(helper, {"write", sentinel.string()});
    ASSERT_EQ(control.exitCode, 0) << control.stderrOutput;
    ASSERT_TRUE(fs::exists(sentinel));
    ASSERT_TRUE(fs::remove(sentinel));
    EXPECT_FALSE(plat::analysisProcessCapability().supported);
    const auto result = plat::runAnalysisProcess(helper, {"write", sentinel.string()}, policy, limits);
    EXPECT_FALSE(result.isolationEstablished);
    EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::StartFailed);
    EXPECT_FALSE(fs::exists(sentinel));
}
#endif

#if defined(__linux__) && !defined(TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS)
TEST_F(AnalysisProcessTest, ExperimentalLinuxBackendDisabledByDefault) {
    const auto sentinel = directory / "experimental-sentinel";
    EXPECT_FALSE(plat::experimental::linuxAnalysisProcessCapability().supported);
    const auto result = plat::experimental::runLinuxAnalysisProcess(helper, {"write", sentinel.string()}, policy, limits);
    EXPECT_FALSE(result.isolationEstablished);
    EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::StartFailed);
    EXPECT_FALSE(fs::exists(sentinel));
}
#endif

#if defined(__APPLE__) || (defined(__linux__) && defined(TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS))
#if defined(__linux__)
class ExperimentalLinuxAnalysisProcessTest : public AnalysisProcessTest {
protected:
    plat::AnalysisIsolationCapability selectedCapability() override {
        return plat::experimental::linuxAnalysisProcessCapability();
    }
    plat::AnalysisProcessResult invoke(const std::string& executable, const std::vector<std::string>& args) override {
        return plat::experimental::runLinuxAnalysisProcess(executable, args, policy, limits);
    }
};
#define TOPO_ISOLATION_TEST_SUITE ExperimentalLinuxAnalysisProcessTest
#else
#define TOPO_ISOLATION_TEST_SUITE AnalysisProcessTest
#endif

TEST_F(TOPO_ISOLATION_TEST_SUITE, OrdinaryToolFailureStillProvesIsolation) {
    const auto capability = selectedCapability();
    ASSERT_TRUE(capability.supported);
#ifdef __APPLE__
    ASSERT_EQ(capability.executionTools.size(), 2u);
    EXPECT_EQ(capability.executionTools[0], fs::canonical("/usr/bin/sandbox-exec"));
    EXPECT_EQ(capability.platformIdentity.find("seatbelt-analysis-v2:"), 0u);
#else
    ASSERT_FALSE(capability.executionTools.empty());
    EXPECT_LE(capability.executionTools.size(), 2u);
    EXPECT_EQ(capability.backend, "experimental-linux-landlock-seccomp");
    EXPECT_EQ(capability.platformIdentity.find("experimental-landlock-seccomp-resource-v1:"), 0u);
#endif
    const auto result = run({"exit"});
    ASSERT_TRUE(result.isolationEstablished) << result.stderrOutput << result.diagnostic;
    EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::None);
    EXPECT_EQ(result.exitCode, 17);
    EXPECT_TRUE(result.stdoutOutput.empty());
    EXPECT_EQ(result.executionTools, capability.executionTools);
    EXPECT_EQ(result.platformIdentity, capability.platformIdentity);
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, ExactInputFileReadAndOutsideDenial) {
    const auto allowed = directory / "allowed";
    const auto denied = directory / "outside";
    {
        std::ofstream(allowed) << "allowed bytes\n";
        std::ofstream(denied) << "outside bytes\n";
    }
    policy.readOnlyFiles.push_back(allowed);
    const auto good = run({"read", allowed.string()});
    ASSERT_TRUE(good.isolationEstablished) << good.stderrOutput;
    EXPECT_EQ(good.exitCode, 0);
    EXPECT_EQ(good.stdoutOutput, "allowed bytes\n");
    EXPECT_EQ(plat::runProcessCapture(helper, {"read", denied.string()}).exitCode, 0);
    const auto bad = run({"read", denied.string()});
    EXPECT_TRUE(bad.isolationEstablished);
    EXPECT_EQ(bad.exitCode, 13);
    const auto overwrite = run({"write", allowed.string()});
    EXPECT_EQ(overwrite.exitCode, 13);
    std::ifstream in(allowed);
    std::string bytes;
    std::getline(in, bytes);
    EXPECT_EQ(bytes, "allowed bytes");
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, DirectoryGrantDoesNotFollowSymlinkOutside) {
    const auto inputs = directory / "inputs";
    fs::create_directory(inputs);
    const auto outside = directory / "outside";
    std::ofstream(outside) << "secret\n";
    fs::create_symlink(outside, inputs / "link");
    policy.readOnlyDirectories.push_back(inputs);
    EXPECT_EQ(plat::runProcessCapture(helper, {"read", (inputs / "link").string()}).exitCode, 0);
    const auto result = run({"read", (inputs / "link").string()});
    EXPECT_TRUE(result.isolationEstablished);
    EXPECT_EQ(result.exitCode, 13);
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, PrivateTemporaryWritesAndCleanup) {
    auto result = run({"environment", "TMPDIR"});
    ASSERT_TRUE(result.isolationEstablished) << result.stderrOutput;
    ASSERT_EQ(result.exitCode, 0);
    const auto temporary = fs::path(result.stdoutOutput.substr(0, result.stdoutOutput.size() - 1));
#ifdef __APPLE__
    EXPECT_EQ(temporary.parent_path(), fs::path("/private/tmp"));
#else
    EXPECT_EQ(temporary.parent_path(), fs::path("/tmp"));
#endif
    EXPECT_FALSE(fs::exists(temporary));
    result = run({"write", std::string(plat::AnalysisTemporaryDirectoryToken) + "/owned"});
    EXPECT_EQ(result.exitCode, 0) << result.stderrOutput;
#ifdef __APPLE__
    result = run({"locked-temporary"});
    ASSERT_EQ(result.exitCode, 0) << result.diagnostic;
    EXPECT_FALSE(fs::exists(fs::path(result.stdoutOutput.substr(0, result.stdoutOutput.size() - 1))));
#endif
    const auto outside = directory / "sentinel";
    result = run({"write", outside.string()});
    EXPECT_EQ(result.exitCode, 13);
    EXPECT_FALSE(fs::exists(outside));
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, InheritedInjectionEnvironmentIsClearedAtSpawn) {
    const char* prior = std::getenv("JAVA_TOOL_OPTIONS");
    const std::string saved = prior ? prior : "";
    setenv("JAVA_TOOL_OPTIONS", "-Dtopo.injected=true", 1);
    const auto result = run({"environment", "JAVA_TOOL_OPTIONS"});
    if (prior)
        setenv("JAVA_TOOL_OPTIONS", saved.c_str(), 1);
    else
        unsetenv("JAVA_TOOL_OPTIONS");
    EXPECT_TRUE(result.isolationEstablished) << result.stderrOutput;
    EXPECT_EQ(result.exitCode, 7);
    policy.environment["TOPO_FIXED_VALUE"] = "authorized";
    EXPECT_EQ(run({"environment", "TOPO_FIXED_VALUE"}).stdoutOutput, "authorized\n");
}

#ifdef __APPLE__
TEST_F(TOPO_ISOLATION_TEST_SUITE, NumericSysctlCannotReadOutsideParentStartupEnvironment) {
    plat::ProcessCaptureLimits parentLimits;
    parentLimits.environment =
        std::map<std::string, std::string>{{"TOPO_ANALYSIS_PARENT_SECRET", "controlled-sysctl-value"}};
    const auto control = plat::runProcessCaptureBounded(helper, {"sysctl-parent", "control"}, parentLimits);
    ASSERT_EQ(control.exitCode, 0) << control.diagnostic << control.stderrOutput;
    EXPECT_EQ(control.stdoutOutput, "parent sentinel=yes child-env=absent\n");
    const auto isolated = plat::runProcessCaptureBounded(helper, {"sysctl-parent", "isolated"}, parentLimits);
    EXPECT_EQ(isolated.exitCode, 13) << isolated.diagnostic << isolated.stderrOutput;
    EXPECT_EQ(isolated.stdoutOutput, "sysctl denied errno=1\n");
    const auto osBuild = run({"sysctl-os-build"});
    EXPECT_EQ(osBuild.exitCode, 0);
    EXPECT_TRUE(osBuild.isolationEstablished);
    EXPECT_FALSE(osBuild.stdoutOutput.empty());
}

#endif

TEST_F(TOPO_ISOLATION_TEST_SUITE, UnlistedInheritedFileDescriptorIsClosedBeforeExec) {
    const auto secret = directory / "secret";
    std::ofstream(secret) << "inherited secret\n";
    const int fd = ::open(secret.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
    const auto result = run({"read-fd", std::to_string(fd)});
    ::close(fd);
    EXPECT_TRUE(result.isolationEstablished) << result.stderrOutput;
    EXPECT_EQ(result.exitCode, 13);
    EXPECT_EQ(result.stdoutOutput, "errno=9\n");
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, UnlistedInheritedWritableDescriptorCannotModifyOutsideFile) {
    const auto outside = directory / "outside-writable";
    std::ofstream(outside) << "unchanged bytes\n";
    const int fd = ::open(outside.c_str(), O_WRONLY);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
    constexpr char original[] = "unchanged bytes\n";
    ASSERT_EQ(::write(fd, original, sizeof(original) - 1), static_cast<ssize_t>(sizeof(original) - 1));
    const auto result = run({"write-fd", std::to_string(fd)});
    ::close(fd);
    ASSERT_TRUE(result.isolationEstablished) << result.diagnostic << result.stderrOutput;
    EXPECT_EQ(result.exitCode, 13);
    EXPECT_EQ(result.stdoutOutput, "errno=9\n");
    std::ifstream input(outside);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), "unchanged bytes\n");
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, LoopbackConnectionIsActuallyDenied) {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listener, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    ASSERT_EQ(::listen(listener, 4), 0);
    socklen_t size = sizeof(address);
    ASSERT_EQ(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size), 0);
    const auto port = std::to_string(ntohs(address.sin_port));
    const auto control = plat::runProcessCapture(helper, {"connect", port});
    EXPECT_EQ(control.exitCode, 0) << control.stdoutOutput;
    const auto result = run({"connect", port});
    EXPECT_TRUE(result.isolationEstablished);
    EXPECT_EQ(result.exitCode, 13);
    EXPECT_NE(result.stdoutOutput.find("errno=1"), std::string::npos);
    ::close(listener);
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, ForkAndProcessGroupEscapeAreDeniedWithWorkingControls) {
    for (const auto* mode : {"fork", "spawn", "setpgid", "setsid"}) {
        const auto control = plat::runProcessCapture(helper, {mode});
        EXPECT_EQ(control.exitCode, 0) << mode << control.stderrOutput;
        const auto result = run({mode});
        EXPECT_TRUE(result.isolationEstablished);
        EXPECT_EQ(result.exitCode, 13) << mode << result.stdoutOutput;
    }
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, NativeExecOutsideExactAllowlistIsDenied) {
    const auto other = directory / "other-helper";
    fs::copy_file(helper, other);
    const auto sentinel = directory / "sentinel";
    EXPECT_EQ(plat::runProcessCapture(helper, {"exec", other.string(), sentinel.string()}).exitCode, 0);
    EXPECT_TRUE(fs::exists(sentinel));
    fs::remove(sentinel);
    policy.readOnlyDirectories.push_back(directory);
    const auto result = run({"exec", other.string(), sentinel.string()});
    EXPECT_TRUE(result.isolationEstablished);
    EXPECT_EQ(result.exitCode, 13);
    EXPECT_FALSE(fs::exists(sentinel));
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, ScriptCannotBecomeAnAllowedExecutable) {
    const auto script = directory / "script";
    std::ofstream(script) << "#!/bin/sh\ntouch '" << (directory / "sentinel").string() << "'\n";
    fs::permissions(script, fs::perms::owner_all);
    policy.allowedExecutables.push_back(script);
    const auto result = invoke(script.string(), {});
    EXPECT_FALSE(result.isolationEstablished);
    EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::StartFailed);
    EXPECT_FALSE(fs::exists(directory / "sentinel"));
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, TimeoutAndOutputCapsRemainBoundedAfterHandshake) {
    limits.timeoutMs = 100;
    auto result = run({"sleep"});
    EXPECT_TRUE(result.isolationEstablished);
    EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::TimedOut);
    limits.timeoutMs = 5000;
    limits.maxStdoutBytes = 333;
    result = run({"flood"});
    EXPECT_TRUE(result.isolationEstablished);
    EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::OutputLimitExceeded);
    EXPECT_EQ(result.stdoutOutput.size(), 333u);
    limits.maxStdoutBytes = 0;
    result = run({"exit"});
    EXPECT_TRUE(result.isolationEstablished);
    EXPECT_EQ(result.exitCode, 17);
    EXPECT_TRUE(result.stdoutOutput.empty());
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, ThreadsAndInProcessCodeGenerationRemainAvailable) {
    const auto thread = run({"thread"});
    EXPECT_TRUE(thread.isolationEstablished);
    EXPECT_EQ(thread.exitCode, 0);
    EXPECT_EQ(thread.stdoutOutput, "23\n");
    const auto jit = run({"jit"});
    EXPECT_TRUE(jit.isolationEstablished);
    EXPECT_EQ(jit.exitCode, 0) << jit.stdoutOutput << jit.stderrOutput;
    const auto executed = run({"jit-execute"});
    EXPECT_EQ(executed.exitCode, 0);
    EXPECT_EQ(executed.stdoutOutput, "executed=42\n");
}
#ifdef __linux__
TEST_F(TOPO_ISOLATION_TEST_SUITE, ProcCannotReadOutsideParentStartupEnvironment) {
    plat::ProcessCaptureLimits parentLimits;
    parentLimits.environment =
        std::map<std::string, std::string>{{"TOPO_ANALYSIS_PARENT_SECRET", "controlled-proc-value"}};
    const auto control = plat::runProcessCaptureBounded(helper, {"proc-parent", "control"}, parentLimits);
    ASSERT_EQ(control.exitCode, 0) << control.diagnostic << control.stderrOutput;
    EXPECT_EQ(control.stdoutOutput, "parent sentinel=yes child-env=absent\n");
    const auto isolated = plat::runProcessCaptureBounded(helper, {"proc-parent", "isolated"}, parentLimits);
    EXPECT_EQ(isolated.exitCode, 13) << isolated.diagnostic << isolated.stderrOutput;
    EXPECT_NE(isolated.stdoutOutput.find("proc denied"), std::string::npos);
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, ReadOnlyInputsRejectMetadataChangesAndTruncation) {
    const auto file = directory / "read-only-input";
    std::ofstream(file) << "unchanged bytes\n";
    policy.readOnlyFiles.push_back(file);
    for (const char* action : {"chmod", "utimens", "truncate"}) {
        const auto control = plat::runProcessCapture(helper, {action, file.string()});
        ASSERT_EQ(control.exitCode, 0) << action << control.stderrOutput;
        fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write);
        std::ofstream(file) << "unchanged bytes\n";
        const auto timestamp = fs::last_write_time(file);
        const auto result = run({action, file.string()});
        ASSERT_TRUE(result.isolationEstablished) << result.diagnostic << result.stderrOutput;
        EXPECT_EQ(result.exitCode, 13) << action;
        EXPECT_EQ(fs::status(file).permissions(), fs::perms::owner_read | fs::perms::owner_write);
        EXPECT_EQ(fs::last_write_time(file), timestamp);
        std::ifstream input(file);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), "unchanged bytes\n");
    }
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, RawCloneAndProcessInterfacesAreDeniedWithWorkingControls) {
    for (const char* action : {"clone-process", "process-memory", "signal"}) {
        const auto pid = std::to_string(::getpid());
        const auto control = plat::runProcessCapture(helper, {action, pid});
        ASSERT_EQ(control.exitCode, 0) << action << control.stderrOutput;
        const auto result = run({action, pid});
        ASSERT_TRUE(result.isolationEstablished) << result.diagnostic << result.stderrOutput;
        EXPECT_EQ(result.exitCode, 13) << action;
    }
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, InvalidGrantRejectsBeforeTargetAndHandshake) {
    const auto sentinel = directory / "sentinel";
    policy.readOnlyFiles.push_back(directory / "missing");
    const auto result = run({"write", sentinel.string()});
    EXPECT_FALSE(result.isolationEstablished);
    EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::StartFailed);
    EXPECT_FALSE(fs::exists(sentinel));
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, LauncherRejectsFailedPolicyBeforeHandshakeAndTarget) {
    const auto capability = selectedCapability();
    ASSERT_TRUE(capability.supported) << capability.diagnostic;
    ASSERT_FALSE(capability.executionTools.empty());
    const auto sentinel = directory / "sentinel";
    const auto result = plat::runProcessCaptureBounded(capability.executionTools.front().string(),
        {"--read-file", (directory / "missing").string(), "--", helper, "write", sentinel.string()}, limits);
    EXPECT_NE(result.exitCode, 0);
    EXPECT_TRUE(result.stdoutOutput.empty());
    EXPECT_FALSE(fs::exists(sentinel));
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, ForeignElfCannotEnterNativeExecutableAllowlist) {
    const auto foreign = directory / "foreign";
    fs::copy_file(helper, foreign);
    std::fstream executable(foreign, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(executable);
    executable.seekp(18); // ELF64 e_machine.
#if defined(__x86_64__)
    const char machine[] = {static_cast<char>(183), 0};
#else
    const char machine[] = {62, 0};
#endif
    executable.write(machine, 2);
    executable.close();
    policy.allowedExecutables.push_back(foreign);
    const auto sentinel = directory / "sentinel";
    const auto result = invoke(foreign.string(), {"write", sentinel.string()});
    EXPECT_FALSE(result.isolationEstablished);
    EXPECT_EQ(result.failure, plat::ProcessCaptureFailure::StartFailed);
    EXPECT_FALSE(fs::exists(sentinel));
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, RuntimeDirectoryReadPermissionDoesNotGrantExec) {
    const auto other = directory / "other-helper";
    fs::copy_file(helper, other);
    policy.toolchainDirectories.push_back(directory);
    const auto readable = run({"read", other.string()});
    ASSERT_TRUE(readable.isolationEstablished) << readable.diagnostic << readable.stderrOutput;
    EXPECT_EQ(readable.exitCode, 0);
    const auto sentinel = directory / "sentinel";
    const auto result = run({"exec", other.string(), sentinel.string()});
    ASSERT_TRUE(result.isolationEstablished) << result.diagnostic << result.stderrOutput;
    EXPECT_EQ(result.exitCode, 13);
    EXPECT_FALSE(fs::exists(sentinel));
}

TEST_F(TOPO_ISOLATION_TEST_SUITE, ReadableImagesCanRunThroughBoundElfLoader) {
    // This is an explicit Linux capability limit, not a passing proof of
    // executable-image confinement. Dynamic ELF startup must grant its loader
    // execute access, and that loader can itself map a readable image.
    const auto capability = selectedCapability();
    ASSERT_TRUE(capability.supported) << capability.diagnostic;
    ASSERT_EQ(capability.executionTools.size(), 2u) << "this fixture requires a dynamic ELF launcher";
    const auto other = directory / "unlisted-helper";
    fs::copy_file(helper, other);
    const auto controlFile = directory / "control";
    const auto control = plat::runProcessCapture(helper,
        {"exec-loader", capability.executionTools[1].string(), other.string(), controlFile.string()});
    ASSERT_EQ(control.exitCode, 0) << control.stderrOutput;
    std::ifstream controlInput(controlFile);
    ASSERT_EQ(std::string(std::istreambuf_iterator<char>(controlInput), {}), "loader image executed\n");
    policy.readOnlyFiles.push_back(other);
    const auto result = run({"exec-loader", capability.executionTools[1].string(), other.string(),
                             std::string(plat::AnalysisTemporaryDirectoryToken) + "/loader-marker"});
    ASSERT_TRUE(result.isolationEstablished) << result.diagnostic << result.stderrOutput;
    EXPECT_EQ(result.exitCode, 0) << result.stderrOutput;
    EXPECT_EQ(result.stdoutOutput, "loader image executed\n");
}
#endif
#undef TOPO_ISOLATION_TEST_SUITE
#endif
} // namespace
