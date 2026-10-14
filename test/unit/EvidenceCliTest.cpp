#include "topo/Distribution/EvidenceCli.h"
#include "topo/Distribution/BackendCli.h"
#include "topo/Platform/TempFile.h"
#include "topo/Distribution/Sha256.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
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
TEST_F(EvidenceCliTest, VerificationCacheFlagsRequireCompleteExplicitConfiguration) {
 for (const auto& flags : std::vector<std::vector<std::string>>{
      {"--verification-cache",root.string()}, {"--trust-epoch","one"},
      {"--cache-max-age","0"}, {"--cache-max-age","86401"}, {"--cache-max-age","5seconds"}}) {
  std::vector<std::string> args={"attest","--artifact",(root/"payload/data").string(),"--policy",(root/"policy.json").string()};
  args.insert(args.end(),flags.begin(),flags.end());
  EXPECT_EQ(topo::dist::runEvidenceCli(args),2);
 }
 EXPECT_EQ(topo::dist::runEvidenceCli({"snapshot","--root","payload="+(root/"payload").string(),
  "--output",(root/"evidence.json").string(),"--trust-epoch","one"}),2);
}
#ifndef _WIN32
TEST_F(EvidenceCliTest, CliPersistsVerifierResultAndCurrentRevocationOverridesHit) {
 using Json=nlohmann::json;
 const auto reviewer=root/"reviewer";
 fs::create_directory(reviewer); fs::permissions(reviewer,fs::perms::owner_all);
 const auto artifact=root/"payload/data", bundle=root/"payload/bundle.json", trustedRoot=reviewer/"root.json";
 const auto gh=reviewer/"gh", calls=reviewer/"calls", policyPath=reviewer/"policy.json";
 std::ofstream(bundle) << "bundle"; std::ofstream(trustedRoot) << "trusted root";
 Json statement={{"_type","https://in-toto.io/Statement/v1"},{"predicateType","https://slsa.dev/provenance/v1"},
  {"subject",Json::array({{{"digest",{{"sha256",topo::dist::sha256File(artifact.string())}}}}})}};
 Json response=Json::array({{{"verificationResult",{{"statement",statement},{"signature",{{"certificate",{{"issuer","github"}}}}},
  {"verifiedTimestamps",Json::array({{{"time","2026-10-13"}}})}}}}});
 // A pinned test verifier exercises the real subprocess/CLI path. Its state
 // records actual execution; the second invocation must leave it unchanged.
 std::ofstream script(gh);
 script << "#!/bin/sh\nprintf 'call\\n' >> '" << calls.string() << "'\n"
           "if [ \"$1\" = '--version' ]; then printf 'gh version test\\n'; else cat <<'TOPO_JSON'\n"
        << response.dump() << "\nTOPO_JSON\nfi\n"; script.close();
 fs::permissions(gh,fs::perms::owner_all);
 Json policy={{"schemaVersion",1},{"gh",gh.string()},{"ghSha256",topo::dist::sha256File(gh.string())},
  {"ghVersion","gh version test"},{"trustedRoot",trustedRoot.string()},{"trustedRootSha256",topo::dist::sha256File(trustedRoot.string())},
  {"repository","Topo-lang/topo-core"},{"signerWorkflow","Topo-lang/topo-core/.github/workflows/release.yml"},
  {"signerDigest",std::string(40,'a')},{"sourceDigest",std::string(40,'b')}};
 std::ofstream(policyPath) << policy.dump();
 std::vector<std::string> args={"attest","--artifact",artifact.string(),"--bundle",bundle.string(),"--policy",policyPath.string(),
  "--verification-cache",reviewer.string(),"--project-root",(root/"payload").string(),"--trust-epoch","one"};
 auto run=[&]() {
  ::testing::internal::CaptureStdout(); const auto code=topo::dist::runEvidenceCli(args);
  const auto output=::testing::internal::GetCapturedStdout();
  return std::make_pair(code,Json::parse(output));
 };
 const auto cold=run(); ASSERT_EQ(cold.first,0); EXPECT_FALSE(cold.second.at("verificationCacheHit"));
 const auto invocationBytes=fs::file_size(calls); EXPECT_EQ(invocationBytes,10u);
 const auto hot=run(); ASSERT_EQ(hot.first,0); EXPECT_TRUE(hot.second.at("verificationCacheHit"));
 EXPECT_FALSE(hot.second.at("buildEvidence")); EXPECT_FALSE(hot.second.at("rebuildMatch"));
 EXPECT_EQ(fs::file_size(calls),invocationBytes);
 const auto cachedArgs=args; args.resize(7);
 const auto uncached=run(); ASSERT_EQ(uncached.first,0); EXPECT_FALSE(uncached.second.at("verificationCacheHit"));
 EXPECT_EQ(fs::file_size(calls),invocationBytes+10); args=cachedArgs;
 policy["revoked"]=true; std::ofstream(policyPath) << policy.dump();
 const auto revoked=run(); EXPECT_EQ(revoked.first,1); EXPECT_FALSE(revoked.second.at("signerIdentity"));
 EXPECT_EQ(fs::file_size(calls),invocationBytes+10);
}
#endif
}
