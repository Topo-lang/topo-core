#ifndef TOPO_BUILD_BUILDCONFIG_H
#define TOPO_BUILD_BUILDCONFIG_H

#include "topo/Basic/BuildTypes.h"
#include "topo/Basic/HostLanguage.h"
#include "topo/Build/PassConfig.h"

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace topo::build {

/// Three-state setting for [build].check.
/// Auto: no explicit preference — run topo-check on every build (the
///       default). Check is the universal half of the toolchain promise:
///       declarations participate in transpile / debug views / optimization
///       regardless of build flavor, so they are validated on every build,
///       not only when an optimization consumes them.
/// On:   always run topo-check as part of the build. Behaves like Auto
///       today, but pins the choice explicitly so a future change to the
///       Auto default can never silently turn this project's check off.
/// Off:  skip topo-check even if it would otherwise run.
enum class CheckMode { Auto, On, Off };

inline CheckMode parseCheckMode(const std::string& s, bool& ok) {
    ok = true;
    if (s == "auto") return CheckMode::Auto;
    if (s == "on") return CheckMode::On;
    if (s == "off") return CheckMode::Off;
    ok = false;
    return CheckMode::Auto;
}

inline const char* checkModeToString(CheckMode m) {
    switch (m) {
    case CheckMode::Auto: return "auto";
    case CheckMode::On: return "on";
    case CheckMode::Off: return "off";
    }
    return "auto";
}

/// A partial configuration overlay. Only fields the user specifies get set;
/// unset fields (std::nullopt) inherit from the parent profile or base config.
struct ProfileOverrides {
    std::optional<std::string> extends; // parent profile name
    std::optional<BuildMode> buildMode;
    std::optional<OptLevel> optLevel;
    std::optional<ObfuscationMode> obfMode;
    std::optional<std::string> obfSalt;
    std::optional<bool> embedIR;

    // Feature modes (the big switches; sub-params stay at base config defaults)
    std::optional<FeatureMode> parallel;
    std::optional<FeatureMode> adaptive;
    std::optional<FeatureMode> dataLayout;
    std::optional<FeatureMode> indirection;
    std::optional<FeatureMode> observability;
    std::optional<FeatureMode> lifetime;
    std::optional<FeatureMode> loopParallel;
    std::optional<FeatureMode> prefetch;
    std::optional<FeatureMode> typeNarrowing;
    std::optional<FeatureMode> containment;

    // Observability-specific
    std::optional<std::string> observabilityExporter;

    // Parallel-specific
    std::optional<bool> parallelInstrument;
};

struct BuildConfig {
    HostLanguage language = HostLanguage::Cpp;
    std::vector<std::string> sources;
    std::string topoRoot;
    std::string outputPath;
    std::vector<std::string> includeDirs;
    std::string standard = "c++17";
    OptLevel optLevel = OptLevel::O2;
    ObfuscationMode obfMode = ObfuscationMode::Normal;
    std::string obfSalt;
    BuildMode buildMode = BuildMode::Dev;
    OutputType outputType = OutputType::Exe;
    bool noVerify = false;
    bool warnOnly = false;
    bool dumpIR = false;
    bool dumpMap = false;
    bool keepTemps = false;
    bool verbose = false;
    bool debugInternal = false;
    bool embedIR = false;
    std::string hostCompilerPath; // clang++ path (resolved at init)
    std::string cargoPath = "cargo";
    ParallelConfig parallelCfg;
    AdaptiveConfig adaptiveCfg;
    DataLayoutConfig dataLayoutCfg;
    IndirectionConfig indirectionCfg;
    bool indirectionExplicit = false; // whether [optimize.indirection] section explicitly exists in Topo.toml
    ObservabilityConfig observabilityCfg;
    LifetimeConfig lifetimeCfg;
    LoopParallelConfig loopParallelCfg;
    PrefetchConfig prefetchCfg;
    TypeNarrowingConfig typeNarrowingCfg;
    ContainmentConfig containmentCfg;
    PipelineConfig pipelineCfg;
    std::vector<std::string> linkLibs;
    std::vector<std::string> linkDirs;
    bool noIncremental = false;                                // --no-incremental: force full rebuild
    bool cleanCache = false;                                   // --clean: remove .topo-cache/ before build
    CheckMode checkMode = CheckMode::Auto;                     // [build].check: on / off / auto (default auto)
    std::optional<bool> checkCliOverride;                      // --check / --no-check: dominates checkMode
    std::unordered_map<std::string, std::string> typeBindings; // [types] overrides

    // Deploy profiles
    std::string selectedProfile;                      // from --profile CLI flag (empty = no profile)
    std::map<std::string, ProfileOverrides> profiles; // parsed [profile.*] sections

    // Raw TOML string values for validation (enum values checked against allowed sets)
    std::string rawLanguage;
    std::string rawOutputType;
    std::string rawBuilderMode;
    std::string rawObfuscation;
    std::string rawCheck;

    /// True when an enabled optimization consumes .topo declarations whose
    /// incorrectness would make the optimization unsafe (parallelism →
    /// concurrency, loop-parallelism, lifetime → memory). Optimizing on such
    /// declarations is only licensed once they are checked. Checking is on by
    /// default for every build, so this predicate no longer gates whether
    /// check runs; it grades the *opt-out*: a build driver that sees
    /// shouldRunCheck() == false while this returns true is skipping
    /// validation the enabled optimizations rely on, and warns loudly
    /// (declarations enter the build UNVERIFIED).
    ///
    /// parallel / loopParallel / lifetime default to Off, so any non-Off value
    /// is a deliberate opt-in. pipeline defaults to Auto, so only an explicit
    /// Force counts as the user enabling it (the no-op Auto pipeline default
    /// must not, on its own, upgrade every unchecked build into the loud
    /// warning path).
    bool consumesDeclarationsForOptimization() const {
        return parallelCfg.mode != FeatureMode::Off
            || loopParallelCfg.mode != FeatureMode::Off
            || lifetimeCfg.mode != FeatureMode::Off
            || pipelineCfg.mode == FeatureMode::Force;
    }

    /// Resolve whether topo-check should run. Precedence: CLI (--check /
    /// --no-check) > [build].check On/Off > Auto. Auto checks every build:
    /// a violating project fails a plain build the same way it fails an
    /// optimized one, and the incremental check cache (.topo-check-cache)
    /// keeps the no-change rebuild overhead near zero. The only unchecked
    /// builds are explicit opt-outs (--no-check / [build].check = "off").
    bool shouldRunCheck() const {
        if (checkCliOverride.has_value()) return *checkCliOverride;
        if (checkMode == CheckMode::On) return true;
        if (checkMode == CheckMode::Off) return false;
        return true; // Auto: every build checks by default
    }

    // Mixed C++/Rust project configuration (only used when language == Mixed)
    struct MixedConfig {
        // C++ side
        std::vector<std::string> cppSources;
        std::vector<std::string> cppIncludeDirs;
        std::vector<std::string> cppFlags;
        // Rust side
        std::string rustManifest; // path to Cargo.toml
    } mixedCfg;

    // Java-specific configuration (only used when language == Java)
    struct JavaConfig {
        std::string targetVersion; // [build.java].target_version → javac --release N
    } javaCfg;
};

/// Result of a driver compile or link step.
struct DriverResult {
    int exitCode = 0;
    std::vector<std::string> outputFiles;
};

/// Get built-in profile definitions (embedded, server, wasm, desktop).
const std::map<std::string, ProfileOverrides>& builtinProfiles();

} // namespace topo::build

#endif // TOPO_BUILD_BUILDCONFIG_H
