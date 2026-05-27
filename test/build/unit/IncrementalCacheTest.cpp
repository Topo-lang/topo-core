#include "topo/Build/IncrementalCache.h"
#include "topo/Build/SymbolTableJson.h"
#include "topo/Sema/SymbolTable.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace topo;
using namespace topo::build;

#ifdef _WIN32
#include <process.h>
static int topo_getpid() {
    return _getpid();
}
#else
#include <unistd.h>
static int topo_getpid() {
    return getpid();
}
#endif

// Helper: create a BuildConfig-like struct for testing computeConfigFingerprint.
// Since tests link TopoBuildConfig, BuildConfig is available.
#include "Config.h"

class IncrementalCacheTest : public ::testing::Test {
protected:
    fs::path testDir;

    void SetUp() override {
        testDir = fs::temp_directory_path() / ("topo-incremental-test_" + std::to_string(topo_getpid()));
        fs::create_directories(testDir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(testDir, ec);
    }
};

// --- Config fingerprint tests ---

TEST_F(IncrementalCacheTest, FingerprintStability) {
    BuildConfig cfg;
    cfg.hostCompilerPath = "/usr/bin/clang++";
    cfg.standard = "c++17";
    cfg.includeDirs = {"/inc/a", "/inc/b"};
    cfg.sources = {"src/main.cpp", "src/engine.cpp"};
    cfg.embedIR = false;
    cfg.outputType = OutputType::Exe;

    auto fp1 = IncrementalCache::computeConfigFingerprint(cfg);
    auto fp2 = IncrementalCache::computeConfigFingerprint(cfg);
    EXPECT_EQ(fp1, fp2) << "Same config should produce same fingerprint";
}

TEST_F(IncrementalCacheTest, FingerprintChangesWithCompiler) {
    BuildConfig cfg1;
    cfg1.hostCompilerPath = "/usr/bin/clang++";
    cfg1.standard = "c++17";
    cfg1.sources = {"src/main.cpp"};

    BuildConfig cfg2 = cfg1;
    cfg2.hostCompilerPath = "/opt/clang-16/bin/clang++";

    EXPECT_NE(IncrementalCache::computeConfigFingerprint(cfg1), IncrementalCache::computeConfigFingerprint(cfg2));
}

TEST_F(IncrementalCacheTest, FingerprintChangesWithSources) {
    BuildConfig cfg1;
    cfg1.hostCompilerPath = "clang++";
    cfg1.sources = {"src/main.cpp"};

    BuildConfig cfg2 = cfg1;
    cfg2.sources.push_back("src/engine.cpp");

    EXPECT_NE(IncrementalCache::computeConfigFingerprint(cfg1), IncrementalCache::computeConfigFingerprint(cfg2));
}

TEST_F(IncrementalCacheTest, FingerprintSourceOrderIndependent) {
    BuildConfig cfg1;
    cfg1.hostCompilerPath = "clang++";
    cfg1.sources = {"src/a.cpp", "src/b.cpp"};

    BuildConfig cfg2 = cfg1;
    cfg2.sources = {"src/b.cpp", "src/a.cpp"};

    // Sources are sorted before hashing, so order shouldn't matter
    EXPECT_EQ(IncrementalCache::computeConfigFingerprint(cfg1), IncrementalCache::computeConfigFingerprint(cfg2));
}

TEST_F(IncrementalCacheTest, FingerprintIgnoresOptLevel) {
    BuildConfig cfg1;
    cfg1.hostCompilerPath = "clang++";
    cfg1.sources = {"src/main.cpp"};
    cfg1.optLevel = OptLevel::O0;

    BuildConfig cfg2 = cfg1;
    cfg2.optLevel = OptLevel::O3;

    // optLevel only affects Step 6, not Step 3
    EXPECT_EQ(IncrementalCache::computeConfigFingerprint(cfg1), IncrementalCache::computeConfigFingerprint(cfg2));
}

// --- Manifest round-trip ---

TEST_F(IncrementalCacheTest, ManifestRoundTrip) {
    IncrementalCache cache(testDir);
    cache.ensureDirectories();

    CacheManifest m;
    m.version = 2;
    m.configFingerprint = "abc123";
    m.topoTomlMtime = 1234567890;
    m.topoFileMtimes["main.topo"] = 100;
    m.topoFileMtimes["engine.topo"] = 200;

    cache.saveManifest(m);

    CacheManifest loaded;
    ASSERT_TRUE(cache.loadManifest(loaded));
    EXPECT_EQ(loaded.version, 2);
    EXPECT_EQ(loaded.configFingerprint, "abc123");
    EXPECT_EQ(loaded.topoTomlMtime, 1234567890);
    EXPECT_EQ(loaded.topoFileMtimes.size(), 2u);
    EXPECT_EQ(loaded.topoFileMtimes["main.topo"], 100);
    EXPECT_EQ(loaded.topoFileMtimes["engine.topo"], 200);
}

TEST_F(IncrementalCacheTest, ManifestVersionMismatch) {
    IncrementalCache cache(testDir);
    cache.ensureDirectories();

    // Write a manifest with wrong version
    nlohmann::json j;
    j["version"] = 999;
    j["configFingerprint"] = "test";
    j["topoTomlMtime"] = 0;
    j["topoFileMtimes"] = nlohmann::json::object();

    fs::path manifestPath = cache.cacheDir() / "manifest.json";
    std::ofstream ofs(manifestPath);
    ofs << j.dump();
    ofs.close();

    CacheManifest loaded;
    EXPECT_FALSE(cache.loadManifest(loaded)) << "Version mismatch should cause load failure";
}

// --- SymbolTable JSON round-trip ---

TEST_F(IncrementalCacheTest, SymbolTableRoundTrip) {
    IncrementalCache cache(testDir);
    cache.ensureDirectories();

    SymbolTable original;

    // Add a function
    FunctionSymbol fn;
    fn.qualifiedName = "engine::core::init";
    fn.simpleName = "init";
    fn.visibility = Visibility::Public;
    fn.returnType.nameParts = {"void"};
    fn.params.push_back({TypeNode{false, OwnershipKind::None, {"int"}, TypeNode::None}, "x"});
    fn.isConst = false;
    fn.hasLogicBlock = true;
    original.addFunction(fn);

    // Add a logic block
    LogicBlockEntry lb;
    lb.qualifiedName = "engine::core::run";
    lb.simpleName = "run";
    lb.calledFunctions = {"engine::core::init"};
    lb.stages = {0};
    lb.isPipeline = false;
    original.addLogicBlock(lb);

    // Add a class
    ClassSymbol cls;
    cls.qualifiedName = "engine::core::Vector";
    cls.simpleName = "Vector";
    cls.visibility = Visibility::Public;
    cls.memberFunctions = {"engine::core::Vector::size"};
    cls.constructors = {"engine::core::Vector::Vector"};
    original.addClassSymbol(cls);

    // Add a type alias
    TypeAliasEntry alias;
    alias.name = "Int";
    alias.targetType.nameParts = {"std", "cpp17", "int32_t"};
    original.addTypeAlias(alias);

    // Save & load
    ASSERT_TRUE(cache.saveSymbolTable(original));

    SymbolTable loaded;
    ASSERT_TRUE(cache.loadSymbolTable(loaded));

    // Verify functions
    auto* loadedFn = loaded.findFunction("engine::core::init");
    ASSERT_NE(loadedFn, nullptr);
    EXPECT_EQ(loadedFn->simpleName, "init");
    EXPECT_EQ(loadedFn->visibility, Visibility::Public);
    EXPECT_EQ(loadedFn->params.size(), 1u);
    EXPECT_TRUE(loadedFn->hasLogicBlock);

    // Verify logic blocks
    auto* loadedLb = loaded.findLogicBlock("engine::core::run");
    ASSERT_NE(loadedLb, nullptr);
    EXPECT_EQ(loadedLb->calledFunctions.size(), 1u);

    // Verify classes
    auto* loadedCls = loaded.findClassSymbol("engine::core::Vector");
    ASSERT_NE(loadedCls, nullptr);
    EXPECT_EQ(loadedCls->memberFunctions.size(), 1u);

    // Verify type aliases
    auto* loadedAlias = loaded.findTypeAlias("Int");
    ASSERT_NE(loadedAlias, nullptr);
    EXPECT_EQ(loadedAlias->targetType.nameParts.size(), 3u);
}

// --- VisibilityEntry JSON round-trip ---

TEST_F(IncrementalCacheTest, VisibilityRoundTrip) {
    IncrementalCache cache(testDir);
    cache.ensureDirectories();

    std::vector<VisibilityEntry> original;

    VisibilityEntry e1;
    e1.qualifiedName = "engine::core::init";
    e1.visibility = Visibility::Public;
    e1.isConst = false;
    original.push_back(e1);

    VisibilityEntry e2;
    e2.qualifiedName = "engine::core::Vector::size";
    e2.visibility = Visibility::Protected;
    e2.isConst = true;
    ParamConstInfo pci;
    pci.isConst = true;
    pci.modifier = TypeNode::Ref;
    pci.ownership = OwnershipKind::Shared;
    e2.paramConsts.push_back(pci);
    original.push_back(e2);

    ASSERT_TRUE(cache.saveVisibilityEntries(original));

    std::vector<VisibilityEntry> loaded;
    ASSERT_TRUE(cache.loadVisibilityEntries(loaded));

    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].qualifiedName, "engine::core::init");
    EXPECT_EQ(loaded[0].visibility, Visibility::Public);
    EXPECT_EQ(loaded[1].qualifiedName, "engine::core::Vector::size");
    EXPECT_TRUE(loaded[1].isConst);
    ASSERT_EQ(loaded[1].paramConsts.size(), 1u);
    EXPECT_TRUE(loaded[1].paramConsts[0].isConst);
    EXPECT_EQ(loaded[1].paramConsts[0].modifier, TypeNode::Ref);
    EXPECT_EQ(loaded[1].paramConsts[0].ownership, OwnershipKind::Shared);
}

// --- Clean ---

TEST_F(IncrementalCacheTest, CleanRemovesCache) {
    IncrementalCache cache(testDir);
    cache.ensureDirectories();

    CacheManifest m;
    cache.saveManifest(m);

    ASSERT_TRUE(fs::exists(cache.cacheDir()));
    cache.clean();
    EXPECT_FALSE(fs::exists(cache.cacheDir()));
}

// --- mtime validity ---

TEST_F(IncrementalCacheTest, MtimeValidityDetectsChange) {
    IncrementalCache cache(testDir);
    cache.ensureDirectories();

    // Create a .topo file
    fs::path topoFile = testDir / "test.topo";
    {
        std::ofstream ofs(topoFile);
        ofs << "namespace test {}";
    }

    auto mtime = IncrementalCache::getFileMtime(topoFile);
    EXPECT_NE(mtime, 0);

    CacheManifest m;
    m.topoFileMtimes[topoFile.generic_string()] = mtime;

    // Should be valid
    EXPECT_TRUE(cache.isTopoFrontendValid(m, {topoFile.string()}));

    // Simulate a file change (wrong mtime)
    m.topoFileMtimes[topoFile.generic_string()] = mtime + 1;
    EXPECT_FALSE(cache.isTopoFrontendValid(m, {topoFile.string()}));
}

// --- Pipeline analysis round-trip ---

TEST_F(IncrementalCacheTest, PipelineAnalysisRoundTrip) {
    IncrementalCache cache(testDir);
    cache.ensureDirectories();

    SymbolTable original;

    LogicBlockEntry lb;
    lb.qualifiedName = "app::pipeline::process";
    lb.simpleName = "process";
    lb.isPipeline = true;

    PipelineEdge edge;
    edge.source = "parse";
    edge.target = "transform";
    lb.edges.push_back(edge);

    PipelineAnalysis analysis;
    analysis.stages["parse"] = 0;
    analysis.stages["transform"] = 1;
    analysis.sourceNodes = {"parse"};
    analysis.terminalNode = "transform";
    analysis.terminalType = "Result";
    analysis.demand["parse"] = {"data", "status"};
    lb.pipelineAnalysis = analysis;

    lb.calledFunctions = {"parse", "transform"};
    lb.stages = {0, 1};
    original.addLogicBlock(lb);

    ASSERT_TRUE(cache.saveSymbolTable(original));

    SymbolTable loaded;
    ASSERT_TRUE(cache.loadSymbolTable(loaded));

    auto* loadedLb = loaded.findLogicBlock("app::pipeline::process");
    ASSERT_NE(loadedLb, nullptr);
    EXPECT_TRUE(loadedLb->isPipeline);
    ASSERT_TRUE(loadedLb->pipelineAnalysis.has_value());
    EXPECT_EQ(loadedLb->pipelineAnalysis->stages.size(), 2u);
    EXPECT_EQ(loadedLb->pipelineAnalysis->terminalNode, "transform");
    EXPECT_EQ(loadedLb->pipelineAnalysis->demand.at("parse").size(), 2u);
}
