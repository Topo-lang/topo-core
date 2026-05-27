// Pass -> FeatureCategory registry.
//
// Mirrors the project's authoritative feature-taxonomy table exactly.
// If that taxonomy changes, this file MUST be updated in the same PR,
// and vice versa.
//
// Mapping covers both LLVM Transforms (17 passes) and JVM transform passes
// (15 passes) for a total of 32 entries.

#include "topo/Build/PassCategoryRegistry.h"

#include <string_view>
#include <unordered_map>

namespace topo {

const char* toString(FeatureCategory cat) {
    switch (cat) {
    case FeatureCategory::OPT: return "OPT";
    case FeatureCategory::ENHANCE: return "ENHANCE";
    case FeatureCategory::COVERED: return "COVERED";
    case FeatureCategory::INSTRUMENT: return "INSTRUMENT";
    case FeatureCategory::INFRA: return "INFRA";
    case FeatureCategory::RUNTIME: return "RUNTIME";
    }
    return "UNKNOWN";
}

namespace {

// Static registry. std::string keys so heterogeneous lookup via string_view
// is well-defined across all supported compilers (C++17 unordered_map does
// not guarantee heterogeneous lookup without transparent hash + C++20).
const std::unordered_map<std::string, FeatureCategory>& registry() {
    static const std::unordered_map<std::string, FeatureCategory> kRegistry = {
        // ---- LLVM passes (topo-llvm/lib/Transforms/) ----
        {"DataLayoutPass", FeatureCategory::ENHANCE},
        {"IndirectionPass", FeatureCategory::OPT},
        {"TopoParallelPass", FeatureCategory::COVERED},
        {"LifetimeArenaPass", FeatureCategory::OPT},
        {"LoopParallelizePass", FeatureCategory::COVERED},
        {"ReturnSpecializationPass", FeatureCategory::OPT},
        {"PrefetchPass", FeatureCategory::COVERED},
        {"ContainmentInterceptionPass", FeatureCategory::ENHANCE},
        {"ObservabilityPass", FeatureCategory::INSTRUMENT},
        {"SymbolObfuscator", FeatureCategory::INSTRUMENT},
        {"AdaptiveDispatchPass", FeatureCategory::RUNTIME},
        {"TopoInlinePass", FeatureCategory::INFRA},
        {"TopoFlattenPass", FeatureCategory::INFRA},
        {"TopoReorderPass", FeatureCategory::INFRA},
        {"TopoLayoutPass", FeatureCategory::INFRA},
        {"PipelineCodeGenPass", FeatureCategory::INFRA},
        {"PassFiredMarker", FeatureCategory::INFRA},

        // ---- JVM passes (topo-jvm/transform/src/main/java/dev/topo/transform/pass/) ----
        // Disambiguated with "Jvm" prefix where the bare name collides with an LLVM pass.
        // The JVM pass class is named ParallelPass etc.; we use JvmParallelPass as the
        // registry key so callers disambiguate which backend they mean.
        {"JvmParallelPass", FeatureCategory::ENHANCE},
        {"JvmLoopVectorizePass", FeatureCategory::COVERED},
        {"JvmPipelinePass", FeatureCategory::ENHANCE},
        {"JvmDataLayoutPass", FeatureCategory::COVERED},
        {"JvmTypeNarrowingPass", FeatureCategory::COVERED},
        {"JvmVisibilityPass", FeatureCategory::COVERED},
        {"JvmPrefetchPass", FeatureCategory::ENHANCE},
        {"JvmReturnSpecializationPass", FeatureCategory::COVERED},
        {"JvmArenaPass", FeatureCategory::ENHANCE},
        {"JvmObservabilityPass", FeatureCategory::INSTRUMENT},
        {"JvmObfuscationPass", FeatureCategory::INSTRUMENT},
        {"JvmAdaptivePass", FeatureCategory::RUNTIME},
        {"JvmStageReorderPass", FeatureCategory::INFRA},
        {"JvmInlineHintPass", FeatureCategory::INFRA},
        {"JvmStaticPromotionPass", FeatureCategory::INFRA},
    };
    return kRegistry;
}

} // namespace

std::optional<FeatureCategory> categoryOf(std::string_view passName) {
    const auto& tbl = registry();
    auto it = tbl.find(std::string(passName));
    if (it == tbl.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace topo
