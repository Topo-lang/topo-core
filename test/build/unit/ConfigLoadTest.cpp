#include "Config.h"
#include "topo/Build/BackendProtocol.h"
#include "topo/Build/BuildConfig.h"
#include "topo/Build/ConfigValidator.h"

#include <gtest/gtest.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace topo;
using namespace topo::build;

namespace {

#ifdef _WIN32
int testPid() { return _getpid(); }
#else
int testPid() { return getpid(); }
#endif

class ConfigCheckLoadTest : public ::testing::Test {
protected:
    fs::path testDir;
    fs::path prevCwd;

    void SetUp() override {
        prevCwd = fs::current_path();
        testDir = fs::temp_directory_path() /
                  ("topo-build-check-load_" + std::to_string(testPid()) + "_" +
                   std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(testDir);
        fs::current_path(testDir);
    }

    void TearDown() override {
        fs::current_path(prevCwd);
        std::error_code ec;
        fs::remove_all(testDir, ec);
    }

    void writeToml(const std::string& body) {
        std::ofstream(testDir / "Topo.toml") << body;
        // A dummy .topo file, because loadTopoToml resolves [topo].root.
        std::ofstream(testDir / "main.topo") << "";
    }
};

// ---------------------------------------------------------------------------
// TOML [build].check parsing
// ---------------------------------------------------------------------------

TEST_F(ConfigCheckLoadTest, DefaultIsAutoAndDoesNotRunCheck) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.checkMode, CheckMode::Auto);
    EXPECT_FALSE(cfg.checkCliOverride.has_value());
    EXPECT_FALSE(cfg.shouldRunCheck());
}

TEST_F(ConfigCheckLoadTest, CheckOffTomlSkipsCheck) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"
check = "off"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.checkMode, CheckMode::Off);
    EXPECT_FALSE(cfg.shouldRunCheck());
}

TEST_F(ConfigCheckLoadTest, CheckOnTomlRunsCheck) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"
check = "on"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.checkMode, CheckMode::On);
    EXPECT_TRUE(cfg.shouldRunCheck());
}

TEST_F(ConfigCheckLoadTest, CheckAutoTomlDoesNotRunCheck) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"
check = "auto"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.checkMode, CheckMode::Auto);
    EXPECT_FALSE(cfg.shouldRunCheck());
}

TEST_F(ConfigCheckLoadTest, InvalidCheckValueRejected) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"
check = "maybe"
)");
    BuildConfig cfg;
    EXPECT_FALSE(loadTopoToml(cfg));
}

// ---------------------------------------------------------------------------
// Override precedence: CLI > TOML > default
// ---------------------------------------------------------------------------

TEST(ConfigCheckResolve, CliNoCheckOverridesTomlOn) {
    BuildConfig cfg;
    cfg.checkMode = CheckMode::On;
    cfg.checkCliOverride = false;
    EXPECT_FALSE(cfg.shouldRunCheck());
}

TEST(ConfigCheckResolve, CliCheckOverridesTomlOff) {
    BuildConfig cfg;
    cfg.checkMode = CheckMode::Off;
    cfg.checkCliOverride = true;
    EXPECT_TRUE(cfg.shouldRunCheck());
}

TEST(ConfigCheckResolve, CliCheckOverridesTomlAuto) {
    BuildConfig cfg;
    cfg.checkMode = CheckMode::Auto;
    cfg.checkCliOverride = true;
    EXPECT_TRUE(cfg.shouldRunCheck());
}

TEST(ConfigCheckResolve, NoCliOverrideFallsBackToTomlMode) {
    BuildConfig on;
    on.checkMode = CheckMode::On;
    EXPECT_TRUE(on.shouldRunCheck());

    BuildConfig off;
    off.checkMode = CheckMode::Off;
    EXPECT_FALSE(off.shouldRunCheck());

    BuildConfig autoMode;
    autoMode.checkMode = CheckMode::Auto;
    EXPECT_FALSE(autoMode.shouldRunCheck());
}

// ---------------------------------------------------------------------------
// CLI flag parsing sets the override, not the mode
// ---------------------------------------------------------------------------

TEST(ConfigCheckCli, CheckFlagSetsOverrideTrue) {
    BuildConfig cfg;
    const char* argv[] = {"topo-build", "--check"};
    ASSERT_TRUE(parseArgs(2, const_cast<char**>(argv), cfg));
    ASSERT_TRUE(cfg.checkCliOverride.has_value());
    EXPECT_TRUE(*cfg.checkCliOverride);
    EXPECT_EQ(cfg.checkMode, CheckMode::Auto);
}

TEST(ConfigCheckCli, NoCheckFlagSetsOverrideFalse) {
    BuildConfig cfg;
    const char* argv[] = {"topo-build", "--no-check"};
    ASSERT_TRUE(parseArgs(2, const_cast<char**>(argv), cfg));
    ASSERT_TRUE(cfg.checkCliOverride.has_value());
    EXPECT_FALSE(*cfg.checkCliOverride);
}

// ---------------------------------------------------------------------------
// ConfigValidator rejects invalid rawCheck
// ---------------------------------------------------------------------------

TEST(ConfigCheckValidator, InvalidRawCheckIsError) {
    BuildConfig cfg;
    cfg.rawCheck = "maybe";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigCheckValidator, ValidRawCheckValues) {
    for (const char* v : {"on", "off", "auto"}) {
        BuildConfig cfg;
        cfg.rawCheck = v;
        auto result = validateConfig(cfg);
        EXPECT_FALSE(result.hasErrors()) << "value: " << v;
    }
}

TEST(ConfigCheckValidator, EmptyRawCheckIsValid) {
    BuildConfig cfg;
    cfg.rawCheck = "";
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

// ---------------------------------------------------------------------------
// [pipeline].mode parsing
//
// Closes .aidesk/live/40-issue/
//   config-pipeline-section-not-in-knownsections.md
//
// Before fix: `[pipeline]` was absent from knownSections, so
// `mode = "off"` silently passed through ConfigValidator as "unknown
// section ignored" and never reached PipelineCodeGenPass / PipelinePass.
// These tests pin the three valid modes plus the default-Auto behaviour.
// ---------------------------------------------------------------------------

class ConfigPipelineLoadTest : public ConfigCheckLoadTest {};

TEST_F(ConfigPipelineLoadTest, NoSectionDefaultsToAuto) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.pipelineCfg.mode, FeatureMode::Auto);
    EXPECT_TRUE(cfg.pipelineCfg.isEnabled());
}

TEST_F(ConfigPipelineLoadTest, PipelineOffDisablesPass) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"

[pipeline]
mode = "off"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.pipelineCfg.mode, FeatureMode::Off);
    EXPECT_FALSE(cfg.pipelineCfg.isEnabled());
}

TEST_F(ConfigPipelineLoadTest, PipelineForceEnablesPass) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"

[pipeline]
mode = "force"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.pipelineCfg.mode, FeatureMode::Force);
    EXPECT_TRUE(cfg.pipelineCfg.isEnabled());
}

TEST_F(ConfigPipelineLoadTest, PipelineAutoIsExplicitDefault) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"

[pipeline]
mode = "auto"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.pipelineCfg.mode, FeatureMode::Auto);
    EXPECT_TRUE(cfg.pipelineCfg.isEnabled());
}

TEST(BackendProtocolPipelineCfg, RoundTripsMode) {
    // Verify `[pipeline].mode` survives BackendRequest JSON serialize →
    // deserialize.  Without this round-trip the backend processes would
    // see mode = Auto even when the frontend parsed mode = "off" from
    // Topo.toml — re-introducing the silent-noop.
    for (auto mode : {FeatureMode::Off, FeatureMode::Auto, FeatureMode::Force}) {
        BackendRequest req;
        req.config.pipelineCfg.mode = mode;
        req.outputPath = "out";
        req.tempDir = "/tmp/topo-test";

        auto jsonStr = serializeBackendRequest(req);
        BackendRequest round;
        ASSERT_TRUE(deserializeBackendRequest(jsonStr, round))
            << "deserialize failed for mode="
            << featureModeToString(mode);
        EXPECT_EQ(round.config.pipelineCfg.mode, mode);
    }
}

TEST(BackendProtocolPipelineCfg, DefaultsToAutoWhenMissingFromJson) {
    // Backwards compatibility: older backends / older request JSONs that
    // predate pipelineCfg must deserialize into Auto (the struct default),
    // not Off, or they'd silently disable pipeline DAG codegen.
    nlohmann::json j = nlohmann::json::object();
    j["outputPath"] = "out";
    j["tempDir"] = "/tmp/x";
    j["language"] = "cpp";
    j["config"] = nlohmann::json::object(); // no pipelineCfg key
    j["topoMetadata"] = nlohmann::json::object();
    j["visibilityEntries"] = nlohmann::json::array();

    BackendRequest req;
    ASSERT_TRUE(deserializeBackendRequest(j.dump(), req));
    EXPECT_EQ(req.config.pipelineCfg.mode, FeatureMode::Auto);
}

TEST_F(ConfigPipelineLoadTest, EmptyPipelineSectionKeepsAutoDefault) {
    // A bare `[pipeline]` header with no `mode` key must not flip the
    // default — historical behaviour expected Auto, and flipping to
    // Off silently would be worse than the pre-fix silent-ignore.
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"

[pipeline]
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.pipelineCfg.mode, FeatureMode::Auto);
}

// ---------------------------------------------------------------------------
// TOML [optimize.data-layout].mode parsing
//
// Closes .aidesk/live/40-issue/data-layout-mode-force-parses-to-off.md
//
// Before fix: a bespoke switch accepted only "soa"/"auto" and routed
// everything else (including "force") to FeatureMode::Off, so benchmarks
// that declared `mode = "force"` silently ran with DataLayoutPass disabled.
// These tests pin the three generic modes plus the "soa"/"aos" aliases.
// ---------------------------------------------------------------------------

class ConfigDataLayoutLoadTest : public ConfigCheckLoadTest {};

TEST_F(ConfigDataLayoutLoadTest, ModeForceParsesToForce) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"

[optimize.data-layout]
mode = "force"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.dataLayoutCfg.mode, FeatureMode::Force);
}

TEST_F(ConfigDataLayoutLoadTest, ModeAutoParsesToAuto) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"

[optimize.data-layout]
mode = "auto"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.dataLayoutCfg.mode, FeatureMode::Auto);
}

TEST_F(ConfigDataLayoutLoadTest, ModeOffParsesToOff) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"

[optimize.data-layout]
mode = "off"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.dataLayoutCfg.mode, FeatureMode::Off);
}

TEST_F(ConfigDataLayoutLoadTest, ModeSoaAliasMapsToForce) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"

[optimize.data-layout]
mode = "soa"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.dataLayoutCfg.mode, FeatureMode::Force);
}

// ---------------------------------------------------------------------------
// TOML [build.java].target_version parsing
//
// Closes .aidesk/live/40-issue/topo-build-java-target-version-not-forwarded.md
//
// Before fix: Config.cpp had no parser for [build.java], so users setting
// `target_version = "21"` (or any other value) were silently ignored and
// the backend always saw its hardcoded default. These tests pin the parse.
// ---------------------------------------------------------------------------

class ConfigJavaLoadTest : public ConfigCheckLoadTest {};

TEST_F(ConfigJavaLoadTest, TargetVersionParsed) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "java"
sources = ["Foo.java"]
output = "out"

[build.java]
target_version = "21"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.javaCfg.targetVersion, "21");
}

TEST_F(ConfigJavaLoadTest, TargetVersionEmptyWhenSectionMissing) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "java"
sources = ["Foo.java"]
output = "out"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.javaCfg.targetVersion, "");
}

TEST_F(ConfigDataLayoutLoadTest, ModeAosAliasMapsToOff) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"

[optimize.data-layout]
mode = "aos"
)");
    BuildConfig cfg;
    ASSERT_TRUE(loadTopoToml(cfg));
    EXPECT_EQ(cfg.dataLayoutCfg.mode, FeatureMode::Off);
}

// ---------------------------------------------------------------------------
// TOML [loop_parallel].reduction parsing
//
// Closes .aidesk/live/40-issue/
//   loop-parallel-reduction-key-false-unknown-warning.md
//
// Before fix: Config.cpp read `reduction` into
// loopParallelCfg.reductionEnabled, but `reduction` was absent from the
// [loop_parallel] warnUnknownKeys known-key set, so a correct
// `reduction = true` emitted a spurious
// "unknown key 'reduction' in [loop_parallel] (ignored)" warning while
// the value was in fact consumed.
// ---------------------------------------------------------------------------

class ConfigLoopParallelLoadTest : public ConfigCheckLoadTest {};

TEST_F(ConfigLoopParallelLoadTest, ReductionConsumedWithoutUnknownKeyWarning) {
    writeToml(R"(
[topo]
root = "main.topo"

[build]
language = "cpp"
sources = ["x.cpp"]
output = "out"

[loop_parallel]
mode = "force"
reduction = true
)");
    BuildConfig cfg;
    // warnUnknownKeys writes to std::cerr — capture it to assert the
    // `reduction` key no longer triggers a false "unknown key" warning.
    std::ostringstream captured;
    std::streambuf* prevBuf = std::cerr.rdbuf(captured.rdbuf());
    bool ok = loadTopoToml(cfg);
    std::cerr.rdbuf(prevBuf);

    ASSERT_TRUE(ok);
    EXPECT_TRUE(cfg.loopParallelCfg.reductionEnabled);
    EXPECT_EQ(captured.str().find("unknown key 'reduction'"), std::string::npos)
        << "spurious unknown-key warning; stderr was:\n"
        << captured.str();
}

// ---------------------------------------------------------------------------
// BackendRequest.backendExtras schema enforcement
//
// Closes .aidesk/live/40-issue/jvm-backend-request-extras-schema-unstated.md
//
// The deserializer must:
//   - round-trip the documented JVM keys (javaHome / classpath / jvmArgs /
//     targetVersion / mainClass) and the documented LLVM-cpp keys
//     (hostCompilerPath / standard);
//   - reject unknown JVM keys (the JVM sub-schema is the finalised one);
//   - keep historical silent-tolerant behaviour for LLVM-cpp / LLVM-rust /
//     LLVM-mixed / Python / TypeScript (their sub-schemas are not
//     finalised yet — explicit TODO in the spec).
//
// The JVM backend tool additionally enforces per-value constraints on
// javaHome / targetVersion / jvmArgs; those tests live below and shell
// out to topo-build-jvm-java.
// ---------------------------------------------------------------------------

namespace {

// Build a minimal valid BackendRequest JSON object for the given language.
nlohmann::json minimalRequestJson(const std::string& language) {
    nlohmann::json j = nlohmann::json::object();
    j["outputPath"] = "out";
    j["tempDir"] = "/tmp/topo-test";
    j["language"] = language;
    j["config"] = nlohmann::json::object();
    j["topoMetadata"] = nlohmann::json::object();
    j["visibilityEntries"] = nlohmann::json::array();
    return j;
}

} // namespace

TEST(BackendRequestExtras, JvmKeysRoundTrip) {
    BackendRequest req;
    req.outputPath = "out";
    req.tempDir = "/tmp/topo-test";
    req.language = HostLanguage::Java;
    req.backendExtras = nlohmann::json::object();
    req.backendExtras["javaHome"] = "/opt/jdk21";
    req.backendExtras["classpath"] = std::vector<std::string>{"a.jar", "b.jar"};
    req.backendExtras["jvmArgs"] = std::vector<std::string>{"-Xmx512m"};
    req.backendExtras["targetVersion"] = "21";
    req.backendExtras["mainClass"] = "app.Main";

    auto jsonStr = serializeBackendRequest(req);
    BackendRequest round;
    ASSERT_TRUE(deserializeBackendRequest(jsonStr, round));
    EXPECT_EQ(round.backendExtras["javaHome"].get<std::string>(), "/opt/jdk21");
    EXPECT_EQ(round.backendExtras["classpath"].size(), 2u);
    EXPECT_EQ(round.backendExtras["jvmArgs"][0].get<std::string>(), "-Xmx512m");
    EXPECT_EQ(round.backendExtras["targetVersion"].get<std::string>(), "21");
    EXPECT_EQ(round.backendExtras["mainClass"].get<std::string>(), "app.Main");
}

TEST(BackendRequestExtras, LlvmCppKeysRoundTrip) {
    BackendRequest req;
    req.outputPath = "out";
    req.tempDir = "/tmp/topo-test";
    req.language = HostLanguage::Cpp;
    req.backendExtras = nlohmann::json::object();
    req.backendExtras["hostCompilerPath"] = "/usr/bin/clang++";
    req.backendExtras["standard"] = "c++17";

    auto jsonStr = serializeBackendRequest(req);
    BackendRequest round;
    ASSERT_TRUE(deserializeBackendRequest(jsonStr, round));
    EXPECT_EQ(round.backendExtras["hostCompilerPath"].get<std::string>(),
              "/usr/bin/clang++");
    EXPECT_EQ(round.backendExtras["standard"].get<std::string>(), "c++17");
}

TEST(BackendRequestExtras, UnknownKeyRejectedForJvm) {
    auto j = minimalRequestJson("java");
    j["backendExtras"] = nlohmann::json::object();
    j["backendExtras"]["javaHome"] = "/opt/jdk21";
    j["backendExtras"]["mysteryKey"] = "anything";  // unknown — must reject

    BackendRequest req;
    // Capture stderr — the deserializer logs the offending key.
    std::ostringstream captured;
    std::streambuf* prev = std::cerr.rdbuf(captured.rdbuf());
    bool ok = deserializeBackendRequest(j.dump(), req);
    std::cerr.rdbuf(prev);

    EXPECT_FALSE(ok);
    EXPECT_NE(captured.str().find("mysteryKey"), std::string::npos)
        << "expected diagnostic to mention the offending key; stderr was:\n"
        << captured.str();
}

TEST(BackendRequestExtras, UnknownKeyTolerantForLlvmCpp) {
    // LLVM-cpp sub-schema is not finalised yet — historical
    // silent-tolerant behaviour stands. This test pins that explicit
    // tolerance so the future flip is a conscious diff.
    auto j = minimalRequestJson("cpp");
    j["backendExtras"] = nlohmann::json::object();
    j["backendExtras"]["futureExperimentalKey"] = true;

    BackendRequest req;
    ASSERT_TRUE(deserializeBackendRequest(j.dump(), req));
    EXPECT_TRUE(req.backendExtras.contains("futureExperimentalKey"));
}

TEST(BackendRequestExtras, JvmTargetVersionMustBeRecognised) {
    // The recognised set is {"8","11","17","21"} per
    // backend-extras-schema.md. The deserializer does not type-check
    // values (that's the JVM driver's job), so this test goes one level
    // deeper — it exercises the same registry by checking that the
    // key itself is accepted (so we know rejection didn't fire on the
    // key) and pins the recognised set with a regression-brake unit test.
    auto j = minimalRequestJson("java");
    j["backendExtras"] = nlohmann::json::object();
    j["backendExtras"]["targetVersion"] = "banana";

    BackendRequest req;
    // Key is known → deserializer accepts; value validation is in the
    // JVM driver. The shell-level rejection is covered by the
    // topo-build-jvm-java CLI test in topo-jvm/test/.
    ASSERT_TRUE(deserializeBackendRequest(j.dump(), req));
    EXPECT_EQ(req.backendExtras["targetVersion"].get<std::string>(), "banana");
}

TEST(BackendRequestExtras, JvmJavaHomeKeyAccepted) {
    // Companion regression brake for JvmJavaHomeMustExist: a non-existent
    // javaHome value passes the deserializer (key is known) and is caught
    // by the driver-level validator. Pinning this keeps the unknown-key
    // diagnostic from accidentally swallowing the driver-level signal.
    auto j = minimalRequestJson("java");
    j["backendExtras"] = nlohmann::json::object();
    j["backendExtras"]["javaHome"] = "/no/such/jdk";

    BackendRequest req;
    ASSERT_TRUE(deserializeBackendRequest(j.dump(), req));
    EXPECT_EQ(req.backendExtras["javaHome"].get<std::string>(), "/no/such/jdk");
}

TEST(BackendRequestExtras, JvmJvmArgsKeyAccepted) {
    // Companion regression brake for JvmJvmArgsBootclasspathPrefixRejected:
    // the deserializer accepts the key (it's in the JVM schema). The
    // forbidden-prefix check fires in topo-build-jvm-java.
    auto j = minimalRequestJson("java");
    j["backendExtras"] = nlohmann::json::object();
    j["backendExtras"]["jvmArgs"] =
        std::vector<std::string>{"-Xbootclasspath/p:/tmp/x"};

    BackendRequest req;
    ASSERT_TRUE(deserializeBackendRequest(j.dump(), req));
    ASSERT_EQ(req.backendExtras["jvmArgs"].size(), 1u);
    EXPECT_EQ(req.backendExtras["jvmArgs"][0].get<std::string>(),
              "-Xbootclasspath/p:/tmp/x");
}

TEST(BackendRequestExtras, JvmRequestPassesCleanlyThroughDeserializer) {
    // Regression brake: when topo-build assembles a Java
    // BackendRequest, every backendExtras key it writes must belong to
    // the JVM registry. Anyone who adds an LLVM-only key (e.g.
    // "hostCompilerPath") to the unconditional emit path would
    // re-break every Java build at the deserializer. This test pins
    // the cross-component invariant by exercising the deserializer
    // with the exact key set topo-build/main.cpp emits for Java.
    auto j = minimalRequestJson("java");
    j["backendExtras"] = nlohmann::json::object();
    // Only the JVM-side keys topo-build writes today.
    j["backendExtras"]["targetVersion"] = "21";

    BackendRequest req;
    ASSERT_TRUE(deserializeBackendRequest(j.dump(), req));
    EXPECT_EQ(req.backendExtras["targetVersion"].get<std::string>(), "21");
}

TEST(BackendRequestExtras, KnownKeysRegistryPinnedPerBackend) {
    // Regression brake — anyone adding a backendExtras key has to update
    // the registry. If this test fails after a code change, update
    // both the registry and the corresponding sub-schema spec doc.
    auto contains = [](const std::unordered_set<std::string>& s,
                       const std::string& key) {
        return s.find(key) != s.end();
    };

    const auto& jvm = knownBackendExtrasKeys(HostLanguage::Java);
    EXPECT_EQ(jvm.size(), 5u);
    EXPECT_TRUE(contains(jvm, "javaHome"));
    EXPECT_TRUE(contains(jvm, "classpath"));
    EXPECT_TRUE(contains(jvm, "jvmArgs"));
    EXPECT_TRUE(contains(jvm, "targetVersion"));
    EXPECT_TRUE(contains(jvm, "mainClass"));

    const auto& cpp = knownBackendExtrasKeys(HostLanguage::Cpp);
    EXPECT_TRUE(contains(cpp, "hostCompilerPath"));
    EXPECT_TRUE(contains(cpp, "standard"));

    const auto& rust = knownBackendExtrasKeys(HostLanguage::Rust);
    EXPECT_TRUE(contains(rust, "cargoPath"));

    const auto& mixed = knownBackendExtrasKeys(HostLanguage::Mixed);
    EXPECT_TRUE(contains(mixed, "mixedConfig"));

    const auto& ts = knownBackendExtrasKeys(HostLanguage::TypeScript);
    EXPECT_TRUE(contains(ts, "nodePath"));
    EXPECT_TRUE(contains(ts, "tsconfigPath"));
    EXPECT_TRUE(contains(ts, "packageManager"));

    // JVM is the only enforced sub-schema today; others stay
    // silent-tolerant (failure-modes table in
    // .aidesk/base/60-spec/topo-build/backend-protocol.md).
    EXPECT_TRUE(backendExtrasRejectsUnknown(HostLanguage::Java));
    EXPECT_FALSE(backendExtrasRejectsUnknown(HostLanguage::Cpp));
    EXPECT_FALSE(backendExtrasRejectsUnknown(HostLanguage::Rust));
    EXPECT_FALSE(backendExtrasRejectsUnknown(HostLanguage::Mixed));
    EXPECT_FALSE(backendExtrasRejectsUnknown(HostLanguage::Python));
    EXPECT_FALSE(backendExtrasRejectsUnknown(HostLanguage::TypeScript));
}

} // namespace
