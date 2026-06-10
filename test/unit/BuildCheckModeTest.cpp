#include <gtest/gtest.h>

#include "topo/Build/BuildConfig.h"

using namespace topo;        // FeatureMode + the per-feature config structs
using namespace topo::build; // BuildConfig, CheckMode

// Check is on by default: under CheckMode::Auto, topo-check runs on EVERY
// build — plain and optimized alike. The only unchecked builds are explicit
// opt-outs (--no-check / [build].check = "off"), and an opt-out that skips
// validation an enabled optimization relies on is graded separately via
// consumesDeclarationsForOptimization() (the build driver's loud-warning
// input).

TEST(BuildCheckModeTest, PlainAutoBuildChecks) {
    BuildConfig cfg; // defaults: Auto; parallel/loopParallel/lifetime Off; pipeline Auto
    EXPECT_FALSE(cfg.consumesDeclarationsForOptimization());
    EXPECT_TRUE(cfg.shouldRunCheck()); // every build checks by default
}

TEST(BuildCheckModeTest, OptimizedAutoBuildChecks) {
    for (FeatureMode m : {FeatureMode::Auto, FeatureMode::Force}) {
        { BuildConfig c; c.parallelCfg.mode = m;     EXPECT_TRUE(c.shouldRunCheck()); }
        { BuildConfig c; c.loopParallelCfg.mode = m; EXPECT_TRUE(c.shouldRunCheck()); }
        { BuildConfig c; c.lifetimeCfg.mode = m;     EXPECT_TRUE(c.shouldRunCheck()); }
    }
}

TEST(BuildCheckModeTest, ExplicitOnAlwaysChecks) {
    BuildConfig cfg;
    cfg.checkMode = CheckMode::On; // explicit pin; same behavior as Auto today
    EXPECT_TRUE(cfg.shouldRunCheck());
}

TEST(BuildCheckModeTest, ExplicitOffSkipsPlainBuildQuietly) {
    // [build].check=off on a plain build is a quiet escape hatch: check is
    // skipped and no optimization consumes declarations, so the build driver
    // has nothing to warn about.
    BuildConfig cfg;
    cfg.checkMode = CheckMode::Off;
    EXPECT_FALSE(cfg.shouldRunCheck());
    EXPECT_FALSE(cfg.consumesDeclarationsForOptimization());
}

TEST(BuildCheckModeTest, ExplicitOffSkipsButStillReportsOptimizing) {
    // [build].check=off while optimizing: check is skipped, but the build is
    // consuming declarations it never validated — the build driver uses
    // consumesDeclarationsForOptimization() to warn loudly (UNVERIFIED) in
    // exactly this combination.
    BuildConfig cfg;
    cfg.checkMode = CheckMode::Off;
    cfg.parallelCfg.mode = FeatureMode::Force;
    EXPECT_FALSE(cfg.shouldRunCheck());
    EXPECT_TRUE(cfg.consumesDeclarationsForOptimization());
}

TEST(BuildCheckModeTest, DefaultAutoPipelineDoesNotCountAsOptimizing) {
    // pipeline defaults to Auto (a no-op unless a pipeline block exists), so
    // the default must not register as a declaration-consuming optimization —
    // otherwise every plain build that opts out of check would trip the loud
    // UNVERIFIED warning.
    BuildConfig cfg;
    ASSERT_EQ(cfg.pipelineCfg.mode, FeatureMode::Auto); // the default
    EXPECT_FALSE(cfg.consumesDeclarationsForOptimization());
}

TEST(BuildCheckModeTest, ForcedPipelineCountsAsOptimizing) {
    BuildConfig cfg;
    cfg.pipelineCfg.mode = FeatureMode::Force;
    EXPECT_TRUE(cfg.shouldRunCheck());
    EXPECT_TRUE(cfg.consumesDeclarationsForOptimization());
}

TEST(BuildCheckModeTest, CliOverrideDominates) {
    { // --check forces on even with [build].check=off
        BuildConfig c; c.checkMode = CheckMode::Off; c.checkCliOverride = true;
        EXPECT_TRUE(c.shouldRunCheck());
    }
    { // --no-check forces off even when optimizing (the warning path's input,
      // consumesDeclarationsForOptimization(), stays true independently)
        BuildConfig c; c.parallelCfg.mode = FeatureMode::Force; c.checkCliOverride = false;
        EXPECT_FALSE(c.shouldRunCheck());
        EXPECT_TRUE(c.consumesDeclarationsForOptimization());
    }
    { // --no-check also silences the default-on plain build
        BuildConfig c; c.checkCliOverride = false;
        EXPECT_FALSE(c.shouldRunCheck());
        EXPECT_FALSE(c.consumesDeclarationsForOptimization());
    }
}
