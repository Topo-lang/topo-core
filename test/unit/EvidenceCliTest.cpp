#include "topo/Distribution/EvidenceCli.h"
#include "topo/Distribution/BackendCli.h"
#include "topo/Platform/TempFile.h"
#include "topo/Distribution/Sha256.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
namespace {
namespace fs=std::filesystem;
class EvidenceCliTest : public ::testing::Test {
protected:
 topo::platform::TempFile marker{"evidence-cli"};
 fs::path root=marker.path().string()+"-root";
 void SetUp() override { fs::create_directories(root/"payload"); std::ofstream(root/"payload/data") << "frozen bytes"; }
 void TearDown() override { std::error_code ec; fs::remove_all(root,ec); }
 int snapshot() { return topo::dist::runEvidenceCli({"snapshot","--root","payload="+(root/"payload").string(),"--output",(root/"evidence.json").string()}); }
 int verify() { return topo::dist::runEvidenceCli({"verify","--manifest",(root/"evidence.json").string(),"--root","payload="+(root/"payload").string()}); }
};
TEST_F(EvidenceCliTest, InventoryAndVerificationPreserveArtifactBytes) {
 auto before=topo::dist::sha256File((root/"payload/data").string());
 EXPECT_EQ(snapshot(),0); EXPECT_EQ(verify(),0);
 EXPECT_EQ(before,topo::dist::sha256File((root/"payload/data").string()));
}
TEST_F(EvidenceCliTest, ChangedBytesAndExtraPayloadFileReject) {
 ASSERT_EQ(snapshot(),0); std::ofstream(root/"payload/data") << "different bytes"; EXPECT_EQ(verify(),1);
 std::ofstream(root/"payload/data") << "frozen bytes"; std::ofstream(root/"payload/hidden") << "extra"; EXPECT_EQ(verify(),1);
}
TEST_F(EvidenceCliTest, EvidenceCannotRecursivelyIncludeItself) {
 EXPECT_EQ(topo::dist::runEvidenceCli({"snapshot","--root","payload="+(root/"payload").string(),"--output",(root/"payload/evidence.json").string()}),2);
}
TEST_F(EvidenceCliTest, ContentComparisonReportsOnlyByteEquality) {
 std::ofstream(root/"rebuilt") << "frozen bytes";
 EXPECT_EQ(topo::dist::runEvidenceCli({"compare",(root/"payload/data").string(),(root/"rebuilt").string()}),0);
 std::ofstream(root/"rebuilt") << "different";
 EXPECT_EQ(topo::dist::runEvidenceCli({"compare",(root/"payload/data").string(),(root/"rebuilt").string()}),1);
}
TEST_F(EvidenceCliTest, InstalledBackendVerificationUsesSelectedPayloadAndManifest) {
 auto installed=root/"cache/topo-jvm/1.2.0";
 fs::create_directories(installed);
 std::ofstream(installed/"topo-backend.toml") << "[backend]\nname='topo-jvm'\nversion='1.2.0'\nlanguage=['java']\ncore_compat='>=4.0.0'\n[platform]\nos='macos'\narch='aarch64'\n";
 std::ofstream(installed/"library") << "binary";
 ASSERT_EQ(topo::dist::runEvidenceCli({"snapshot","--root","payload="+installed.string(),"--output",(root/"backend.evidence.json").string()}),0);
 std::vector<std::string> args={"verify-provenance","topo-jvm@1.2.0","--cache-root",(root/"cache").string(),"--evidence",(root/"backend.evidence.json").string()};
 EXPECT_EQ(topo::dist::runBackendCli(args),0);
 std::ofstream(installed/"library") << "replacement";
 EXPECT_NE(topo::dist::runBackendCli(args),0);
 fs::remove(installed/"topo-backend.toml");
 EXPECT_NE(topo::dist::runBackendCli(args),0);
}
}
