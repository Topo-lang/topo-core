#include "topo/Build/IncrementalCache.h"
#include "topo/Build/BuildConfig.h"
#include "topo/Build/ExecutionEvidence.h"
#include "topo/Platform/ToolResolution.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <fstream>

namespace {
namespace fs = std::filesystem;
namespace ev = topo::build::evidence;

class IncrementalEvidenceCacheTest : public ::testing::Test {
protected:
    fs::path dir;
    void SetUp() override {
        dir = fs::temp_directory_path() / ("topo-cache-evidence-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(fs::create_directory(dir));
        std::ofstream(dir / "input.txt") << "observed input\n";
        topo::build::IncrementalCache(dir).ensureDirectories();
    }
    void TearDown() override { std::error_code ec; fs::remove_all(dir, ec); }
    ev::RootMap roots() const { return {{"project", dir}}; }
    ev::Manifest observe() {
        const auto cmake = topo::platform::findSpawnableOnPath("cmake");
        EXPECT_FALSE(cmake.empty());
        ev::ExecutionRecorder recorder(roots(), dir / "commands.json", "actual-local-process");
        const auto result = recorder.run("copy", cmake,
            {"-E", "copy", (dir / "input.txt").string(), (dir / "output.txt").string()},
            {(dir / "input.txt").string()}, {(dir / "output.txt").string()});
        EXPECT_EQ(result.exitCode, 0);
        EXPECT_TRUE(recorder.diagnostics().empty());
        return recorder.manifest();
    }
    void save(const ev::Manifest& manifest) {
        std::string error;
        ASSERT_TRUE(topo::build::IncrementalCache(dir).saveEvidenceManifest(manifest, error)) << error;
    }
};

TEST_F(IncrementalEvidenceCacheTest, OrdinaryV3CacheRemainsUsableWithoutEvidence) {
    topo::build::IncrementalCache cache(dir);
    topo::build::BuildConfig cfg;
    std::ofstream(dir / "input.topo") << "// legacy frontend input\n";
    std::ofstream(dir / "Topo.toml") << "[build]\n";
    const std::vector<std::string> paths{(dir / "input.topo").generic_string()};
    topo::build::CacheManifest original;
    original.configFingerprint = cache.computeConfigFingerprint(cfg);
    original.topoFileMtimes[paths.front()] = cache.getFileMtime(paths.front());
    original.topoTomlMtime = cache.getFileMtime(dir / "Topo.toml");
    ASSERT_TRUE(cache.saveManifest(original));
    topo::build::CacheManifest loaded;
    ASSERT_TRUE(cache.loadManifest(loaded));
    EXPECT_TRUE(cache.isCompileConfigValid(loaded, cfg));
    EXPECT_TRUE(cache.isTopoFrontendValid(loaded, paths));
    const auto strict = cache.verifyEvidence({}, roots());
    EXPECT_FALSE(strict.integrity);
    EXPECT_FALSE(strict.complete);
    EXPECT_FALSE(strict.buildEvidence);
    ASSERT_FALSE(strict.diagnostics.empty());
    EXPECT_NE(strict.diagnostics.front().find("unavailable"), std::string::npos);
}

TEST_F(IncrementalEvidenceCacheTest, ActualObservedProcessRemainsAnExplicitStrictMiss) {
    const auto manifest = observe(); ASSERT_EQ(manifest.nodes.size(), 1u); save(manifest);
    const auto start = std::chrono::steady_clock::now();
    const auto verified = topo::build::IncrementalCache(dir).verifyEvidence(manifest.nodes.front().action, roots());
    RecordProperty("verify_us", std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count());
    RecordProperty("evidence_bytes", fs::file_size(topo::build::IncrementalCache(dir).cacheDir() / "evidence.json"));
    EXPECT_TRUE(verified.integrity);
    EXPECT_FALSE(verified.complete);
    EXPECT_FALSE(verified.signerIdentity);
    EXPECT_FALSE(verified.buildEvidence);
    EXPECT_FALSE(verified.rebuildMatch);
    ev::Manifest reread; std::string error;
    ASSERT_TRUE(topo::build::IncrementalCache(dir).loadEvidenceManifest(reread, error)) << error;
    EXPECT_EQ(ev::manifestDigest(manifest), ev::manifestDigest(reread));
    EXPECT_EQ(reread.nodes.front().producer.at("builder"), "actual-local-process");
}

TEST_F(IncrementalEvidenceCacheTest, IncompleteCoverageStillChecksRequestedActionIdentity) {
    const auto manifest = observe(); ASSERT_EQ(manifest.nodes.size(), 1u); save(manifest);
    auto expected = manifest.nodes.front().action;
    expected.argv.push_back("changed-configuration");
    const auto verified = topo::build::IncrementalCache(dir).verifyEvidence(expected, roots());
    EXPECT_FALSE(verified.integrity);
    EXPECT_FALSE(verified.complete);
    EXPECT_NE(std::find(verified.diagnostics.begin(), verified.diagnostics.end(),
                       "cache action identity mismatch"), verified.diagnostics.end());
}

TEST_F(IncrementalEvidenceCacheTest, IncompleteCoverageStillRequiresOneActionRoot) {
    auto manifest = observe(); ASSERT_EQ(manifest.nodes.size(), 1u);
    auto second = manifest.nodes.front(); second.action.kind = "independent-copy";
    ev::sealRecord(second); manifest.nodes.push_back(second); manifest.roots.push_back(second.id); save(manifest);
    const auto verified = topo::build::IncrementalCache(dir).verifyEvidence(manifest.nodes.front().action, roots());
    EXPECT_FALSE(verified.integrity);
    EXPECT_FALSE(verified.complete);
    EXPECT_NE(std::find(verified.diagnostics.begin(), verified.diagnostics.end(),
                       "cache evidence requires exactly one action root"), verified.diagnostics.end());
}

TEST_F(IncrementalEvidenceCacheTest, SourceChangePreservingMtimeInvalidatesObservedContent) {
    const auto manifest = observe(); ASSERT_EQ(manifest.nodes.size(), 1u); save(manifest);
    const auto originalTime = fs::last_write_time(dir / "input.txt");
    std::ofstream(dir / "input.txt") << "modified input\n";
    fs::last_write_time(dir / "input.txt", originalTime);
    const auto verified = topo::build::IncrementalCache(dir).verifyEvidence(manifest.nodes.front().action, roots());
    EXPECT_FALSE(verified.integrity);
    EXPECT_FALSE(verified.buildEvidence);
}

TEST_F(IncrementalEvidenceCacheTest, ReplacedArtifactInvalidatesObservedContent) {
    const auto manifest = observe(); ASSERT_EQ(manifest.nodes.size(), 1u); save(manifest);
    std::ofstream(dir / "output.txt") << "substituted output\n";
    const auto verified = topo::build::IncrementalCache(dir).verifyEvidence(manifest.nodes.front().action, roots());
    EXPECT_FALSE(verified.integrity);
    EXPECT_FALSE(verified.buildEvidence);
}

TEST_F(IncrementalEvidenceCacheTest, ResealedSelfReportedProducerCannotPromoteBuildProvenance) {
    auto manifest = observe(); ASSERT_EQ(manifest.nodes.size(), 1u);
    auto& record = manifest.nodes.front();
    record.complete = true;
    record.producer["builder"] = "trusted-builder";
    record.producer["isolation"] = "complete";
    ev::sealRecord(record); manifest.roots = {record.id}; save(manifest);
    const auto verified = topo::build::IncrementalCache(dir).verifyEvidence(record.action, roots());
    EXPECT_TRUE(verified.integrity);  // Consistent forged content is still self-reported.
    EXPECT_TRUE(verified.complete);
    EXPECT_FALSE(verified.signerIdentity);
    EXPECT_FALSE(verified.buildEvidence);
    EXPECT_FALSE(verified.rebuildMatch);
}
} // namespace
