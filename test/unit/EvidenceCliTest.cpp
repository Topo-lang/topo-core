#include "topo/Distribution/EvidenceCli.h"
#include "topo/Distribution/BackendCli.h"
#include "topo/Platform/TempFile.h"
#include "topo/Distribution/Sha256.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
namespace fs=std::filesystem;
std::error_code createInventoryDirectoryLink(const fs::path& target, const fs::path& link) {
#ifdef _WIN32
 if (CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2)) return {};
 auto error = GetLastError();
 if (error == ERROR_INVALID_PARAMETER || error == ERROR_PRIVILEGE_NOT_HELD) {
  if (CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY)) return {};
  error = GetLastError();
 }
 return {static_cast<int>(error), std::system_category()};
#else
 std::error_code error; fs::create_directory_symlink(target, link, error); return error;
#endif
}
class EvidenceCliTest : public ::testing::Test {
protected:
 topo::platform::TempFile marker{"evidence-cli"};
 fs::path root=marker.path().string()+"-root";
 void SetUp() override { fs::create_directories(root/"payload"); std::ofstream(root/"payload/data") << "frozen bytes"; }
 void TearDown() override {
#ifdef _WIN32
  for (const auto& path : {root/"payload/alias", root/"alias"}) {
   const auto attributes = GetFileAttributesW(path.c_str());
   if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    RemoveDirectoryW(path.c_str());
  }
#endif
  std::error_code ec; fs::remove_all(root,ec);
 }
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
TEST_F(EvidenceCliTest, EmptyDirectoryLinkRejectsSnapshotAndVerification) {
 ASSERT_EQ(snapshot(),0);
 fs::create_directory(root/"outside");
 auto error=createInventoryDirectoryLink(root/"outside",root/"payload/alias");
 ASSERT_FALSE(error) << error.message();
 EXPECT_EQ(snapshot(),2);
 EXPECT_EQ(verify(),2);
}
TEST_F(EvidenceCliTest, PayloadRootAliasCannotHideSidecarInsidePayload) {
 auto error=createInventoryDirectoryLink(root/"payload",root/"alias");
 ASSERT_FALSE(error) << error.message();
 EXPECT_EQ(topo::dist::runEvidenceCli({"snapshot","--root","payload="+(root/"alias").string(),
  "--output",(root/"payload/evidence.json").string()}),2);
 EXPECT_FALSE(fs::exists(root/"payload/evidence.json"));
}
TEST_F(EvidenceCliTest, SidecarDirectoryAliasCannotWriteInsidePayload) {
 auto error=createInventoryDirectoryLink(root/"payload",root/"alias");
 ASSERT_FALSE(error) << error.message();
 EXPECT_EQ(topo::dist::runEvidenceCli({"snapshot","--root","payload="+(root/"payload").string(),
  "--output",(root/"alias/evidence.json").string()}),2);
 EXPECT_FALSE(fs::exists(root/"payload/evidence.json"));
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
