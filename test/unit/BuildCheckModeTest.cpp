#include <gtest/gtest.h>

#include "topo/Build/BuildConfig.h"

using namespace topo;        // FeatureMode + the per-feature config structs
using namespace topo::build; // BuildConfig, CheckMode

// The optimization license rests on checked declarations: under CheckMode::Auto,
// topo-check must run exactly when a declaration-consuming optimization is
// enabled — and plain builds must stay unchecked (and fast).

TEST(BuildCheckModeTest, PlainAutoBuildDoesNotCheck) {
    BuildConfig cfg; // defaults: Auto; parallel/loopParallel/lifetime Off; pipeline Auto
    EXPECT_FALSE(cfg.consumesDeclarationsForOptimization());
    EXPECT_FALSE(cfg.shouldRunCheck());
}

TEST(BuildCheckModeTest, EnabledOptimizationsTriggerAutoCheck) {
    for (FeatureMode m : {FeatureMode::Auto, FeatureMode::Force}) {
        { BuildConfig c; c.parallelCfg.mode = m;     EXPECT_TRUE(c.shouldRunCheck()); }
        { BuildConfig c; c.loopParallelCfg.mode = m; EXPECT_TRUE(c.shouldRunCheck()); }
        { BuildConfig c; c.lifetimeCfg.mode = m;     EXPECT_TRUE(c.shouldRunCheck()); }
    }
}

TEST(BuildCheckModeTest, DefaultAutoPipelineAloneDoesNotTrigger) {
    BuildConfig cfg;
    ASSERT_EQ(cfg.pipelineCfg.mode, FeatureMode::Auto); // the default
    EXPECT_FALSE(cfg.shouldRunCheck());                 // must not flip plain builds
}

TEST(BuildCheckModeTest, ForcedPipelineTriggers) {
    BuildConfig cfg;
    cfg.pipelineCfg.mode = FeatureMode::Force;
    EXPECT_TRUE(cfg.shouldRunCheck());
}

TEST(BuildCheckModeTest, ExplicitOnAlwaysChecks) {
    BuildConfig cfg;
    cfg.checkMode = CheckMode::On;
    EXPECT_TRUE(cfg.shouldRunCheck());
}

TEST(BuildCheckModeTest, ExplicitOffSkipsButStillReportsOptimizing) {
    // [build].check=off is an escape hatch: check is skipped, but the build is
    // still optimizing — the build driver uses consumesDeclarationsForOptimization()
    // to warn loudly in this case.
    BuildConfig cfg;
    cfg.checkMode = CheckMode::Off;
    cfg.parallelCfg.mode = FeatureMode::Force;
    EXPECT_FALSE(cfg.shouldRunCheck());
    EXPECT_TRUE(cfg.consumesDeclarationsForOptimization());
}

TEST(BuildCheckModeTest, CliOverrideDominates) {
    { // --check forces on even with [build].check=off and no optimization
        BuildConfig c; c.checkMode = CheckMode::Off; c.checkCliOverride = true;
        EXPECT_TRUE(c.shouldRunCheck());
    }
    { // --no-check forces off even when optimizing
        BuildConfig c; c.parallelCfg.mode = FeatureMode::Force; c.checkCliOverride = false;
        EXPECT_FALSE(c.shouldRunCheck());
    }
}
