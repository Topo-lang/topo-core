#ifndef TOPO_BUILD_ARTIFACTATTESTATION_H
#define TOPO_BUILD_ARTIFACTATTESTATION_H

#include "topo/Build/ArtifactEvidence.h"
#include <functional>
#include <cstdint>
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
    bool revoked = false;
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
    bool cacheHit = false;
    std::int64_t cacheExpiresAt = 0; // Unix seconds, zero when caching is disabled
    std::string artifactSha256;
    std::string verificationKey;
    nlohmann::json verifiedStatements = nlohmann::json::array();
    std::vector<std::string> diagnostics;
};

struct AttestationCacheOptions {
    // Explicit reviewer-owned, pre-existing private directory outside projectRoot.
    // The caller must isolate it from project execution under the same account.
    std::filesystem::path directory;
    // Caller-defined untrusted tree; must contain the artifact and bundle.
    std::filesystem::path projectRoot;
    std::string purpose;
    std::string trustEpoch; // reviewer-controlled revocation/policy generation
    std::int64_t maxAgeSeconds = 3600; // 1..86400; part of the cache identity
};

using AttestationRunner = std::function<platform::CapturedProcessResult(
    const std::string&, const std::vector<std::string>&, int)>;

bool attestationArguments(const AttestationOptions& options, const AttestationPolicy& policy,
                          std::vector<std::string>& argv, std::string& error);
// The key binds bytes, verifier, policy and trusted-root contents.
std::string attestationVerificationKey(const AttestationOptions& options,
                                       const AttestationPolicy& policy, std::string& error);
AttestationResult verifyGitHubAttestation(const AttestationOptions& options,
                                         const AttestationPolicy& policy,
                                         AttestationRunner runner = {});
// Persistent caching is opt-in. POSIX checks owner-only storage and protected
// ancestors; Windows rejects this mode until an ACL-backed store is available.
// Expired/invalid entries are misses; current input/policy rejection wins before
// lookup. A hit rehashes all inputs and runs no subprocess. Cached signatures
// never establish buildEvidence or rebuildMatch.
AttestationResult verifyGitHubAttestationCached(const AttestationOptions& options,
                                               const AttestationPolicy& policy,
                                               const AttestationCacheOptions& cache,
                                               AttestationRunner runner = {});

} // namespace topo::build::evidence
#endif
