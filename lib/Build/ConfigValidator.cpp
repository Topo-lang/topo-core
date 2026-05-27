#include "topo/Build/ConfigValidator.h"

namespace topo {

ValidationResult validateConfig(const build::BuildConfig& cfg) {
    ValidationResult result;

    // --- [parallel] ---
    // Note: `[parallel].min_tasks_to_parallelize` validation removed in Plan
    // 34 — the field itself was removed from ParallelConfig.

    if (cfg.parallelCfg.instrument && !cfg.parallelCfg.isEnabled()) {
        result.errors.push_back({ConfigErrorLevel::Warning,
                                 "[parallel]",
                                 "instrument",
                                 "[parallel].instrument = true has no effect without [parallel] mode = auto/force"});
    }

    // --- [adaptive] ---
    if (cfg.adaptiveCfg.isEnabled()) {
        if (!cfg.embedIR) {
            result.errors.push_back(
                {ConfigErrorLevel::Error, "[adaptive]", "mode", "[adaptive] requires [build].embed_ir = true"});
        }
        if (!cfg.parallelCfg.instrument) {
            result.errors.push_back(
                {ConfigErrorLevel::Error, "[adaptive]", "mode", "[adaptive] requires [parallel].instrument = true"});
        }
        if (cfg.adaptiveCfg.min_trigger_ns == 0) {
            result.errors.push_back(
                {ConfigErrorLevel::Error, "[adaptive]", "min_trigger_ns", "[adaptive].min_trigger_ns must be > 0"});
        }
    }

    // --- [optimize.data-layout] ---
    // Note: `[optimize.data-layout].min_array_size` validation was removed —
    // the field itself no longer exists on DataLayoutConfig.

    // --- [optimize.indirection] ---
    if (cfg.indirectionCfg.isEnabled() && !cfg.indirectionCfg.uniquePtrPromotion &&
        !cfg.indirectionCfg.sharedPtrExclusive && !cfg.indirectionCfg.vectorSpanLowering &&
        !cfg.indirectionCfg.pointerAttrInference && !cfg.indirectionCfg.devirtualize) {
        result.errors.push_back({ConfigErrorLevel::Warning,
                                 "[optimize.indirection]",
                                 "mode",
                                 "[optimize.indirection] is enabled but all sub-switches are off"});
    }

    // --- [observability] ---
    // Validate exporter and sampling_rate even when disabled, so invalid values
    // are caught early rather than breaking when later enabled.
    {
        const auto& exp = cfg.observabilityCfg.exporter;
        if (exp != "stdout") {
            result.errors.push_back(
                {ConfigErrorLevel::Error,
                 "[observability]",
                 "exporter",
                 "[observability].exporter only supports 'stdout' (got '" + exp +
                     "'); 'json'/'otlp' are not implemented"});
        }
        if (cfg.observabilityCfg.samplingRate < 0.0 || cfg.observabilityCfg.samplingRate > 1.0) {
            result.errors.push_back({ConfigErrorLevel::Error,
                                     "[observability]",
                                     "sampling_rate",
                                     "[observability].sampling_rate must be in [0.0, 1.0]"});
        }
    }

    // --- [lifetime] ---
    if (cfg.lifetimeCfg.isEnabled()) {
        if (cfg.lifetimeCfg.defaultArenaSize == 0) {
            result.errors.push_back({ConfigErrorLevel::Error,
                                     "[lifetime]",
                                     "default_arena_size",
                                     "[lifetime].default_arena_size must be > 0"});
        }
    }

    // --- [optimize.prefetch] ---
    if (cfg.prefetchCfg.isEnabled() && cfg.prefetchCfg.distance <= 0) {
        result.errors.push_back({ConfigErrorLevel::Error,
                                 "[optimize.prefetch]",
                                 "distance",
                                 "[optimize.prefetch].distance must be > 0"});
    }

    // --- [containment] ---
    if (cfg.containmentCfg.isEnabled() && cfg.language != HostLanguage::Cpp) {
        result.errors.push_back(
            {ConfigErrorLevel::Warning,
             "[containment]",
             "mode",
             "[containment] is enabled but the current language has no ImportExtractor (only C++ is supported)"});
    }

    // --- [loop_parallel] ---
    if (cfg.loopParallelCfg.isEnabled() && !cfg.parallelCfg.isEnabled()) {
        result.errors.push_back(
            {ConfigErrorLevel::Warning, "[loop_parallel]", "mode", "[loop_parallel] is enabled but [parallel] is off"});
    }
    if (cfg.loopParallelCfg.partitionEnabled && !cfg.loopParallelCfg.isEnabled()) {
        result.errors.push_back({ConfigErrorLevel::Warning,
                                 "[loop_parallel]",
                                 "partition",
                                 "[loop_parallel].partition is true but [loop_parallel] mode is off"});
    }
    // Note: `[loop_parallel].min_trip_count` validation was removed —
    // the field itself no longer exists on LoopParallelConfig.
    if (cfg.loopParallelCfg.partitionEnabled && cfg.loopParallelCfg.chunkSize < 1) {
        result.errors.push_back(
            {ConfigErrorLevel::Error, "[loop_parallel]", "chunk_size", "[loop_parallel].chunk_size must be >= 1"});
    }

    // --- Raw string enum validation ---
    const auto& language = cfg.rawLanguage;
    if (!language.empty() && language != "cpp" && language != "c++" && language != "rust" && language != "java" &&
        language != "python" && language != "typescript" && language != "mixed") {
        result.errors.push_back(
            {ConfigErrorLevel::Error,
             "[build]",
             "language",
             "[build].language must be one of: cpp, c++, rust, java, python, typescript, mixed (got '" +
                 language + "')"});
    }

    // --- [build] mixed language validation ---
    if (cfg.language == HostLanguage::Mixed) {
        if (cfg.mixedCfg.cppSources.empty()) {
            result.errors.push_back({ConfigErrorLevel::Error,
                                     "[build.cpp]",
                                     "sources",
                                     "[build.cpp].sources is required for mixed language projects"});
        }
        if (cfg.mixedCfg.rustManifest.empty()) {
            result.errors.push_back({ConfigErrorLevel::Error,
                                     "[build.rust]",
                                     "manifest",
                                     "[build.rust].manifest is required for mixed language projects"});
        }
    }

    const auto& outputType = cfg.rawOutputType;
    if (!outputType.empty() && outputType != "exe" && outputType != "shared" && outputType != "static") {
        result.errors.push_back({ConfigErrorLevel::Error,
                                 "[build]",
                                 "output_type",
                                 "[build].output_type must be one of: exe, shared, static (got '" + outputType + "')"});
    }

    const auto& builderMode = cfg.rawBuilderMode;
    if (!builderMode.empty() && builderMode != "dev" && builderMode != "aggressive") {
        result.errors.push_back({ConfigErrorLevel::Error,
                                 "[builder]",
                                 "mode",
                                 "[builder].mode must be one of: dev, aggressive (got '" + builderMode + "')"});
    }

    // --- [builder].obfuscation ---
    const auto& obfuscation = cfg.rawObfuscation;
    if (!obfuscation.empty() && obfuscation != "normal" && obfuscation != "salted") {
        result.errors.push_back({ConfigErrorLevel::Error,
                                 "[builder]",
                                 "obfuscation",
                                 "[builder].obfuscation must be one of: normal, salted (got '" + obfuscation + "')"});
    }

    // --- [build].check ---
    const auto& check = cfg.rawCheck;
    if (!check.empty() && check != "on" && check != "off" && check != "auto") {
        result.errors.push_back({ConfigErrorLevel::Error,
                                 "[build]",
                                 "check",
                                 "[build].check must be one of: on, off, auto (got '" + check + "')"});
    }

    // --- Cross-feature: embed_ir + topo-jit in link_libs ---
    {
        bool hasJitInLinkLibs = false;
        for (const auto& lib : cfg.linkLibs) {
            if (lib == "topo-jit") {
                hasJitInLinkLibs = true;
                break;
            }
        }
        if (hasJitInLinkLibs && !cfg.embedIR) {
            result.errors.push_back(
                {ConfigErrorLevel::Warning,
                 "[build]",
                 "embed_ir",
                 "topo-jit in [build].link_libs requires [build].embed_ir = true for JIT to work at runtime"});
        }
    }

    return result;
}

ValidationResult validateConfig(bool adaptiveEnabled,
                                bool embedIR,
                                bool parallelInstrument,
                                const std::string& language,
                                const std::string& outputType,
                                const std::string& builderMode) {
    // Deprecated wrapper: construct a minimal BuildConfig and delegate
    build::BuildConfig cfg;
    cfg.adaptiveCfg.mode = adaptiveEnabled ? FeatureMode::Auto : FeatureMode::Off;
    cfg.embedIR = embedIR;
    cfg.parallelCfg.instrument = parallelInstrument;
    cfg.rawLanguage = language;
    cfg.rawOutputType = outputType;
    cfg.rawBuilderMode = builderMode;
    return validateConfig(cfg);
}

} // namespace topo
