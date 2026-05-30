#include "topo/Build/BackendProtocol.h"
#include "topo/Build/SymbolTableJson.h"
#include "topo/Lang/LanguagePlugin.h"
#include "topo/Lang/BuildDriverFactory.h"

#include <iostream>

using json = nlohmann::json;

namespace topo::build {

// ===================================================================
// BackendConfig JSON serialization
// ===================================================================

namespace {

void to_json(json& j, OptLevel level) {
    j = static_cast<int>(level);
}

void from_json(const json& j, OptLevel& level) {
    level = static_cast<OptLevel>(j.get<int>());
}

void to_json(json& j, BuildMode mode) {
    switch (mode) {
    case BuildMode::Dev: j = "dev"; break;
    case BuildMode::Aggressive: j = "aggressive"; break;
    }
}

void from_json(const json& j, BuildMode& mode) {
    auto s = j.get<std::string>();
    mode = (s == "aggressive") ? BuildMode::Aggressive : BuildMode::Dev;
}

void to_json(json& j, OutputType type) {
    switch (type) {
    case OutputType::Exe: j = "exe"; break;
    case OutputType::Shared: j = "shared"; break;
    case OutputType::Static: j = "static"; break;
    }
}

void from_json(const json& j, OutputType& type) {
    auto s = j.get<std::string>();
    if (s == "shared")
        type = OutputType::Shared;
    else if (s == "static")
        type = OutputType::Static;
    else
        type = OutputType::Exe;
}

void to_json(json& j, ObfuscationMode mode) {
    switch (mode) {
    case ObfuscationMode::Normal: j = "normal"; break;
    case ObfuscationMode::Salted: j = "salted"; break;
    }
}

void from_json(const json& j, ObfuscationMode& mode) {
    auto s = j.get<std::string>();
    mode = (s == "salted") ? ObfuscationMode::Salted : ObfuscationMode::Normal;
}

void to_json(json& j, HostLanguage lang) {
    switch (lang) {
    case HostLanguage::Cpp: j = "cpp"; break;
    case HostLanguage::Rust: j = "rust"; break;
    case HostLanguage::Java: j = "java"; break;
    case HostLanguage::Python: j = "python"; break;
    case HostLanguage::TypeScript: j = "typescript"; break;
    case HostLanguage::Mixed: j = "mixed"; break;
    }
}

void from_json(const json& j, HostLanguage& lang) {
    auto s = j.get<std::string>();
    if (s == "rust")
        lang = HostLanguage::Rust;
    else if (s == "java")
        lang = HostLanguage::Java;
    else if (s == "python")
        lang = HostLanguage::Python;
    else if (s == "typescript")
        lang = HostLanguage::TypeScript;
    else if (s == "mixed")
        lang = HostLanguage::Mixed;
    else
        lang = HostLanguage::Cpp;
}

// --- Pass config serialization ---

json serializeParallelConfig(const ParallelConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    j["exclude"] = cfg.exclude;
    j["instrument"] = cfg.instrument;
    j["benchmarkIterations"] = cfg.benchmarkIterations;
    j["benchmarkWarmup"] = cfg.benchmarkWarmup;
    return j;
}

ParallelConfig deserializeParallelConfig(const json& j) {
    ParallelConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    else
        cfg.mode = j.value("enabled", false) ? FeatureMode::Auto : FeatureMode::Off;
    if (j.contains("exclude")) cfg.exclude = j["exclude"].get<std::vector<std::string>>();
    cfg.instrument = j.value("instrument", false);
    cfg.benchmarkIterations = j.value("benchmarkIterations", 500);
    cfg.benchmarkWarmup = j.value("benchmarkWarmup", 50);
    return cfg;
}

json serializeAdaptiveConfig(const AdaptiveConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    j["min_trigger_ns"] = cfg.min_trigger_ns;
    j["benchmarkIterations"] = cfg.benchmarkIterations;
    j["benchmarkWarmup"] = cfg.benchmarkWarmup;
    return j;
}

AdaptiveConfig deserializeAdaptiveConfig(const json& j) {
    AdaptiveConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    else
        cfg.mode = j.value("enabled", false) ? FeatureMode::Auto : FeatureMode::Off;
    cfg.min_trigger_ns = j.value("min_trigger_ns", uint64_t(10000));
    cfg.benchmarkIterations = j.value("benchmarkIterations", 500);
    cfg.benchmarkWarmup = j.value("benchmarkWarmup", 50);
    return cfg;
}

json serializeDataLayoutConfig(const DataLayoutConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    j["benchmarkIterations"] = cfg.benchmarkIterations;
    j["benchmarkWarmup"] = cfg.benchmarkWarmup;
    return j;
}

DataLayoutConfig deserializeDataLayoutConfig(const json& j) {
    DataLayoutConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    else
        cfg.mode = j.value("enabled", false) ? FeatureMode::Auto : FeatureMode::Off;
    cfg.benchmarkIterations = j.value("benchmarkIterations", 1000);
    cfg.benchmarkWarmup = j.value("benchmarkWarmup", 100);
    return cfg;
}

json serializeIndirectionConfig(const IndirectionConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    j["uniquePtrPromotion"] = cfg.uniquePtrPromotion;
    j["sharedPtrExclusive"] = cfg.sharedPtrExclusive;
    j["vectorSpanLowering"] = cfg.vectorSpanLowering;
    j["pointerAttrInference"] = cfg.pointerAttrInference;
    j["devirtualize"] = cfg.devirtualize;
    j["benchmarkIterations"] = cfg.benchmarkIterations;
    j["benchmarkWarmup"] = cfg.benchmarkWarmup;
    return j;
}

IndirectionConfig deserializeIndirectionConfig(const json& j) {
    IndirectionConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    else
        cfg.mode = j.value("enabled", false) ? FeatureMode::Auto : FeatureMode::Off;
    cfg.uniquePtrPromotion = j.value("uniquePtrPromotion", true);
    cfg.sharedPtrExclusive = j.value("sharedPtrExclusive", true);
    cfg.vectorSpanLowering = j.value("vectorSpanLowering", true);
    cfg.pointerAttrInference = j.value("pointerAttrInference", true);
    cfg.devirtualize = j.value("devirtualize", true);
    cfg.benchmarkIterations = j.value("benchmarkIterations", 500);
    cfg.benchmarkWarmup = j.value("benchmarkWarmup", 50);
    return cfg;
}

json serializeObservabilityConfig(const ObservabilityConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    j["exporter"] = cfg.exporter;
    j["samplingRate"] = cfg.samplingRate;
    j["internalStages"] = cfg.internalStages;
    return j;
}

ObservabilityConfig deserializeObservabilityConfig(const json& j) {
    ObservabilityConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    else
        cfg.mode = j.value("enabled", false) ? FeatureMode::Auto : FeatureMode::Off;
    cfg.exporter = j.value("exporter", std::string("stdout"));
    cfg.samplingRate = j.value("samplingRate", 1.0);
    cfg.internalStages = j.value("internalStages", false);
    return cfg;
}

json serializeLifetimeConfig(const LifetimeConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    j["defaultArenaSize"] = cfg.defaultArenaSize;
    j["benchmarkIterations"] = cfg.benchmarkIterations;
    j["benchmarkWarmup"] = cfg.benchmarkWarmup;
    return j;
}

LifetimeConfig deserializeLifetimeConfig(const json& j) {
    LifetimeConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    else
        cfg.mode = j.value("enabled", false) ? FeatureMode::Auto : FeatureMode::Off;
    cfg.defaultArenaSize = j.value("defaultArenaSize", size_t(4096));
    cfg.benchmarkIterations = j.value("benchmarkIterations", 500);
    cfg.benchmarkWarmup = j.value("benchmarkWarmup", 50);
    return cfg;
}

json serializeLoopParallelConfig(const LoopParallelConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    j["exclude"] = cfg.exclude;
    j["benchmarkIterations"] = cfg.benchmarkIterations;
    j["benchmarkWarmup"] = cfg.benchmarkWarmup;
    j["partitionEnabled"] = cfg.partitionEnabled;
    j["reductionEnabled"] = cfg.reductionEnabled;
    j["partitionStrategy"] = loopPartitionStrategyToString(cfg.partitionStrategy);
    j["chunkSize"] = cfg.chunkSize;
    j["instrument"] = cfg.instrument;
    return j;
}

LoopParallelConfig deserializeLoopParallelConfig(const json& j) {
    LoopParallelConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    else
        cfg.mode = j.value("enabled", false) ? FeatureMode::Auto : FeatureMode::Off;
    if (j.contains("exclude")) cfg.exclude = j["exclude"].get<std::vector<std::string>>();
    cfg.benchmarkIterations = j.value("benchmarkIterations", 500);
    cfg.benchmarkWarmup = j.value("benchmarkWarmup", 50);
    cfg.partitionEnabled = j.value("partitionEnabled", false);
    cfg.reductionEnabled = j.value("reductionEnabled", false);
    if (j.contains("partitionStrategy"))
        cfg.partitionStrategy = parseLoopPartitionStrategy(j["partitionStrategy"].get<std::string>());
    cfg.chunkSize = j.value("chunkSize", 64);
    cfg.instrument = j.value("instrument", false);
    return cfg;
}

json serializePrefetchConfig(const PrefetchConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    j["distance"] = cfg.distance;
    return j;
}

PrefetchConfig deserializePrefetchConfig(const json& j) {
    PrefetchConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    else
        cfg.mode = j.value("enabled", false) ? FeatureMode::Auto : FeatureMode::Off;
    cfg.distance = j.value("distance", 8);
    return cfg;
}

json serializeTypeNarrowingConfig(const TypeNarrowingConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    return j;
}

TypeNarrowingConfig deserializeTypeNarrowingConfig(const json& j) {
    TypeNarrowingConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    else
        cfg.mode = j.value("enabled", false) ? FeatureMode::Auto : FeatureMode::Off;
    return cfg;
}

json serializeContainmentConfig(const ContainmentConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    return j;
}

ContainmentConfig deserializeContainmentConfig(const json& j) {
    ContainmentConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    else
        cfg.mode = j.value("enabled", false) ? FeatureMode::Auto : FeatureMode::Off;
    return cfg;
}

json serializePipelineConfig(const PipelineConfig& cfg) {
    json j = json::object();
    j["mode"] = featureModeToString(cfg.mode);
    return j;
}

PipelineConfig deserializePipelineConfig(const json& j) {
    PipelineConfig cfg;
    if (j.contains("mode"))
        cfg.mode = parseFeatureMode(j["mode"].get<std::string>());
    // No legacy "enabled" fallback — pipeline never had that key, and its
    // default is Auto rather than Off.
    return cfg;
}

json serializeBackendConfig(const backend::BackendConfig& cfg) {
    json j = json::object();
    j["optLevel"] = cfg.optLevel;
    j["buildMode"] = cfg.buildMode;
    j["outputType"] = cfg.outputType;
    j["obfMode"] = cfg.obfMode;
    j["obfSalt"] = cfg.obfSalt;
    j["noVerify"] = cfg.noVerify;
    j["warnOnly"] = cfg.warnOnly;
    j["dumpIR"] = cfg.dumpIR;
    j["dumpMap"] = cfg.dumpMap;
    j["debugInternal"] = cfg.debugInternal;
    j["embedIR"] = cfg.embedIR;
    j["outputPath"] = cfg.outputPath;
    j["parallelCfg"] = serializeParallelConfig(cfg.parallelCfg);
    j["adaptiveCfg"] = serializeAdaptiveConfig(cfg.adaptiveCfg);
    j["dataLayoutCfg"] = serializeDataLayoutConfig(cfg.dataLayoutCfg);
    j["indirectionCfg"] = serializeIndirectionConfig(cfg.indirectionCfg);
    j["indirectionExplicit"] = cfg.indirectionExplicit;
    j["observabilityCfg"] = serializeObservabilityConfig(cfg.observabilityCfg);
    j["lifetimeCfg"] = serializeLifetimeConfig(cfg.lifetimeCfg);
    j["loopParallelCfg"] = serializeLoopParallelConfig(cfg.loopParallelCfg);
    j["prefetchCfg"] = serializePrefetchConfig(cfg.prefetchCfg);
    j["typeNarrowingCfg"] = serializeTypeNarrowingConfig(cfg.typeNarrowingCfg);
    j["containmentCfg"] = serializeContainmentConfig(cfg.containmentCfg);
    j["pipelineCfg"] = serializePipelineConfig(cfg.pipelineCfg);
    return j;
}

backend::BackendConfig deserializeBackendConfig(const json& j) {
    backend::BackendConfig cfg;
    if (j.contains("optLevel")) j["optLevel"].get_to(cfg.optLevel);
    if (j.contains("buildMode")) j["buildMode"].get_to(cfg.buildMode);
    if (j.contains("outputType")) j["outputType"].get_to(cfg.outputType);
    if (j.contains("obfMode")) j["obfMode"].get_to(cfg.obfMode);
    cfg.obfSalt = j.value("obfSalt", std::string());
    cfg.noVerify = j.value("noVerify", false);
    cfg.warnOnly = j.value("warnOnly", false);
    cfg.dumpIR = j.value("dumpIR", false);
    cfg.dumpMap = j.value("dumpMap", false);
    cfg.debugInternal = j.value("debugInternal", false);
    cfg.embedIR = j.value("embedIR", false);
    cfg.outputPath = j.value("outputPath", std::string());
    if (j.contains("parallelCfg")) cfg.parallelCfg = deserializeParallelConfig(j["parallelCfg"]);
    if (j.contains("adaptiveCfg")) cfg.adaptiveCfg = deserializeAdaptiveConfig(j["adaptiveCfg"]);
    if (j.contains("dataLayoutCfg")) cfg.dataLayoutCfg = deserializeDataLayoutConfig(j["dataLayoutCfg"]);
    if (j.contains("indirectionCfg")) cfg.indirectionCfg = deserializeIndirectionConfig(j["indirectionCfg"]);
    cfg.indirectionExplicit = j.value("indirectionExplicit", false);
    if (j.contains("observabilityCfg")) cfg.observabilityCfg = deserializeObservabilityConfig(j["observabilityCfg"]);
    if (j.contains("lifetimeCfg")) cfg.lifetimeCfg = deserializeLifetimeConfig(j["lifetimeCfg"]);
    if (j.contains("loopParallelCfg")) cfg.loopParallelCfg = deserializeLoopParallelConfig(j["loopParallelCfg"]);
    if (j.contains("prefetchCfg")) cfg.prefetchCfg = deserializePrefetchConfig(j["prefetchCfg"]);
    if (j.contains("typeNarrowingCfg")) cfg.typeNarrowingCfg = deserializeTypeNarrowingConfig(j["typeNarrowingCfg"]);
    if (j.contains("containmentCfg")) cfg.containmentCfg = deserializeContainmentConfig(j["containmentCfg"]);
    if (j.contains("pipelineCfg")) cfg.pipelineCfg = deserializePipelineConfig(j["pipelineCfg"]);
    return cfg;
}

} // anonymous namespace

// ===================================================================
// Public API
// ===================================================================

std::string serializeBackendRequest(const BackendRequest& req) {
    json j = json::object();
    j["config"] = serializeBackendConfig(req.config);
    j["outputPath"] = req.outputPath;
    j["topoMetadata"] = serializeSymbolTable(req.symbolTable);
    j["visibilityEntries"] = req.visibilityEntries;
    j["tempDir"] = req.tempDir;

    json langJ;
    to_json(langJ, req.language);
    j["language"] = langJ;

    // Generic fields
    j["sources"] = req.sources;
    j["includeDirs"] = req.includeDirs;
    j["linkLibs"] = req.linkLibs;
    j["linkDirs"] = req.linkDirs;
    j["verbose"] = req.verbose;
    j["keepTemps"] = req.keepTemps;
    j["noIncremental"] = req.noIncremental;

    // Backend-specific extensions
    j["backendExtras"] = req.backendExtras;

    return j.dump();
}

bool deserializeBackendRequest(const std::string& jsonStr, BackendRequest& req) {
    try {
        json j = json::parse(jsonStr);

        req.config = deserializeBackendConfig(j.at("config"));
        req.outputPath = j.at("outputPath").get<std::string>();

        if (!deserializeSymbolTable(j.at("topoMetadata"), req.symbolTable)) return false;

        req.visibilityEntries = j.at("visibilityEntries").get<std::vector<VisibilityEntry>>();
        req.tempDir = j.at("tempDir").get<std::string>();

        from_json(j.at("language"), req.language);

        // Generic fields
        if (j.contains("sources")) req.sources = j["sources"].get<std::vector<std::string>>();
        if (j.contains("includeDirs")) req.includeDirs = j["includeDirs"].get<std::vector<std::string>>();
        if (j.contains("linkLibs")) req.linkLibs = j["linkLibs"].get<std::vector<std::string>>();
        if (j.contains("linkDirs")) req.linkDirs = j["linkDirs"].get<std::vector<std::string>>();
        req.verbose = j.value("verbose", false);
        req.keepTemps = j.value("keepTemps", false);
        req.noIncremental = j.value("noIncremental", false);

        // Backend-specific extensions
        req.backendExtras = j.value("backendExtras", json::object());

        // Fail-fast unknown-key check for backends with an enforced
        // sub-schema (currently only JVM). LLVM / Python / TypeScript
        // stay silent-tolerant, as their schemas are still in flux.
        // We deliberately label by HostLanguage (not the plugin-derived
        // tool name) so this diagnostic is consistent whether the
        // deserializer runs in topo-build (plugins registered) or in
        // a standalone backend tool (no plugin registration).
        if (backendExtrasRejectsUnknown(req.language) && req.backendExtras.is_object()) {
            const auto& known = knownBackendExtrasKeys(req.language);
            auto langLabel = [](HostLanguage l) -> const char* {
                switch (l) {
                case HostLanguage::Java: return "java";
                case HostLanguage::Cpp: return "cpp";
                case HostLanguage::Rust: return "rust";
                case HostLanguage::Mixed: return "mixed";
                case HostLanguage::Python: return "python";
                case HostLanguage::TypeScript: return "typescript";
                }
                return "?";
            };
            for (const auto& [key, value] : req.backendExtras.items()) {
                (void)value;
                if (known.find(key) == known.end()) {
                    std::cerr << "error: backendExtras: unknown key '" << key
                              << "' for " << langLabel(req.language)
                              << " backend\n";
                    return false;
                }
            }
        }

        return true;
    } catch (const json::exception&) {
        return false;
    }
}

std::string backendToolName(HostLanguage language) {
    auto* plugin = lang::getPlugin(language);
    if (plugin && plugin->buildDriverFactory()) {
        return plugin->buildDriverFactory()->backendToolName();
    }
    // Fallback for Mixed or unregistered
    switch (language) {
    case HostLanguage::Mixed: return "topo-build-llvm-mixed";
    default: return "topo-build-llvm-cpp";
    }
}

// ===================================================================
// backendExtras key registry: enumerates the keys each backend
// tolerates / rejects in the JSON request, per the topo-build
// backend protocol's unknown-key handling rules.
// ===================================================================
//
// Unknown-key rejection at deserialize is JVM-only today (see
// `backendExtrasRejectsUnknown`). Every other backend listed below
// keeps the historical silent-tolerant behaviour for *unknown* keys,
// but per-value type validation for *known* keys is enforced inside
// each backend's main.cpp before any compile/check step runs (the
// non-JVM backends lack deserialize-time subschema validation).

const std::unordered_set<std::string>& knownBackendExtrasKeys(HostLanguage language) {
    static const std::unordered_set<std::string> kJvmKeys = {
        "javaHome",
        "classpath",
        "jvmArgs",
        "targetVersion",
        "mainClass",
    };
    static const std::unordered_set<std::string> kLlvmCppKeys = {
        "hostCompilerPath",
        "standard",
    };
    static const std::unordered_set<std::string> kLlvmRustKeys = {
        "hostCompilerPath",
        "standard",
        "cargoPath",
    };
    static const std::unordered_set<std::string> kLlvmMixedKeys = {
        "hostCompilerPath",
        "standard",
        "cargoPath",
        "mixedConfig",
    };
    static const std::unordered_set<std::string> kPythonKeys = {
        "pythonPath",
        "venvPath",
        "topoCheckJobs",
    };
    static const std::unordered_set<std::string> kTypeScriptKeys = {
        "nodePath",
        "tsconfigPath",
        "packageManager",
    };
    static const std::unordered_set<std::string> kEmpty = {};

    switch (language) {
    case HostLanguage::Java: return kJvmKeys;
    case HostLanguage::Cpp: return kLlvmCppKeys;
    case HostLanguage::Rust: return kLlvmRustKeys;
    case HostLanguage::Mixed: return kLlvmMixedKeys;
    case HostLanguage::Python: return kPythonKeys;
    case HostLanguage::TypeScript: return kTypeScriptKeys;
    }
    return kEmpty;
}

bool backendExtrasRejectsUnknown(HostLanguage language) {
    // JVM is the only finalised sub-schema. The remaining backends
    // keep historical silent-tolerant behaviour until each finalises
    // its own schema.
    return language == HostLanguage::Java;
}

} // namespace topo::build
