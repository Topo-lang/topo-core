// Tests for PassCategoryRegistry.
//
// The registry is the code-side mirror of the project's feature-taxonomy
// principle: six categories (OPT / ENHANCE / COVERED / INSTRUMENT / INFRA /
// RUNTIME) classify every pass. Every expectation in this file must match
// that taxonomy; if the taxonomy moves, so does this file.

#include "topo/Build/PassCategoryRegistry.h"

#include <gtest/gtest.h>

using topo::categoryOf;
using topo::FeatureCategory;
using topo::toString;

namespace {

// --- toString ---

TEST(PassCategoryRegistry, ToStringCoversAllCategories) {
    EXPECT_STREQ(toString(FeatureCategory::OPT), "OPT");
    EXPECT_STREQ(toString(FeatureCategory::ENHANCE), "ENHANCE");
    EXPECT_STREQ(toString(FeatureCategory::COVERED), "COVERED");
    EXPECT_STREQ(toString(FeatureCategory::INSTRUMENT), "INSTRUMENT");
    EXPECT_STREQ(toString(FeatureCategory::INFRA), "INFRA");
    EXPECT_STREQ(toString(FeatureCategory::RUNTIME), "RUNTIME");
}

// --- Unknown passes ---

TEST(PassCategoryRegistry, UnknownPassReturnsNullopt) {
    EXPECT_FALSE(categoryOf("NotARealPass").has_value());
    EXPECT_FALSE(categoryOf("").has_value());
    EXPECT_FALSE(categoryOf("datalayoutpass").has_value()); // case sensitive
}

// --- LLVM passes (17) ---

TEST(PassCategoryRegistry, LlvmOptPasses) {
    // Per principle §5 footnote: only IndirectionPass / LifetimeArenaPass /
    // ReturnSpecializationPass satisfy all three OPT entry criteria.
    EXPECT_EQ(categoryOf("IndirectionPass"), FeatureCategory::OPT);
    EXPECT_EQ(categoryOf("LifetimeArenaPass"), FeatureCategory::OPT);
    EXPECT_EQ(categoryOf("ReturnSpecializationPass"), FeatureCategory::OPT);
}

TEST(PassCategoryRegistry, LlvmCoveredPasses) {
    EXPECT_EQ(categoryOf("PrefetchPass"), FeatureCategory::COVERED);
    // LoopParallelizePass: OPT → COVERED per archived proposal 15 —
    // LLVM O2 LoopVectorize already covers streaming loops; the pass's
    // per-function parallel-stage path is not exercised by the current
    // friendly workload (single function per stage).
    EXPECT_EQ(categoryOf("LoopParallelizePass"), FeatureCategory::COVERED);
    // TopoParallelPass: OPT → COVERED per Plan 34. Parallel speedup =
    // task-grain × cores × structure; Topo cannot guarantee any friendly
    // workload `forced/base ≤ 0.90`, so OPT entry criteria fail.
    EXPECT_EQ(categoryOf("TopoParallelPass"), FeatureCategory::COVERED);
}

TEST(PassCategoryRegistry, LlvmEnhancePasses) {
    EXPECT_EQ(categoryOf("ContainmentInterceptionPass"), FeatureCategory::ENHANCE);
    // DataLayoutPass: OPT → ENHANCE per Plan 34. Pass infers stride /
    // contiguous-ratio inside the pass itself, not declaration-driven —
    // fails OPT §5.4 rule 3 ("no in-pass cost heuristics").
    EXPECT_EQ(categoryOf("DataLayoutPass"), FeatureCategory::ENHANCE);
}

TEST(PassCategoryRegistry, LlvmInstrumentPasses) {
    EXPECT_EQ(categoryOf("ObservabilityPass"), FeatureCategory::INSTRUMENT);
    EXPECT_EQ(categoryOf("SymbolObfuscator"), FeatureCategory::INSTRUMENT);
}

TEST(PassCategoryRegistry, LlvmRuntimePasses) {
    EXPECT_EQ(categoryOf("AdaptiveDispatchPass"), FeatureCategory::RUNTIME);
}

TEST(PassCategoryRegistry, LlvmInfraPasses) {
    EXPECT_EQ(categoryOf("TopoInlinePass"), FeatureCategory::INFRA);
    EXPECT_EQ(categoryOf("TopoFlattenPass"), FeatureCategory::INFRA);
    EXPECT_EQ(categoryOf("TopoReorderPass"), FeatureCategory::INFRA);
    EXPECT_EQ(categoryOf("TopoLayoutPass"), FeatureCategory::INFRA);
    EXPECT_EQ(categoryOf("PipelineCodeGenPass"), FeatureCategory::INFRA);
    EXPECT_EQ(categoryOf("PassFiredMarker"), FeatureCategory::INFRA);
}

// --- JVM passes (15) ---
//
// Keys are prefixed with "Jvm" to disambiguate from LLVM passes with the
// same class name (DataLayoutPass, ReturnSpecializationPass, ObservabilityPass,
// PrefetchPass). The class names themselves in `topo-jvm/transform/.../pass/`
// are bare ("ParallelPass", "DataLayoutPass", etc.).

// No JVM passes are currently classified OPT — per Plan 28 Phase 6,
// declaration-class JVM passes (ParallelPass / PipelinePass) were demoted
// to ENHANCE because speedup depends on task-grain × cores × structure,
// violating §5.4 rule 2 ("no runtime-data dependency").

TEST(PassCategoryRegistry, JvmCoveredPasses) {
    EXPECT_EQ(categoryOf("JvmDataLayoutPass"), FeatureCategory::COVERED);
    EXPECT_EQ(categoryOf("JvmTypeNarrowingPass"), FeatureCategory::COVERED);
    EXPECT_EQ(categoryOf("JvmVisibilityPass"), FeatureCategory::COVERED);
    EXPECT_EQ(categoryOf("JvmReturnSpecializationPass"), FeatureCategory::COVERED);
    // JvmLoopVectorizePass: OPT → COVERED per Plan 28 Phase 6. HotSpot C2
    // auto-vectorizer covers streaming `scaleAndAdd`/`reduceSum`; gather
    // forced/vanilla ≈ 0.905 sits in COVERED [0.90, 1.10] WARN band, not
    // OPT ≤ 0.90.
    EXPECT_EQ(categoryOf("JvmLoopVectorizePass"), FeatureCategory::COVERED);
}

TEST(PassCategoryRegistry, JvmEnhancePasses) {
    EXPECT_EQ(categoryOf("JvmArenaPass"), FeatureCategory::ENHANCE);
    // JvmPrefetchPass: COVERED → ENHANCE per archived proposal 17 —
    // HotSpot has no public software-prefetch intrinsic; declaration-class
    // witness for `access(streaming)` only.
    EXPECT_EQ(categoryOf("JvmPrefetchPass"), FeatureCategory::ENHANCE);
    // JvmParallelPass / JvmPipelinePass: OPT → ENHANCE per Plan 28 Phase 6.
    // declaration-class — stage<N> / pipeline materialized as ForkJoinPool
    // / CompletableFuture bytecode witness; speedup is a side-effect of
    // declaration-correct materialization, not a contract.
    EXPECT_EQ(categoryOf("JvmParallelPass"), FeatureCategory::ENHANCE);
    EXPECT_EQ(categoryOf("JvmPipelinePass"), FeatureCategory::ENHANCE);
}

TEST(PassCategoryRegistry, JvmInstrumentPasses) {
    EXPECT_EQ(categoryOf("JvmObservabilityPass"), FeatureCategory::INSTRUMENT);
    EXPECT_EQ(categoryOf("JvmObfuscationPass"), FeatureCategory::INSTRUMENT);
}

TEST(PassCategoryRegistry, JvmRuntimePasses) {
    EXPECT_EQ(categoryOf("JvmAdaptivePass"), FeatureCategory::RUNTIME);
}

TEST(PassCategoryRegistry, JvmInfraPasses) {
    EXPECT_EQ(categoryOf("JvmStageReorderPass"), FeatureCategory::INFRA);
    EXPECT_EQ(categoryOf("JvmInlineHintPass"), FeatureCategory::INFRA);
    EXPECT_EQ(categoryOf("JvmStaticPromotionPass"), FeatureCategory::INFRA);
}

} // namespace
