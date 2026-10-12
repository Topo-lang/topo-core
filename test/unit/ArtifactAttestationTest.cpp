#include "topo/Build/ArtifactAttestation.h"
#include "topo/Distribution/Sha256.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>

namespace {
namespace ev = topo::build::evidence;
namespace fs = std::filesystem;
using Json = nlohmann::json;

class ArtifactAttestationTest : public ::testing::Test {
protected:
    fs::path dir;
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
    }
    void TearDown() override { std::error_code ec; fs::remove_all(dir, ec); }
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
