// Unit tests for the backend-distribution library (topo-core/lib/Distribution).
// Covers SHA-256 against FIPS 180-4 vectors, SemVer range matching, the
// topo-backend.toml manifest parser (including path-escape rejection), the
// registry index parser, and the transactional cache install/rollback path.

#include "topo/Distribution/BackendCache.h"
#include "topo/Distribution/BackendManifest.h"
#include "topo/Distribution/RegistryIndex.h"
#include "topo/Distribution/SemVer.h"
#include "topo/Distribution/Sha256.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace fs = std::filesystem;
using namespace topo::dist;

// ── SHA-256 ──────────────────────────────────────────────────────

TEST(Sha256Test, FipsVectors) {
    EXPECT_EQ(sha256Hex(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256Hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256Test, MillionA) {
    std::string s(1'000'000, 'a');
    EXPECT_EQ(sha256Hex(s),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256Test, IncrementalMatchesOneShot) {
    Sha256 h;
    h.update("hello ");
    h.update("world");
    EXPECT_EQ(h.hexDigest(), sha256Hex("hello world"));
}

// ── SemVer ───────────────────────────────────────────────────────

TEST(SemVerTest, ParseAndOrder) {
    SemVer a = parseSemVer("4.0.0");
    SemVer b = parseSemVer("4.1.2");
    ASSERT_TRUE(a.valid);
    ASSERT_TRUE(b.valid);
    EXPECT_LT(a.compareCore(b), 0);
    EXPECT_GT(b.compareCore(a), 0);
    EXPECT_EQ(a.compareCore(parseSemVer("4.0.0")), 0);
}

TEST(SemVerTest, RejectsMalformed) {
    EXPECT_FALSE(parseSemVer("4.0").valid);
    EXPECT_FALSE(parseSemVer("v4.0.0").valid);
    EXPECT_FALSE(parseSemVer("4.0.0.0").valid);
    EXPECT_FALSE(parseSemVer("").valid);
}

TEST(SemVerTest, RangeSatisfaction) {
    EXPECT_TRUE(satisfiesRange("4.0.0", ">=4.0.0, <5.0.0"));
    EXPECT_TRUE(satisfiesRange("4.9.9", ">=4.0.0, <5.0.0"));
    EXPECT_FALSE(satisfiesRange("5.0.0", ">=4.0.0, <5.0.0"));
    EXPECT_FALSE(satisfiesRange("3.9.0", ">=4.0.0, <5.0.0"));
    EXPECT_TRUE(satisfiesRange("4.2.0", ""));      // empty range matches all
    EXPECT_TRUE(satisfiesRange("4.2.0", "=4.2.0"));
    EXPECT_FALSE(satisfiesRange("4.2.1", "=4.2.0"));
}

TEST(SemVerTest, MalformedRangeFailsClosed) {
    EXPECT_FALSE(satisfiesRange("4.0.0", ">=garbage"));
}

// ── manifest parser ──────────────────────────────────────────────

namespace {
const char* kGoodManifest = R"(
[backend]
name        = "topo-jvm"
version     = "1.2.0"
language    = ["java"]
core_compat = ">=4.0.0, <5.0.0"
description = "JVM backend"

[binaries]
topo-build-jvm-java = "bin/topo-build-jvm-java"

[runtime.transform]
kind    = "jar"
files   = ["lib/topo-transform.jar"]
install = "lib"

[platform]
os   = "macos"
arch = "aarch64"

[signature]
algorithm = "ed25519"
key_id    = "deadbeef"
value     = "MEUCIQ"
)";
} // namespace

TEST(BackendManifestTest, ParsesValidManifest) {
    auto r = parseBackendManifest(kGoodManifest, "test");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.manifest.backend.name, "topo-jvm");
    EXPECT_EQ(r.manifest.backend.version, "1.2.0");
    ASSERT_EQ(r.manifest.backend.language.size(), 1u);
    EXPECT_EQ(r.manifest.backend.language[0], "java");
    EXPECT_EQ(r.manifest.platform.str(), "macos-aarch64");
    ASSERT_EQ(r.manifest.binaries.count("topo-build-jvm-java"), 1u);
    ASSERT_EQ(r.manifest.runtime.size(), 1u);
    EXPECT_EQ(r.manifest.runtime[0].kind, "jar");

    auto payload = r.manifest.payloadPaths();
    ASSERT_EQ(payload.size(), 2u);  // one binary + one runtime file
}

TEST(BackendManifestTest, RejectsMissingRequiredKey) {
    auto r = parseBackendManifest("[backend]\nname=\"x\"\n", "test");
    EXPECT_FALSE(r.ok);
}

TEST(BackendManifestTest, RejectsPathEscape) {
    std::string m = R"(
[backend]
name = "x"
version = "1.0.0"
language = ["java"]
core_compat = ">=4.0.0"
[binaries]
tool = "../escape/tool"
[platform]
os = "linux"
arch = "x86_64"
)";
    auto r = parseBackendManifest(m, "test");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("escape"), std::string::npos);
}

TEST(BackendManifestTest, RejectsBadBackendName) {
    std::string m = R"(
[backend]
name = "Topo_JVM"
version = "1.0.0"
language = ["java"]
core_compat = ">=4.0.0"
[platform]
os = "linux"
arch = "x86_64"
)";
    EXPECT_FALSE(parseBackendManifest(m, "test").ok);
}

TEST(BackendManifestTest, RejectsBadRuntimeKind) {
    std::string m = R"(
[backend]
name = "x"
version = "1.0.0"
language = ["java"]
core_compat = ">=4.0.0"
[runtime.g]
kind = "bogus"
files = ["a"]
install = "lib"
[platform]
os = "linux"
arch = "x86_64"
)";
    EXPECT_FALSE(parseBackendManifest(m, "test").ok);
}

// ── registry index parser ────────────────────────────────────────

TEST(RegistryIndexTest, ParsesValidIndex) {
    const char* json = R"({
      "schema": 1,
      "backends": [
        { "name": "topo-llvm", "language": ["cpp","rust"],
          "versions": [
            { "version": "1.4.0", "core_compat": ">=4.0.0, <5.0.0",
              "artifacts": [
                { "platform": "macos-aarch64", "path": "topo-llvm/1.4.0/a.tar.zst", "sha256": "abc" }
              ] } ] } ] })";
    auto r = parseRegistryIndex(json, "test");
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.index.backends.size(), 1u);
    const IndexBackend* b = r.index.findBackend("topo-llvm");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->versions.size(), 1u);
    ASSERT_EQ(b->versions[0].artifacts.size(), 1u);
    EXPECT_EQ(b->versions[0].artifacts[0].platform, "macos-aarch64");
}

TEST(RegistryIndexTest, RejectsWrongSchema) {
    EXPECT_FALSE(parseRegistryIndex(R"({"schema":2,"backends":[]})", "test").ok);
}

TEST(RegistryIndexTest, RejectsMalformedJson) {
    EXPECT_FALSE(parseRegistryIndex("{not json", "test").ok);
}

// ── cache install / rollback ─────────────────────────────────────

namespace {
// Build a minimal unpacked backend package directory; returns its path.
std::string makeUnpackedPackage(const fs::path& base, const std::string& coreCompat) {
    fs::path pkg = base / "pkg";
    fs::create_directories(pkg / "bin");
    fs::create_directories(pkg / "lib");
    {
        std::ofstream(pkg / "bin" / "topo-build-jvm-java") << "#!/bin/sh\n";
        std::ofstream(pkg / "lib" / "topo-transform.jar") << "jar\n";
        std::ofstream(pkg / "topo-backend.toml")
            << "[backend]\nname=\"topo-jvm\"\nversion=\"1.2.0\"\n"
               "language=[\"java\"]\ncore_compat=\"" << coreCompat << "\"\n"
               "[binaries]\ntopo-build-jvm-java=\"bin/topo-build-jvm-java\"\n"
               "[runtime.t]\nkind=\"jar\"\nfiles=[\"lib/topo-transform.jar\"]\n"
               "install=\"lib\"\n"
               "[platform]\nos=\"linux\"\narch=\"x86_64\"\n";
    }
    return pkg.string();
}

fs::path uniqueTmp(const std::string& tag) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    fs::path p = fs::temp_directory_path() /
                 ("topo-disttest-" + tag + "-" + std::to_string(gen()));
    fs::create_directories(p);
    return p;
}
} // namespace

TEST(BackendCacheTest, TransactionalInstallAndList) {
    fs::path tmp = uniqueTmp("install");
    std::string pkg = makeUnpackedPackage(tmp, ">=4.0.0, <5.0.0");

    BackendCache cache((tmp / "cache").string());
    ASSERT_TRUE(cache.ensureRoot());

    auto outcome = cache.installFromUnpacked(pkg, "4.0.0", /*requireSignature=*/false);
    ASSERT_TRUE(outcome.ok) << outcome.error;
    EXPECT_TRUE(cache.isInstalled("topo-jvm", "1.2.0"));

    auto installed = cache.listInstalled();
    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].name, "topo-jvm");
    EXPECT_EQ(installed[0].version, "1.2.0");

    // No staging directory left behind.
    for (const auto& e : fs::directory_iterator(cache.root())) {
        EXPECT_NE(e.path().filename().string().rfind(".staging-", 0), 0u);
    }
    fs::remove_all(tmp);
}

TEST(BackendCacheTest, CompatErrorExitCode4) {
    fs::path tmp = uniqueTmp("compat");
    std::string pkg = makeUnpackedPackage(tmp, ">=5.0.0, <6.0.0");

    BackendCache cache((tmp / "cache").string());
    ASSERT_TRUE(cache.ensureRoot());

    auto outcome = cache.installFromUnpacked(pkg, "4.0.0", false);
    EXPECT_FALSE(outcome.ok);
    EXPECT_EQ(outcome.exitCode, 4);
    EXPECT_FALSE(cache.isInstalled("topo-jvm", "1.2.0"));
    fs::remove_all(tmp);
}

TEST(BackendCacheTest, RequireSignatureRefusesExitCode3) {
    fs::path tmp = uniqueTmp("sig");
    std::string pkg = makeUnpackedPackage(tmp, ">=4.0.0, <5.0.0");

    BackendCache cache((tmp / "cache").string());
    ASSERT_TRUE(cache.ensureRoot());

    auto outcome = cache.installFromUnpacked(pkg, "4.0.0", /*requireSignature=*/true);
    EXPECT_FALSE(outcome.ok);
    EXPECT_EQ(outcome.exitCode, 3);
    EXPECT_FALSE(cache.isInstalled("topo-jvm", "1.2.0"));
    fs::remove_all(tmp);
}

TEST(BackendCacheTest, MissingPayloadRollsBack) {
    fs::path tmp = uniqueTmp("missing");
    std::string pkg = makeUnpackedPackage(tmp, ">=4.0.0, <5.0.0");
    // Delete a file the manifest names.
    fs::remove(fs::path(pkg) / "lib" / "topo-transform.jar");

    BackendCache cache((tmp / "cache").string());
    ASSERT_TRUE(cache.ensureRoot());

    auto outcome = cache.installFromUnpacked(pkg, "4.0.0", false);
    EXPECT_FALSE(outcome.ok);
    EXPECT_FALSE(cache.isInstalled("topo-jvm", "1.2.0"));
    fs::remove_all(tmp);
}

TEST(BackendCacheTest, DefaultSelectionAndRemove) {
    fs::path tmp = uniqueTmp("default");
    std::string pkg = makeUnpackedPackage(tmp, ">=4.0.0, <5.0.0");

    BackendCache cache((tmp / "cache").string());
    ASSERT_TRUE(cache.ensureRoot());
    ASSERT_TRUE(cache.installFromUnpacked(pkg, "4.0.0", false).ok);

    std::string err;
    EXPECT_TRUE(cache.setDefault("topo-jvm", "1.2.0", err)) << err;
    EXPECT_EQ(cache.defaultVersion("topo-jvm"), "1.2.0");

    EXPECT_TRUE(cache.removeVersion("topo-jvm", "1.2.0", err)) << err;
    EXPECT_FALSE(cache.isInstalled("topo-jvm", "1.2.0"));
    fs::remove_all(tmp);
}
