#include "topo/Build/ArtifactEvidence.h"
#include "topo/Distribution/Sha256.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <fstream>

namespace {
namespace ev = topo::build::evidence;
namespace fs = std::filesystem;

class ArtifactEvidenceTest : public ::testing::Test {
protected:
    fs::path dir;
    void SetUp() override {
        dir = fs::temp_directory_path() / ("topo-evidence-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(fs::create_directory(dir));
        write("source.cpp", "int main() { return 0; }\n");
        write("output.bin", "frozen output");
        write("facts.json", "{\"passed\":true,\"coverage\":\"limited\"}");
    }
    void TearDown() override { std::error_code ec; fs::remove_all(dir, ec); }
    void write(const std::string& path, const std::string& data) {
        fs::create_directories((dir / path).parent_path());
        std::ofstream out(dir / path, std::ios::binary); out << data;
    }
    ev::FileEntry file(const std::string& path) {
        ev::FileEntry entry; std::string error;
        EXPECT_TRUE(ev::snapshotFile(dir, "project", path, entry, error)) << error;
        return entry;
    }
    ev::Record record() {
        ev::Record r;
        r.action.kind = "compile"; r.action.version = "1";
        r.action.toolIdentity = "sha256:" + std::string(64, 'a');
        r.action.argv = {"clang++", "-Iinclude/a", "-Iinclude/b", "source.cpp"};
        r.action.cwd = "project:."; r.action.inputs = {file("source.cpp")};
        r.outputs = {file("output.bin")}; r.metadata = {file("facts.json")};
        r.producer = {{"builder", "local"}, {"run", "original-run"}};
        r.complete = true; ev::sealRecord(r); return r;
    }
    ev::Manifest manifest() { auto r = record(); return {ev::SchemaVersion, {r}, {r.id}}; }
    ev::RootMap roots() { return {{"project", dir}}; }
};

TEST_F(ArtifactEvidenceTest, RoundTripLeavesArtifactBytesUnchanged) {
    auto m = manifest(); const auto before = topo::dist::sha256File((dir / "output.bin").string());
    std::string error; ASSERT_TRUE(ev::writeManifest(dir / "evidence.json", m, error)) << error;
    ev::Manifest loaded; ASSERT_TRUE(ev::readManifest(dir / "evidence.json", loaded, error)) << error;
    EXPECT_EQ(ev::manifestDigest(m), ev::manifestDigest(loaded));
    auto verified = ev::verifyManifest(loaded, roots());
    EXPECT_TRUE(verified.integrity); EXPECT_TRUE(verified.complete);
    EXPECT_FALSE(verified.signerIdentity); EXPECT_FALSE(verified.buildEvidence); EXPECT_FALSE(verified.rebuildMatch);
    EXPECT_EQ(before, topo::dist::sha256File((dir / "output.bin").string()));
}

TEST_F(ArtifactEvidenceTest, OrderedFlagsAndToolVersionChangeActionIdentity) {
    auto a = record().action; auto b = a;
    std::swap(b.argv[1], b.argv[2]); EXPECT_NE(ev::actionDigest(a), ev::actionDigest(b));
    b = a; b.toolIdentity += "-new"; EXPECT_NE(ev::actionDigest(a), ev::actionDigest(b));
    b = a; b.version = "2"; EXPECT_NE(ev::actionDigest(a), ev::actionDigest(b));
    b = a; b.environment["CPATH"] = "other"; EXPECT_NE(ev::actionDigest(a), ev::actionDigest(b));
}

TEST_F(ArtifactEvidenceTest, InputSetOrderingAndJsonMapOrderingAreCanonical) {
    auto a = record().action; a.inputs.push_back(file("facts.json"));
    a.parameters = {{"one", 1}, {"two", "2"}};
    auto b = a; std::reverse(b.inputs.begin(), b.inputs.end());
    b.parameters = {{"two", "2"}, {"one", 1}};
    EXPECT_EQ(ev::actionDigest(a), ev::actionDigest(b));
    b.parameters["float"] = 0.1; EXPECT_TRUE(ev::actionDigest(b).empty());
}

TEST_F(ArtifactEvidenceTest, SourceTamperingPreservingMtimeRejectsCache) {
    auto r = record(); auto time = fs::last_write_time(dir / "source.cpp");
    write("source.cpp", "int main() { return 1; }\n"); fs::last_write_time(dir / "source.cpp", time);
    EXPECT_FALSE(ev::verifyCacheReuse(r, r.action, roots()).integrity);
}

TEST_F(ArtifactEvidenceTest, OutputAndMetadataTamperingRejectCache) {
    auto r = record(); write("output.bin", "replaced cache");
    EXPECT_FALSE(ev::verifyCacheReuse(r, r.action, roots()).integrity);
    write("output.bin", "frozen output"); write("facts.json", "{\"passed\":false}");
    EXPECT_FALSE(ev::verifyCacheReuse(r, r.action, roots()).integrity);
}

TEST_F(ArtifactEvidenceTest, CacheReuseRetainsOriginalProducerWithoutPromotingTrust) {
    auto r = record(); auto id = r.id;
    auto verified = ev::verifyCacheReuse(r, r.action, roots());
    EXPECT_TRUE(verified.integrity); EXPECT_TRUE(verified.complete);
    EXPECT_EQ(r.id, id); EXPECT_EQ(r.producer.at("run"), "original-run");
    EXPECT_FALSE(verified.signerIdentity); EXPECT_FALSE(verified.buildEvidence);
    auto different = r.action; different.version = "2";
    EXPECT_FALSE(ev::verifyCacheReuse(r, different, roots()).integrity);
}

TEST_F(ArtifactEvidenceTest, MissingAndIncompleteEvidenceAreNotReusable) {
    auto r = record(); r.complete = false; ev::sealRecord(r);
    EXPECT_FALSE(ev::verifyCacheReuse(r, r.action, roots()).integrity);
    ev::Manifest loaded; std::string error;
    EXPECT_FALSE(ev::readManifest(dir / "legacy-cache", loaded, error));
    EXPECT_FALSE(error.empty());
}

TEST_F(ArtifactEvidenceTest, UnapprovedRootAndTraversalAreRejected) {
    ev::FileEntry out; std::string error;
    for (const auto& path : {"../output.bin", "/etc/hosts", "a/../output.bin", "a//b", "C:foo", "a\\b"})
        EXPECT_FALSE(ev::snapshotFile(dir, "project", path, out, error)) << path;
    auto m = manifest(); EXPECT_FALSE(ev::verifyManifest(m, {}).integrity);
}

TEST_F(ArtifactEvidenceTest, MissingDirectoryAndUnreadableKindsNeverHashAsEmptyContent) {
    std::string error; ev::FileEntry out;
    EXPECT_FALSE(ev::snapshotFile(dir, "project", "absent", out, error));
    fs::create_directory(dir / "directory");
    EXPECT_FALSE(ev::snapshotFile(dir, "project", "directory", out, error));
    EXPECT_TRUE(topo::dist::sha256File((dir / "directory").string()).empty());
    EXPECT_TRUE(topo::dist::sha256File((dir / "missing").string()).empty());
}

TEST_F(ArtifactEvidenceTest, SymlinkTargetIdentityAndTextAreVerified) {
    std::error_code ec; fs::create_symlink("output.bin", dir / "link", ec);
    if (ec) GTEST_SKIP() << "symlink creation unavailable: " << ec.message();
    auto f = file("link"); EXPECT_EQ(f.kind, "symlink"); EXPECT_EQ(f.resolvedPath, "output.bin");
    write("same.bin", "frozen output"); fs::remove(dir / "link"); fs::create_symlink("same.bin", dir / "link");
    std::string error; EXPECT_FALSE(ev::verifyFile(f, roots(), error));
}

TEST_F(ArtifactEvidenceTest, EscapingCyclicAndIntermediateSymlinksAreRejected) {
    std::error_code ec; fs::create_symlink("../outside", dir / "escape", ec);
    if (ec) GTEST_SKIP() << "symlink creation unavailable: " << ec.message();
    ev::FileEntry out; std::string error;
    EXPECT_FALSE(ev::snapshotFile(dir, "project", "escape", out, error));
    fs::create_symlink("cycle", dir / "cycle");
    EXPECT_FALSE(ev::snapshotFile(dir, "project", "cycle", out, error));
    fs::create_directory(dir / "nested"); write("nested/data", "data");
    fs::create_directory_symlink("nested", dir / "alias");
    EXPECT_FALSE(ev::snapshotFile(dir, "project", "alias/data", out, error));
}

TEST_F(ArtifactEvidenceTest, ExecutablePermissionIsPartOfIdentity) {
#ifdef _WIN32
    GTEST_SKIP() << "Windows filesystem has no POSIX executable permission identity";
#else
    auto f = file("output.bin");
    fs::permissions(dir / "output.bin", fs::perms::owner_exec, fs::perm_options::add);
    std::string error; EXPECT_FALSE(ev::verifyFile(f, roots(), error));
#endif
}

TEST_F(ArtifactEvidenceTest, CaseCollidingPathsCannotBecomeEvidence) {
    auto r = record(); auto duplicate = r.action.inputs.front(); duplicate.path = "SOURCE.cpp";
    r.action.inputs.push_back(duplicate); EXPECT_TRUE(ev::recordDigest(r).empty());
}

TEST_F(ArtifactEvidenceTest, DagParentsAndRootsAreBoundByDigests) {
    auto first = record(); auto next = first; next.kind = "distribution";
    next.parents = {first.id}; next.producer["run"] = "publish-run"; ev::sealRecord(next);
    ev::Manifest m{ev::SchemaVersion, {first, next}, {next.id}};
    EXPECT_TRUE(ev::verifyManifest(m, roots()).integrity);
    auto rootDigest = ev::manifestDigest(m); std::reverse(m.nodes.begin(), m.nodes.end());
    EXPECT_EQ(rootDigest, ev::manifestDigest(m));
    m.nodes = {next}; EXPECT_FALSE(ev::verifyManifest(m, roots()).integrity);
}

TEST_F(ArtifactEvidenceTest, OrphanAndForgedRecordsAreRejected) {
    auto m = manifest(); auto orphan = record(); orphan.producer["run"] = "orphan"; ev::sealRecord(orphan);
    m.nodes.push_back(orphan); EXPECT_TRUE(ev::manifestDigest(m).empty());
    m = manifest(); m.nodes[0].complete = false;
    EXPECT_FALSE(ev::verifyManifest(m, roots()).integrity);
}

TEST_F(ArtifactEvidenceTest, StrictSchemaAndUnknownFieldsAreRejected) {
    auto j = ev::toJson(manifest()); ev::Manifest parsed; std::string error;
    j["schemaVersion"] = 999; EXPECT_FALSE(ev::fromJson(j, parsed, error));
    j = ev::toJson(manifest()); j["extra"] = true; EXPECT_FALSE(ev::fromJson(j, parsed, error));
    j = ev::toJson(manifest()); j["nodes"][0]["complete"] = "true"; EXPECT_FALSE(ev::fromJson(j, parsed, error));
    j = ev::toJson(manifest()); j["nodes"][0]["outputs"][0]["size"] = -1;
    EXPECT_FALSE(ev::fromJson(j, parsed, error));
}

TEST_F(ArtifactEvidenceTest, DuplicateFieldsAndSizeLimitsAreRejected) {
    auto text = ev::toJson(manifest()).dump(); text.insert(1, "\"type\":\"topo.artifact-evidence\",");
    write("duplicate.json", text); ev::Manifest parsed; std::string error;
    EXPECT_FALSE(ev::readManifest(dir / "duplicate.json", parsed, error));
    EXPECT_NE(error.find("duplicate"), std::string::npos);
    write("valid.json", ev::toJson(manifest()).dump());
    EXPECT_FALSE(ev::readManifest(dir / "valid.json", parsed, error, 10));
}

TEST_F(ArtifactEvidenceTest, Sha256KnownVectorsAndStreaming) {
    EXPECT_EQ(topo::dist::sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(topo::dist::sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    topo::dist::Sha256 h; h.update("a"); h.update("bc");
    EXPECT_EQ(h.hexDigest(), topo::dist::sha256Hex("abc"));
}

} // namespace
