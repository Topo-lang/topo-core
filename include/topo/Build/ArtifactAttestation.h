#ifndef TOPO_BUILD_ARTIFACTATTESTATION_H
#define TOPO_BUILD_ARTIFACTATTESTATION_H

#include "topo/Build/ArtifactEvidence.h"
#include <functional>
#include "topo/Platform/Process.h"

namespace topo::build::evidence {

struct AttestationPolicy {
    std::string repository;
    std::string signerWorkflow;
    std::string signerDigest;
    std::string sourceDigest;
    std::string sourceRef;
    std::string certificateIdentity;
    std::string oidcIssuer = "https://token.actions.githubusercontent.com";
    std::string predicateType = "https://slsa.dev/provenance/v1";
    bool denySelfHostedRunners = true;
};

struct AttestationOptions {
    std::filesystem::path executable;
    std::string executableSha256;
    std::string expectedVersion; // exact first line from gh --version
    std::filesystem::path artifact;
    std::filesystem::path bundle;
    std::filesystem::path trustedRoot;
    std::string trustedRootSha256;
    bool offline = true;
    int timeoutMs = 30000;
};

struct AttestationResult {
    bool integrity = false;
    bool signerIdentity = false;
    bool buildEvidence = false;
    bool rebuildMatch = false;
    std::string artifactSha256;
    std::string verificationKey;
    nlohmann::json verifiedStatements = nlohmann::json::array();
    std::vector<std::string> diagnostics;
};

using AttestationRunner = std::function<platform::CapturedProcessResult(
    const std::string&, const std::vector<std::string>&, int)>;

bool attestationArguments(const AttestationOptions& options, const AttestationPolicy& policy,
                          std::vector<std::string>& argv, std::string& error);
// The key binds bytes, verifier, policy and trusted-root contents. Caller owns
// protected storage and validity/revocation lifetime; this API never trusts a
// project-local cached verification result.
std::string attestationVerificationKey(const AttestationOptions& options,
                                       const AttestationPolicy& policy, std::string& error);
AttestationResult verifyGitHubAttestation(const AttestationOptions& options,
                                         const AttestationPolicy& policy,
                                         AttestationRunner runner = {});

} // namespace topo::build::evidence
#endif
