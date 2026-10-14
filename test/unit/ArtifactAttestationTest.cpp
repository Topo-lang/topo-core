#include "topo/Build/ArtifactAttestation.h"
#include "topo/Distribution/Sha256.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#ifdef __APPLE__
#include <pwd.h>
#include <unistd.h>
#endif

namespace {
namespace ev = topo::build::evidence;
namespace fs = std::filesystem;
using Json = nlohmann::json;

class ArtifactAttestationTest : public ::testing::Test {
protected:
    fs::path dir;
    fs::path cacheDir;
    ev::AttestationCacheOptions cache;
    ev::AttestationOptions options;
    ev::AttestationPolicy policy;
    void write(const fs::path& path, const std::string& value) { std::ofstream f(path, std::ios::binary); f << value; }
    void SetUp() override {
        dir = fs::temp_directory_path() / ("topo-attestation-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(fs::create_directory(dir));
        options.executable = dir / "trusted-gh"; write(options.executable, "trusted binary");
        options.executableSha256 = topo::dist::sha256File(options.executable.string());
        options.expectedVersion = "gh version 2.test";
        options.artifact = dir / "artifact with spaces.bin"; write(options.artifact, "payload");
        options.bundle = dir / "bundle.json"; write(options.bundle, "bundle bytes");
        options.trustedRoot = dir / "root.json"; write(options.trustedRoot, "root bytes");
        options.trustedRootSha256 = topo::dist::sha256File(options.trustedRoot.string());
        policy.repository = "Topo-lang/topo-core";
        policy.signerWorkflow = "Topo-lang/topo-core/.github/workflows/release.yml";
        policy.signerDigest = std::string(40, 'a'); policy.sourceDigest = std::string(40, 'b');
        policy.sourceRef = "refs/tags/v1";
        cacheDir = dir.string() + "-reviewer";
        ASSERT_TRUE(fs::create_directory(cacheDir));
        fs::permissions(cacheDir, fs::perms::owner_all);
        cache.directory = cacheDir; cache.projectRoot = dir;
        cache.purpose = "artifact-attestation"; cache.trustEpoch = "reviewer-policy-1";
    }
    void TearDown() override { std::error_code ec; fs::remove_all(dir, ec); fs::remove_all(cacheDir, ec); }
    Json response(const std::string& digest = "") {
        Json statement = {{"_type", "https://in-toto.io/Statement/v1"}, {"predicateType", policy.predicateType},
                          {"subject", Json::array({{{"name", "payload"}, {"digest", {{"sha256", digest.empty() ? topo::dist::sha256File(options.artifact.string()) : digest}}}}})},
                          {"predicate", {{"claim", "observed-build"}}}};
        return Json::array({{{"verificationResult", {{"statement", statement},
                                                     {"signature", {{"certificate", {{"issuer", policy.oidcIssuer}}}}},
                                                     {"verifiedTimestamps", Json::array({{{"time", "2026-10-12T00:00:00Z"}}})}}}}});
    }
    ev::AttestationRunner runner(Json result) {
        return [this, result](const std::string& exe, const std::vector<std::string>& argv, int timeout) {
            EXPECT_EQ(exe, options.executable.string()); EXPECT_EQ(timeout, options.timeoutMs);
            if (argv == std::vector<std::string>{"--version"})
                return topo::platform::CapturedProcessResult{0, options.expectedVersion + "\nhttps://github.com/cli/cli\n", ""};
            EXPECT_EQ(argv.front(), "attestation");
            return topo::platform::CapturedProcessResult{0, result.dump(), ""};
        };
    }
};

TEST_F(ArtifactAttestationTest, StructuredArgumentsPinAllIdentityConstraints) {
    std::vector<std::string> argv; std::string error;
    ASSERT_TRUE(ev::attestationArguments(options, policy, argv, error)) << error;
    EXPECT_EQ(argv[2], options.artifact.string());
    for (const auto& value : {"--repo", "--signer-workflow", "--signer-digest", "--source-digest", "--source-ref",
                              "--cert-oidc-issuer", "--predicate-type", "--custom-trusted-root", "--bundle", "--format"})
        EXPECT_NE(std::find(argv.begin(), argv.end(), value), argv.end()) << value;
}

TEST_F(ArtifactAttestationTest, VerifiedIdentityDoesNotPromotePredicateToBuildProof) {
    auto r = ev::verifyGitHubAttestation(options, policy, runner(response()));
    EXPECT_TRUE(r.integrity); EXPECT_TRUE(r.signerIdentity);
    EXPECT_FALSE(r.buildEvidence); EXPECT_FALSE(r.rebuildMatch);
    EXPECT_TRUE(ev::validDigest(r.verificationKey)); EXPECT_EQ(r.verifiedStatements.size(), 1u);
}

TEST_F(ArtifactAttestationTest, WrongSubjectAndPredicateAreRejected) {
    auto r = ev::verifyGitHubAttestation(options, policy, runner(response(std::string(64, 'f'))));
    EXPECT_FALSE(r.integrity);
    auto other = response(); other[0]["verificationResult"]["statement"]["predicateType"] = "unrelated";
    EXPECT_FALSE(ev::verifyGitHubAttestation(options, policy, runner(other)).integrity);
}

TEST_F(ArtifactAttestationTest, EmptyAndMalformedSuccessfulVerifierOutputsAreRejected) {
    EXPECT_FALSE(ev::verifyGitHubAttestation(options, policy, runner(Json::array())).integrity);
    EXPECT_FALSE(ev::verifyGitHubAttestation(options, policy, runner(Json::object())).integrity);
    auto wrong = response(); wrong[0]["verificationResult"]["signature"] = nullptr;
    EXPECT_FALSE(ev::verifyGitHubAttestation(options, policy, runner(wrong)).integrity);
}

TEST_F(ArtifactAttestationTest, FailureExitNeverBecomesVerifiedIdentity) {
    auto r = ev::verifyGitHubAttestation(options, policy, [this](const auto&, const auto& argv, int) {
        if (argv[0] == "--version") return topo::platform::CapturedProcessResult{0, options.expectedVersion, ""};
        return topo::platform::CapturedProcessResult{1, response().dump(), "invalid signature"};
    });
    EXPECT_FALSE(r.integrity); EXPECT_FALSE(r.signerIdentity); EXPECT_FALSE(r.diagnostics.empty());
}

TEST_F(ArtifactAttestationTest, MissingOfflineRootsAndMovingRefsAreRejectedBeforeExecution) {
    std::string error; std::vector<std::string> args;
    auto missing = options; missing.trustedRoot.clear();
    EXPECT_FALSE(ev::attestationArguments(missing, policy, args, error));
    auto moving = policy; moving.sourceDigest = "main";
    EXPECT_FALSE(ev::attestationArguments(options, moving, args, error));
    moving = policy; moving.signerDigest.clear();
    EXPECT_FALSE(ev::attestationArguments(options, moving, args, error));
}

TEST_F(ArtifactAttestationTest, ForgedVerifierAndRootAreRejectedBeforeExecution) {
    auto never = [](const auto&, const auto&, int) { ADD_FAILURE() << "untrusted tool executed"; return topo::platform::CapturedProcessResult{}; };
    write(options.executable, "replacement verifier");
    EXPECT_FALSE(ev::verifyGitHubAttestation(options, policy, never).integrity);
    write(options.executable, "trusted binary"); write(options.trustedRoot, "project forged root");
    EXPECT_FALSE(ev::verifyGitHubAttestation(options, policy, never).integrity);
}

TEST_F(ArtifactAttestationTest, VersionMismatchIsRejected) {
    auto r = ev::verifyGitHubAttestation(options, policy, [](const auto&, const auto&, int) {
        return topo::platform::CapturedProcessResult{0, "gh version other\n", ""};
    });
    EXPECT_FALSE(r.signerIdentity);
}

TEST_F(ArtifactAttestationTest, InputChangesDuringVerificationAreRejected) {
    auto result = response();
    auto r = ev::verifyGitHubAttestation(options, policy, [this, result](const auto&, const auto& argv, int) {
        if (argv[0] == "--version") return topo::platform::CapturedProcessResult{0, options.expectedVersion, ""};
        write(options.artifact, "tampered while verifier ran");
        return topo::platform::CapturedProcessResult{0, result.dump(), ""};
    });
    EXPECT_FALSE(r.integrity);
}

TEST_F(ArtifactAttestationTest, VerificationCacheKeyBindsPolicyRootBundleAndTool) {
    std::string error; const auto original = ev::attestationVerificationKey(options, policy, error);
    ASSERT_FALSE(original.empty());
    auto changed = policy; changed.sourceRef = "refs/tags/v0";
    EXPECT_NE(original, ev::attestationVerificationKey(options, changed, error));
    write(options.bundle, "new bundle"); EXPECT_NE(original, ev::attestationVerificationKey(options, policy, error));
    write(options.bundle, "bundle bytes"); write(options.trustedRoot, "new trusted root");
    options.trustedRootSha256 = topo::dist::sha256File(options.trustedRoot.string());
    EXPECT_NE(original, ev::attestationVerificationKey(options, policy, error));
}

TEST_F(ArtifactAttestationTest, RevokedIdentityIsRejectedBeforeAnyVerification) {
    policy.revoked = true;
    auto never = [](const auto&, const auto&, int) { ADD_FAILURE() << "revoked tool executed"; return topo::platform::CapturedProcessResult{}; };
    EXPECT_FALSE(ev::verifyGitHubAttestation(options, policy, never).integrity);
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options, policy, cache, never).integrity);
}

#ifndef _WIN32
TEST_F(ArtifactAttestationTest, PersistentHitRestoresOnlySignatureJudgmentsWithoutExecutingGh) {
    auto cold = ev::verifyGitHubAttestationCached(options, policy, cache, runner(response()));
    ASSERT_TRUE(cold.integrity); ASSERT_TRUE(cold.signerIdentity); ASSERT_FALSE(cold.cacheHit);
    ASSERT_GT(cold.cacheExpiresAt, 0);
    auto never = [](const auto&, const auto&, int) { ADD_FAILURE() << "hot cache executed gh"; return topo::platform::CapturedProcessResult{}; };
    auto hot = ev::verifyGitHubAttestationCached(options, policy, cache, never);
    EXPECT_TRUE(hot.integrity); EXPECT_TRUE(hot.signerIdentity); EXPECT_TRUE(hot.cacheHit);
    EXPECT_FALSE(hot.buildEvidence); EXPECT_FALSE(hot.rebuildMatch);
    EXPECT_EQ(hot.verificationKey, cold.verificationKey);
    EXPECT_EQ(hot.verifiedStatements, cold.verifiedStatements);
    EXPECT_EQ(hot.cacheExpiresAt, cold.cacheExpiresAt);
}

TEST_F(ArtifactAttestationTest, CachedSuccessCannotOverrideCurrentRevocationOrMissingPinnedInput) {
    ASSERT_TRUE(ev::verifyGitHubAttestationCached(options, policy, cache, runner(response())).integrity);
    auto never = [](const auto&, const auto&, int) { ADD_FAILURE() << "invalid input executed gh"; return topo::platform::CapturedProcessResult{}; };
    policy.revoked = true;
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options, policy, cache, never).signerIdentity);
    policy.revoked = false;
    write(options.trustedRoot, "root replacement");
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options, policy, cache, never).signerIdentity);
    write(options.trustedRoot, "root bytes"); fs::remove(options.executable);
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options, policy, cache, never).signerIdentity);
}

TEST_F(ArtifactAttestationTest, PolicyPurposeEpochLifetimeAndModeChangesRequireFreshVerification) {
    ASSERT_TRUE(ev::verifyGitHubAttestationCached(options, policy, cache, runner(response())).integrity);
    int calls=0;
    auto reject = [&](const auto&, const auto&, int) { ++calls; return topo::platform::CapturedProcessResult{1, "", "rejected"}; };
    auto changedPolicy=policy; changedPolicy.repository="Other/repository";
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options, changedPolicy, cache, reject).integrity);
    auto changedCache=cache; changedCache.purpose="manual-approval";
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options, policy, changedCache, reject).integrity);
    changedCache=cache; changedCache.trustEpoch="revocation-generation-2";
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options, policy, changedCache, reject).integrity);
    changedCache=cache; changedCache.maxAgeSeconds=60;
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options, policy, changedCache, reject).integrity);
    auto changedOptions=options; changedOptions.offline=false;
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(changedOptions, policy, cache, reject).integrity);
    EXPECT_EQ(calls,5);
}

TEST_F(ArtifactAttestationTest, ChangedBytesRequireFreshVerificationEvenWhenSizeAndMtimeMatch) {
    ASSERT_TRUE(ev::verifyGitHubAttestationCached(options, policy, cache, runner(response())).integrity);
    int calls=0;
    auto reject = [&](const auto&, const auto&, int) { ++calls; return topo::platform::CapturedProcessResult{1, "", "rejected"}; };
    const auto timestamp=fs::last_write_time(options.artifact);
    write(options.artifact,"changed"); fs::last_write_time(options.artifact,timestamp);
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,reject).integrity);
    write(options.artifact,"payload"); write(options.bundle,"new bundle");
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,reject).integrity);
    write(options.bundle,"bundle bytes"); write(options.trustedRoot,"new root");
    options.trustedRootSha256=topo::dist::sha256File(options.trustedRoot.string());
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,reject).integrity);
    write(options.trustedRoot,"root bytes"); options.trustedRootSha256=topo::dist::sha256File(options.trustedRoot.string());
    write(options.executable,"new trusted binary"); options.executableSha256=topo::dist::sha256File(options.executable.string());
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,reject).integrity);
    write(options.executable,"trusted binary"); options.executableSha256=topo::dist::sha256File(options.executable.string());
    options.expectedVersion="gh version next";
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,reject).integrity);
    EXPECT_EQ(calls,5);
}

TEST_F(ArtifactAttestationTest, ExpiredMalformedAndWrongPurposeRecordsNeverPromoteFailedVerification) {
    ASSERT_TRUE(ev::verifyGitHubAttestationCached(options, policy, cache, runner(response())).integrity);
    const auto entry=fs::directory_iterator(cacheDir)->path();
    std::ifstream input(entry); const auto original=Json::parse(input); input.close();
    std::vector<Json> records;
    auto record=original; record["verifiedAt"]=1; record["expiresAt"]=1+cache.maxAgeSeconds; records.push_back(record);
    record=original; record["schemaVersion"]=2; records.push_back(record);
    record=original; record["buildEvidence"]=true; records.push_back(record);
    record=original; record["cacheKey"]=std::string(64,'0'); records.push_back(record);
    record=original; record["verifiedAt"]=original.at("verifiedAt").get<std::int64_t>()+86400;
    record["expiresAt"]=record.at("verifiedAt").get<std::int64_t>()+cache.maxAgeSeconds; records.push_back(record);
    record=original; record["verifiedStatements"]=Json::array(); records.push_back(record);
    record=original; record["verifiedStatements"][0]["subject"][0]["digest"]["sha256"]=std::string(64,'0'); records.push_back(record);
    int calls=0;
    auto reject = [&](const auto&, const auto& argv, int) {
        ++calls;
        if (argv[0]=="--version") return topo::platform::CapturedProcessResult{0,options.expectedVersion,""};
        return topo::platform::CapturedProcessResult{1,response().dump(),"invalid signature"};
    };
    for (const auto& invalid:records) {
        write(entry,invalid.dump());
        auto result=ev::verifyGitHubAttestationCached(options,policy,cache,reject);
        EXPECT_FALSE(result.integrity); EXPECT_FALSE(result.cacheHit); EXPECT_FALSE(result.buildEvidence);
    }
    write(entry,"{truncated");
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,reject).integrity);
    EXPECT_EQ(calls,static_cast<int>((records.size()+1)*2));
}

TEST_F(ArtifactAttestationTest, FailedVerificationIsNeverPersisted) {
    auto reject = [this](const auto&, const auto& argv, int) {
        if (argv[0]=="--version") return topo::platform::CapturedProcessResult{0,options.expectedVersion,""};
        return topo::platform::CapturedProcessResult{1,response().dump(),"invalid signature"};
    };
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,reject).integrity);
    EXPECT_TRUE(fs::is_empty(cacheDir));
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,runner(Json::array())).integrity);
    EXPECT_TRUE(fs::is_empty(cacheDir));
}

TEST_F(ArtifactAttestationTest, ExpiredSuccessIsReverifiedAndAtomicallyRefreshed) {
    ASSERT_TRUE(ev::verifyGitHubAttestationCached(options,policy,cache,runner(response())).integrity);
    const auto entry=fs::directory_iterator(cacheDir)->path();
    std::ifstream input(entry); auto record=Json::parse(input); input.close();
    record["verifiedAt"]=1; record["expiresAt"]=1+cache.maxAgeSeconds; write(entry,record.dump());
    int calls=0; const auto verifier=runner(response());
    auto fresh=ev::verifyGitHubAttestationCached(options,policy,cache,[&](const auto& exe,const auto& argv,int timeout) {
        ++calls; return verifier(exe,argv,timeout);
    });
    ASSERT_TRUE(fresh.integrity); EXPECT_FALSE(fresh.cacheHit); EXPECT_EQ(calls,2);
    EXPECT_GT(fresh.cacheExpiresAt,1+cache.maxAgeSeconds);
    auto never=[](const auto&,const auto&,int) { ADD_FAILURE() << "refreshed cache executed gh"; return topo::platform::CapturedProcessResult{}; };
    auto hot=ev::verifyGitHubAttestationCached(options,policy,cache,never);
    EXPECT_TRUE(hot.cacheHit); EXPECT_EQ(hot.cacheExpiresAt,fresh.cacheExpiresAt);
    EXPECT_EQ(std::distance(fs::directory_iterator(cacheDir),fs::directory_iterator()),1);
}

TEST_F(ArtifactAttestationTest, ProjectLocalRedirectedAndUnprotectedStoresAreRejectedBeforeExecution) {
    auto never = [](const auto&, const auto&, int) { ADD_FAILURE() << "unsafe store executed gh"; return topo::platform::CapturedProcessResult{}; };
    auto local=dir/"cache"; fs::create_directory(local); fs::permissions(local,fs::perms::owner_all);
    cache.directory=local;
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
    auto alias=cacheDir/"alias"; fs::create_directory_symlink(local,alias); cache.directory=alias;
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
    cache.directory=cacheDir; fs::permissions(cacheDir,fs::perms::group_read,fs::perm_options::add);
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
    fs::permissions(cacheDir,fs::perms::owner_all); cache.projectRoot=local;
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
}

TEST_F(ArtifactAttestationTest, SharedAncestorAndLinkedOrSharedCacheFilesAreRejected) {
    auto never = [](const auto&, const auto&, int) { ADD_FAILURE() << "unsafe cache executed gh"; return topo::platform::CapturedProcessResult{}; };
    auto nested=cacheDir/"private"; fs::create_directory(nested); fs::permissions(nested,fs::perms::owner_all);
    cache.directory=nested; fs::permissions(cacheDir,fs::perms::others_write,fs::perm_options::add);
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
    fs::permissions(cacheDir,fs::perms::owner_all); fs::remove(nested); cache.directory=cacheDir;
    ASSERT_TRUE(ev::verifyGitHubAttestationCached(options,policy,cache,runner(response())).integrity);
    const auto entry=fs::directory_iterator(cacheDir)->path();
    fs::create_hard_link(entry,dir/"project-controlled-record");
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
    fs::remove(dir/"project-controlled-record");
    fs::permissions(entry,fs::perms::group_read,fs::perm_options::add);
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
    fs::permissions(entry,fs::perms::owner_read|fs::perms::owner_write);
    fs::rename(entry,dir/"project-controlled-record"); fs::create_symlink(dir/"project-controlled-record",entry);
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
}

TEST_F(ArtifactAttestationTest, CacheConfigurationRequiresExplicitBoundedValidityAndFixedInputs) {
    auto never = [](const auto&, const auto&, int) { ADD_FAILURE() << "invalid cache executed gh"; return topo::platform::CapturedProcessResult{}; };
    for (auto age:{0,86401}) {
        cache.maxAgeSeconds=age; EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
    }
    cache.maxAgeSeconds=3600; cache.trustEpoch.clear();
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
    cache.trustEpoch="reviewer-policy-1"; options.offline=false; options.trustedRoot.clear();
    EXPECT_FALSE(ev::verifyGitHubAttestationCached(options,policy,cache,never).integrity);
}
#ifdef __APPLE__
TEST_F(ArtifactAttestationTest, DarwinExtendedAllowAclCannotHideBehindPrivateModeBits) {
    auto chmod=topo::platform::runProcessCaptureWithTimeout("/bin/chmod",
        {"+a", "everyone allow write,append,delete,writeattr,writeextattr,writesecurity", cacheDir.string()},10000);
    ASSERT_EQ(chmod.exitCode,0) << chmod.stderrOutput;
    auto never = [](const auto&, const auto&, int) { ADD_FAILURE() << "shared ACL executed gh"; return topo::platform::CapturedProcessResult{}; };
    auto result=ev::verifyGitHubAttestationCached(options,policy,cache,never);
    EXPECT_FALSE(result.integrity); ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_NE(result.diagnostics.front().find("extended ACL grants"),std::string::npos);
}
TEST_F(ArtifactAttestationTest, DarwinOwnerWriteAndOtherReadOnlyAclsRetainProtectedCache) {
    const auto* owner=::getpwuid(::geteuid()); ASSERT_NE(owner,nullptr);
    auto chmod=topo::platform::runProcessCaptureWithTimeout("/bin/chmod",
        {"+a",std::string("user:")+owner->pw_name+" allow write,append,delete,writeattr,writeextattr,writesecurity",cacheDir.string()},10000);
    ASSERT_EQ(chmod.exitCode,0) << chmod.stderrOutput;
    chmod=topo::platform::runProcessCaptureWithTimeout("/bin/chmod",
        {"+a","everyone allow read,readattr,readextattr,readsecurity",cacheDir.string()},10000);
    ASSERT_EQ(chmod.exitCode,0) << chmod.stderrOutput;
    auto result=ev::verifyGitHubAttestationCached(options,policy,cache,runner(response()));
    ASSERT_TRUE(result.integrity) << (result.diagnostics.empty()?"":result.diagnostics.front());
    EXPECT_GT(result.cacheExpiresAt,0);
}
#endif
#else
TEST_F(ArtifactAttestationTest, PersistentCacheFailsClosedWithoutWindowsAclValidation) {
    auto never = [](const auto&, const auto&, int) { ADD_FAILURE() << "unsupported cache executed gh"; return topo::platform::CapturedProcessResult{}; };
    auto result=ev::verifyGitHubAttestationCached(options,policy,cache,never);
    EXPECT_FALSE(result.integrity); ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_NE(result.diagnostics.front().find("Windows ACL validation is unavailable"),std::string::npos);
}
#endif

TEST_F(ArtifactAttestationTest, RealGhRejectsInvalidOfflineBundle) {
    const char* executable = std::getenv("TOPO_TEST_TRUSTED_GH");
    if (!executable || !*executable) GTEST_SKIP() << "set TOPO_TEST_TRUSTED_GH to exercise an installed pinned gh";
    options.executable = fs::canonical(executable);
    options.executableSha256 = topo::dist::sha256File(options.executable.string());
    auto version = topo::platform::runProcessCaptureWithTimeout(options.executable.string(), {"--version"}, 10000);
    ASSERT_EQ(version.exitCode, 0);
    options.expectedVersion = version.stdoutOutput.substr(0, version.stdoutOutput.find_first_of("\r\n"));
    write(options.bundle, "{}"); write(options.trustedRoot, "{}");
    options.trustedRootSha256 = topo::dist::sha256File(options.trustedRoot.string());
    auto r = ev::verifyGitHubAttestation(options, policy);
    EXPECT_FALSE(r.integrity); EXPECT_FALSE(r.signerIdentity); ASSERT_FALSE(r.diagnostics.empty());
    EXPECT_NE(r.diagnostics.front().find("gh attestation verification failed"), std::string::npos);
}

} // namespace
