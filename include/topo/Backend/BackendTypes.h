#ifndef TOPO_BACKEND_BACKENDTYPES_H
#define TOPO_BACKEND_BACKENDTYPES_H

#include "topo/Basic/BuildTypes.h"

#include <string>
#include <utility>
#include <vector>

namespace topo::backend {

/// Projection of SymbolMapping for consumers that have no LLVM dependency.
struct MappingResult {
    std::vector<std::pair<std::string, std::string>> matched; // (topo name, IR mangled name)
    std::vector<std::string> unmatchedTopo;
    std::vector<std::string> unmatchedIR;
};

/// JIT export entry: tracks original and (possibly obfuscated) current mangled name.
struct JitExportEntry {
    std::string originalMangledName;
    std::string currentMangledName;
};

/// All configuration the backend needs for Steps 4-6.
struct BackendConfig {
    OptLevel optLevel = OptLevel::O2;
    BuildMode buildMode = BuildMode::Dev;
    OutputType outputType = OutputType::Exe;
    ObfuscationMode obfMode = ObfuscationMode::Normal;
    std::string obfSalt;
    bool noVerify = false;
    bool warnOnly = false;
    bool dumpIR = false;
    bool dumpMap = false;
    bool debugInternal = false;
    bool embedIR = false;
    std::string outputPath;
    ParallelConfig parallelCfg;
    AdaptiveConfig adaptiveCfg;
    DataLayoutConfig dataLayoutCfg;
    IndirectionConfig indirectionCfg;
    bool indirectionExplicit = false; // whether [optimize.indirection] section explicitly exists
    ObservabilityConfig observabilityCfg;
    LifetimeConfig lifetimeCfg;
    LoopParallelConfig loopParallelCfg;
    PrefetchConfig prefetchCfg;
    TypeNarrowingConfig typeNarrowingCfg;
    ContainmentConfig containmentCfg;
    PipelineConfig pipelineCfg;
};

/// Result from the full optimize() pipeline.
struct BackendResult {
    ApplyStats visibilityStats;
    ObfuscationResult obfuscation;
    std::vector<JitExportEntry> jitExports;
    int embeddedIRBytes = 0;
    int embeddedMetaBytes = 0;
};

} // namespace topo::backend

#endif // TOPO_BACKEND_BACKENDTYPES_H
