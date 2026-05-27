#include "topo/Build/ConfigValidator.h"
#include "topo/Build/BuildConfig.h"

#include <gtest/gtest.h>

using namespace topo;

namespace {

build::BuildConfig makeDefault() {
    build::BuildConfig cfg;
    return cfg;
}

// =====================================================================
// [parallel] validation
// =====================================================================

TEST(ConfigValidator, ParallelDisabledIsValid) {
    auto cfg = makeDefault();
    cfg.parallelCfg.mode = FeatureMode::Off;
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

TEST(ConfigValidator, ParallelEnabledValid) {
    auto cfg = makeDefault();
    cfg.parallelCfg.mode = FeatureMode::Auto;
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

// Removed: ParallelMinTasksZeroIsError. Plan 34 Phase ii.8 removed
// `min_tasks_to_parallelize` from ParallelConfig — Topo passes do not
// gate on workload-side cost heuristics, so the validator no longer
// guards against zero/negative values.

TEST(ConfigValidator, InstrumentWithoutParallelWarns) {
    auto cfg = makeDefault();
    cfg.parallelCfg.instrument = true;
    cfg.parallelCfg.mode = FeatureMode::Off;
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
    EXPECT_TRUE(result.hasWarnings());
}

// =====================================================================
// [adaptive] validation
// =====================================================================

TEST(ConfigValidator, AdaptiveRequiresEmbedIR) {
    auto cfg = makeDefault();
    cfg.adaptiveCfg.mode = FeatureMode::Auto;
    cfg.embedIR = false;
    cfg.parallelCfg.instrument = true;
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, AdaptiveRequiresInstrument) {
    auto cfg = makeDefault();
    cfg.adaptiveCfg.mode = FeatureMode::Auto;
    cfg.embedIR = true;
    cfg.parallelCfg.instrument = false;
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, AdaptiveMinTriggerZeroIsError) {
    auto cfg = makeDefault();
    cfg.adaptiveCfg.mode = FeatureMode::Auto;
    cfg.embedIR = true;
    cfg.parallelCfg.instrument = true;
    cfg.adaptiveCfg.min_trigger_ns = 0;
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, AdaptiveValidConfig) {
    auto cfg = makeDefault();
    cfg.adaptiveCfg.mode = FeatureMode::Auto;
    cfg.embedIR = true;
    cfg.parallelCfg.instrument = true;
    cfg.adaptiveCfg.min_trigger_ns = 10000;
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

// =====================================================================
// [optimize.data-layout] validation
// =====================================================================

TEST(ConfigValidator, DataLayoutOffIsValid) {
    auto cfg = makeDefault();
    cfg.dataLayoutCfg.mode = FeatureMode::Off;
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

// Removed: DataLayoutForceZeroArraySizeIsError. Plan 34 Phase ii.8 removed
// `min_array_size` from DataLayoutConfig — Topo passes do not gate on
// workload-side cost heuristics, so the validator no longer guards
// against zero/negative values.

// =====================================================================
// [optimize.indirection] validation
// =====================================================================

TEST(ConfigValidator, IndirectionAllSubSwitchesOffWarns) {
    auto cfg = makeDefault();
    cfg.indirectionCfg.mode = FeatureMode::Auto;
    cfg.indirectionCfg.uniquePtrPromotion = false;
    cfg.indirectionCfg.sharedPtrExclusive = false;
    cfg.indirectionCfg.vectorSpanLowering = false;
    cfg.indirectionCfg.pointerAttrInference = false;
    cfg.indirectionCfg.devirtualize = false;
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
    EXPECT_TRUE(result.hasWarnings());
}

// =====================================================================
// [observability] validation — always validated regardless of enabled
// =====================================================================

TEST(ConfigValidator, ObservabilityInvalidExporterIsError) {
    auto cfg = makeDefault();
    cfg.observabilityCfg.exporter = "invalid";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, ObservabilityInvalidExporterWhenDisabled) {
    auto cfg = makeDefault();
    cfg.observabilityCfg.mode = FeatureMode::Off;
    cfg.observabilityCfg.exporter = "invalid";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, ObservabilitySamplingRateOutOfRange) {
    auto cfg = makeDefault();
    cfg.observabilityCfg.samplingRate = 1.5;
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, ObservabilitySamplingRateNegative) {
    auto cfg = makeDefault();
    cfg.observabilityCfg.samplingRate = -0.1;
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, ObservabilityValidConfig) {
    auto cfg = makeDefault();
    cfg.observabilityCfg.mode = FeatureMode::Auto;
    cfg.observabilityCfg.exporter = "stdout";
    cfg.observabilityCfg.samplingRate = 0.5;
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

TEST(ConfigValidator, ObservabilityJsonExporterIsError) {
    // `json` and `otlp` used to be accepted; the runtime never implemented them.
    // ConfigValidator now rejects anything other than `stdout`.
    auto cfg = makeDefault();
    cfg.observabilityCfg.mode = FeatureMode::Auto;
    cfg.observabilityCfg.exporter = "json";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, ObservabilityOtlpExporterIsError) {
    auto cfg = makeDefault();
    cfg.observabilityCfg.mode = FeatureMode::Auto;
    cfg.observabilityCfg.exporter = "otlp";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

// =====================================================================
// [lifetime] validation
// =====================================================================

TEST(ConfigValidator, LifetimeZeroArenaSizeIsError) {
    auto cfg = makeDefault();
    cfg.lifetimeCfg.mode = FeatureMode::Auto;
    cfg.lifetimeCfg.defaultArenaSize = 0;
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

// =====================================================================
// [loop_parallel] validation
// =====================================================================

TEST(ConfigValidator, LoopParallelWithoutParallelWarns) {
    auto cfg = makeDefault();
    cfg.loopParallelCfg.mode = FeatureMode::Auto;
    cfg.parallelCfg.mode = FeatureMode::Off;
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
    EXPECT_TRUE(result.hasWarnings());
}

// =====================================================================
// Enum validation — unknown values are now errors
// =====================================================================

TEST(ConfigValidator, UnknownLanguageIsError) {
    auto cfg = makeDefault();
    cfg.rawLanguage = "kotlin";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, ValidLanguageCpp) {
    auto cfg = makeDefault();
    cfg.rawLanguage = "cpp";
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

TEST(ConfigValidator, ValidLanguageRust) {
    auto cfg = makeDefault();
    cfg.rawLanguage = "rust";
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

TEST(ConfigValidator, ValidLanguagePython) {
    auto cfg = makeDefault();
    cfg.rawLanguage = "python";
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

TEST(ConfigValidator, UnknownOutputTypeIsError) {
    auto cfg = makeDefault();
    cfg.rawOutputType = "dll";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, ValidOutputTypes) {
    for (const auto& type : {"exe", "shared", "static"}) {
        auto cfg = makeDefault();
        cfg.rawOutputType = type;
        auto result = validateConfig(cfg);
        EXPECT_FALSE(result.hasErrors()) << "Failed for: " << type;
    }
}

TEST(ConfigValidator, UnknownBuilderModeIsError) {
    auto cfg = makeDefault();
    cfg.rawBuilderMode = "release";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

// =====================================================================
// [builder].obfuscation validation
// =====================================================================

TEST(ConfigValidator, UnknownObfuscationModeIsError) {
    auto cfg = makeDefault();
    cfg.rawObfuscation = "scramble";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, ValidObfuscationModes) {
    for (const auto& mode : {"normal", "salted"}) {
        auto cfg = makeDefault();
        cfg.rawObfuscation = mode;
        auto result = validateConfig(cfg);
        EXPECT_FALSE(result.hasErrors()) << "Failed for: " << mode;
    }
}

TEST(ConfigValidator, SaltedObfuscationWithoutSaltNoWarning) {
    // Salt is now auto-generated by Config.cpp when empty,
    // so ConfigValidator no longer warns about it.
    auto cfg = makeDefault();
    cfg.obfMode = ObfuscationMode::Salted;
    cfg.obfSalt = "";
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasWarnings());
}

// =====================================================================
// Cross-feature: embed_ir + topo-jit in link_libs
// =====================================================================

TEST(ConfigValidator, JitInLinkLibsWithoutEmbedIRWarns) {
    auto cfg = makeDefault();
    cfg.linkLibs.push_back("topo-jit");
    cfg.embedIR = false;
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasWarnings());
}

TEST(ConfigValidator, JitInLinkLibsWithEmbedIRNoWarning) {
    auto cfg = makeDefault();
    cfg.linkLibs.push_back("topo-jit");
    cfg.embedIR = true;
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasWarnings());
}

// =====================================================================
// [build] mixed language validation
// =====================================================================

TEST(ConfigValidator, ValidLanguageMixed) {
    auto cfg = makeDefault();
    cfg.rawLanguage = "mixed";
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

TEST(ConfigValidator, MixedLanguageRequiresCppSources) {
    auto cfg = makeDefault();
    cfg.language = HostLanguage::Mixed;
    cfg.mixedCfg.cppSources = {};
    cfg.mixedCfg.rustManifest = "Cargo.toml";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, MixedLanguageRequiresRustManifest) {
    auto cfg = makeDefault();
    cfg.language = HostLanguage::Mixed;
    cfg.mixedCfg.cppSources = {"src/main.cpp"};
    cfg.mixedCfg.rustManifest = "";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, MixedLanguageValidConfig) {
    auto cfg = makeDefault();
    cfg.language = HostLanguage::Mixed;
    cfg.mixedCfg.cppSources = {"src/main.cpp"};
    cfg.mixedCfg.rustManifest = "Cargo.toml";
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

TEST(ConfigValidator, MixedLanguageCppOnlyIsError) {
    auto cfg = makeDefault();
    cfg.language = HostLanguage::Mixed;
    cfg.mixedCfg.cppSources = {"src/main.cpp"};
    cfg.mixedCfg.rustManifest = "";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

TEST(ConfigValidator, MixedLanguageRustOnlyIsError) {
    auto cfg = makeDefault();
    cfg.language = HostLanguage::Mixed;
    cfg.mixedCfg.cppSources = {};
    cfg.mixedCfg.rustManifest = "Cargo.toml";
    auto result = validateConfig(cfg);
    EXPECT_TRUE(result.hasErrors());
}

// =====================================================================
// Empty raw strings should not trigger validation (not set in TOML)
// =====================================================================

TEST(ConfigValidator, EmptyRawStringsAreValid) {
    auto cfg = makeDefault();
    cfg.rawLanguage = "";
    cfg.rawOutputType = "";
    cfg.rawBuilderMode = "";
    cfg.rawObfuscation = "";
    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

// =====================================================================
// Profile: built-in profiles
// =====================================================================

TEST(ConfigValidator, BuiltinProfilesExist) {
    const auto& profiles = build::builtinProfiles();
    EXPECT_TRUE(profiles.count("embedded"));
    EXPECT_TRUE(profiles.count("server"));
    EXPECT_TRUE(profiles.count("wasm"));
    EXPECT_TRUE(profiles.count("desktop"));
}

TEST(ConfigValidator, ServerProfileEnablesAllFeatures) {
    const auto& profiles = build::builtinProfiles();
    const auto& server = profiles.at("server");
    EXPECT_EQ(*server.buildMode, BuildMode::Aggressive);
    EXPECT_EQ(*server.parallel, FeatureMode::Auto);
    EXPECT_EQ(*server.adaptive, FeatureMode::Auto);
    EXPECT_EQ(*server.dataLayout, FeatureMode::Auto);
    EXPECT_EQ(*server.indirection, FeatureMode::Auto);
    EXPECT_EQ(*server.observability, FeatureMode::Auto);
    EXPECT_EQ(*server.lifetime, FeatureMode::Auto);
    EXPECT_EQ(*server.loopParallel, FeatureMode::Auto);
    EXPECT_TRUE(*server.embedIR);
    EXPECT_TRUE(*server.parallelInstrument);
}

TEST(ConfigValidator, EmbeddedProfileDisablesParallel) {
    const auto& profiles = build::builtinProfiles();
    const auto& embedded = profiles.at("embedded");
    EXPECT_EQ(*embedded.parallel, FeatureMode::Off);
    EXPECT_EQ(*embedded.adaptive, FeatureMode::Off);
    EXPECT_EQ(*embedded.indirection, FeatureMode::Force);
}

TEST(ConfigValidator, WasmProfileSimilarToEmbedded) {
    const auto& profiles = build::builtinProfiles();
    const auto& wasm = profiles.at("wasm");
    EXPECT_EQ(*wasm.parallel, FeatureMode::Off);
    EXPECT_EQ(*wasm.adaptive, FeatureMode::Off);
    EXPECT_EQ(*wasm.indirection, FeatureMode::Force);
}

TEST(ConfigValidator, DesktopProfileModerateSettings) {
    const auto& profiles = build::builtinProfiles();
    const auto& desktop = profiles.at("desktop");
    EXPECT_EQ(*desktop.buildMode, BuildMode::Dev);
    EXPECT_EQ(*desktop.parallel, FeatureMode::Auto);
    EXPECT_EQ(*desktop.adaptive, FeatureMode::Off);
}

// =====================================================================
// Profile: ProfileOverrides apply correctly
// =====================================================================

TEST(ConfigValidator, ProfileOverridesApplyToConfig) {
    auto cfg = makeDefault();
    cfg.selectedProfile = "embedded";
    // Simulate what resolveAndApplyProfile does (tested via loadTopoToml integration)
    const auto& profiles = build::builtinProfiles();
    const auto& p = profiles.at("embedded");
    if (p.buildMode) cfg.buildMode = *p.buildMode;
    if (p.parallel) cfg.parallelCfg.mode = *p.parallel;
    if (p.indirection) cfg.indirectionCfg.mode = *p.indirection;

    EXPECT_EQ(cfg.buildMode, BuildMode::Dev);
    EXPECT_EQ(cfg.parallelCfg.mode, FeatureMode::Off);
    EXPECT_EQ(cfg.indirectionCfg.mode, FeatureMode::Force);
}

// =====================================================================
// Profile: inheritance via extends
// =====================================================================

TEST(ConfigValidator, ProfileExtendsInheritsFields) {
    build::ProfileOverrides base;
    base.buildMode = BuildMode::Aggressive;
    base.parallel = FeatureMode::Auto;
    base.adaptive = FeatureMode::Auto;
    base.embedIR = true;
    base.parallelInstrument = true;

    build::ProfileOverrides derived;
    derived.extends = "base";
    derived.adaptive = FeatureMode::Off; // override just this

    auto cfg = makeDefault();
    // Manually apply base then derived (same logic as resolveAndApplyProfile)
    auto apply = [&](const build::ProfileOverrides& p) {
        if (p.buildMode) cfg.buildMode = *p.buildMode;
        if (p.parallel) cfg.parallelCfg.mode = *p.parallel;
        if (p.adaptive) cfg.adaptiveCfg.mode = *p.adaptive;
        if (p.embedIR) cfg.embedIR = *p.embedIR;
        if (p.parallelInstrument) cfg.parallelCfg.instrument = *p.parallelInstrument;
    };
    apply(base);
    apply(derived);

    EXPECT_EQ(cfg.buildMode, BuildMode::Aggressive);    // from base
    EXPECT_EQ(cfg.parallelCfg.mode, FeatureMode::Auto); // from base
    EXPECT_EQ(cfg.adaptiveCfg.mode, FeatureMode::Off);  // overridden by derived
    EXPECT_TRUE(cfg.embedIR);                           // from base
}

// =====================================================================
// Profile: unset fields remain at default
// =====================================================================

TEST(ConfigValidator, ProfileUnsetFieldsKeepDefaults) {
    auto cfg = makeDefault();
    // Desktop profile doesn't set lifetime or observability explicitly but sets them Off
    const auto& profiles = build::builtinProfiles();
    const auto& desktop = profiles.at("desktop");

    if (desktop.lifetime) cfg.lifetimeCfg.mode = *desktop.lifetime;
    EXPECT_EQ(cfg.lifetimeCfg.mode, FeatureMode::Off);

    // defaultArenaSize should be untouched
    EXPECT_EQ(cfg.lifetimeCfg.defaultArenaSize, 4096u);
}

// =====================================================================
// Profile: validation still runs after profile application
// =====================================================================

TEST(ConfigValidator, ServerProfilePassesValidation) {
    auto cfg = makeDefault();
    const auto& profiles = build::builtinProfiles();
    const auto& p = profiles.at("server");
    if (p.buildMode) cfg.buildMode = *p.buildMode;
    if (p.parallel) cfg.parallelCfg.mode = *p.parallel;
    if (p.adaptive) cfg.adaptiveCfg.mode = *p.adaptive;
    if (p.dataLayout) cfg.dataLayoutCfg.mode = *p.dataLayout;
    if (p.indirection) cfg.indirectionCfg.mode = *p.indirection;
    if (p.observability) cfg.observabilityCfg.mode = *p.observability;
    if (p.lifetime) cfg.lifetimeCfg.mode = *p.lifetime;
    if (p.loopParallel) cfg.loopParallelCfg.mode = *p.loopParallel;
    if (p.embedIR) cfg.embedIR = *p.embedIR;
    if (p.parallelInstrument) cfg.parallelCfg.instrument = *p.parallelInstrument;

    auto result = validateConfig(cfg);
    EXPECT_FALSE(result.hasErrors());
}

} // namespace
