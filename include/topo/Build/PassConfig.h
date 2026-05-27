#ifndef TOPO_BUILD_PASSCONFIG_H
#define TOPO_BUILD_PASSCONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace topo {

// --- Three-level feature mode ---
// Off:   feature disabled, zero overhead
// Auto:  compiler evaluates benefit; skips if not worthwhile (never slower than Off)
// Force: unconditionally enabled, skips benefit evaluation
enum class FeatureMode { Off, Auto, Force };

/// Parse a feature mode string. Returns Off for unrecognized values.
inline FeatureMode parseFeatureMode(const std::string& s) {
    if (s == "auto") return FeatureMode::Auto;
    if (s == "force") return FeatureMode::Force;
    if (s == "off") return FeatureMode::Off;
    return FeatureMode::Off;
}

inline const char* featureModeToString(FeatureMode m) {
    switch (m) {
    case FeatureMode::Off: return "off";
    case FeatureMode::Auto: return "auto";
    case FeatureMode::Force: return "force";
    }
    return "off";
}

// --- Feature taxonomy (mirrors the project's authoritative feature-taxonomy definition) ---
// Every pass declares exactly one category. Category determines benchmark assertion shape
// and whether the pass appears in the benchmark suite at all.
enum class FeatureCategory {
    OPT,         // Direct measurable speedup path (friendly forced/base < 0.9)
    ENHANCE,     // Semantic boundary hardening / declaration-class bytecode or IR witness
    COVERED,     // Transform correct, but downstream compiler (LLVM O2 / HotSpot C2 / HW) already covers it
    INSTRUMENT,  // Observable artifact (tracing, rename, metadata) with runtime overhead
    INFRA,       // Supports other passes; not a user-visible optimization (excluded from benchmarks)
    RUNTIME,     // Runtime component dependency (JIT engine, adaptive monitor, parallel dispatcher)
};

const char* toString(FeatureCategory cat);

// --- Pass configuration structs ---

struct ParallelConfig {
    FeatureMode mode = FeatureMode::Off;
    // Note: `minTasksToParallelize` (grain threshold) was removed —
    // Topo passes do not gate on workload-side cost heuristics. The Pass
    // unconditionally parallelizes any non-empty stage candidate set.
    std::vector<std::string> exclude;
    bool instrument = false;
    int benchmarkIterations = 500;
    int benchmarkWarmup = 50;

    // Convenience: is this feature active (auto or force)?
    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct AdaptiveConfig {
    FeatureMode mode = FeatureMode::Off;
    // Default 10000ns (10us). Threshold comparison benchmarks in
    // topo-llvm/benchmarks/adaptive/Topo-threshold-{low,mid,high}.toml
    // cover 1000ns / 10000ns / 100000ns to evaluate sensitivity.
    uint64_t min_trigger_ns = 10000;
    int benchmarkIterations = 500;
    int benchmarkWarmup = 50;

    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct DataLayoutConfig {
    FeatureMode mode = FeatureMode::Off;
    // Note: `minArraySize` (size threshold) was removed — Topo
    // passes do not gate on workload-side cost heuristics. The Pass
    // unconditionally rewrites any qualifying topo::array<T,N> from AoS to
    // SoA; LLVM cost model decides downstream cleanup.
    int benchmarkIterations = 1000;
    int benchmarkWarmup = 100;

    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct IndirectionConfig {
    FeatureMode mode = FeatureMode::Off;
    bool uniquePtrPromotion = true;
    bool sharedPtrExclusive = true;
    bool vectorSpanLowering = true;
    bool pointerAttrInference = true;
    bool devirtualize = true;
    bool vtableOptimize = true;
    int benchmarkIterations = 500;
    int benchmarkWarmup = 50;

    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct ObservabilityConfig {
    FeatureMode mode = FeatureMode::Off;
    std::string exporter = "stdout";
    double samplingRate = 1.0;
    bool internalStages = false;

    bool isEnabled() const { return mode != FeatureMode::Off; }
};

/// Gate for pipeline DAG codegen (LLVM PipelineCodeGenPass / JVM PipelinePass).
/// Default is Auto — whenever the .topo declarations contain a pipeline
/// logic block, the backend should rewrite the orchestrator into a DAG.
/// `off` turns the rewrite off entirely (used by benchmark base configs
/// to compare against the unwritten baseline).  `force` is identical to
/// `auto` today; reserved so tooling can distinguish "user opted in" from
/// "default on".
struct PipelineConfig {
    FeatureMode mode = FeatureMode::Auto;

    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct LifetimeConfig {
    FeatureMode mode = FeatureMode::Off;
    size_t defaultArenaSize = 4096;
    int benchmarkIterations = 500;
    int benchmarkWarmup = 50;

    bool isEnabled() const { return mode != FeatureMode::Off; }
};

/// Partition strategy for loop-level parallelization.
enum class LoopPartitionStrategy {
    Static, // Divide iteration space evenly across cores
    Dynamic // Work-stealing: each core grabs next chunk when idle
};

inline LoopPartitionStrategy parseLoopPartitionStrategy(const std::string& s) {
    if (s == "dynamic") return LoopPartitionStrategy::Dynamic;
    return LoopPartitionStrategy::Static;
}

inline const char* loopPartitionStrategyToString(LoopPartitionStrategy s) {
    switch (s) {
    case LoopPartitionStrategy::Static: return "static";
    case LoopPartitionStrategy::Dynamic: return "dynamic";
    }
    return "static";
}

struct LoopParallelConfig {
    FeatureMode mode = FeatureMode::Off;
    std::vector<std::string> exclude;
    int benchmarkIterations = 500;
    int benchmarkWarmup = 50;

    // Partition-based loop parallelization
    bool partitionEnabled = false; // Enable iteration-space partitioning
    LoopPartitionStrategy partitionStrategy = LoopPartitionStrategy::Static;
    // Note: `minTripCount` (loop length threshold) was removed —
    // Topo passes do not gate on workload-side cost heuristics. Partition
    // applies to any loop with a known trip count; benchmark/runtime
    // determines benefit.
    int chunkSize = 64;      // Chunk size for dynamic scheduling
    bool instrument = false; // Insert cost sampling around loop partitions
    bool reductionEnabled = false; // Enable reduction parallelism for loops with associative accumulations

    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct PrefetchConfig {
    FeatureMode mode = FeatureMode::Off;
    int distance = 8; // prefetch distance in elements

    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct TypeNarrowingConfig {
    FeatureMode mode = FeatureMode::Off;

    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct InlineConfig {
    // Note: `functorSizeThreshold` and `emitRemarks` were removed in Plan
    // 34 — Topo's TopoInlinePass uniformly emits AlwaysInline on declared
    // private/internal functor callees; LLVM's standard inliner is the cost
    // model authority for actual inlining decisions.
};

struct ContainmentConfig {
    FeatureMode mode = FeatureMode::Off;
    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct PurityConfig {
    FeatureMode mode = FeatureMode::Off;
    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct VisibilityCheckConfig {
    FeatureMode mode = FeatureMode::Off;
    bool isEnabled() const { return mode != FeatureMode::Off; }
};

struct StageIsolationConfig {
    FeatureMode mode = FeatureMode::Off;
    bool isEnabled() const { return mode != FeatureMode::Off; }
};

} // namespace topo

#endif // TOPO_BUILD_PASSCONFIG_H
