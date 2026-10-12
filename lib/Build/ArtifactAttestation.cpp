#include "topo/Build/ArtifactAttestation.h"
#include "topo/Distribution/Sha256.h"

#include <algorithm>
#include <filesystem>

namespace topo::build::evidence {
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace {

bool failure(std::string& error, const std::string& message) { error = message; return false; }

bool safeValue(const std::string& value) {
    return !value.empty() && std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

bool gitDigest(const std::string& value) {
    return (value.size() == 40 || value.size() == 64) &&
           std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool repository(const std::string& value) {
    auto slash = value.find('/');
    if (slash == 0 || slash == std::string::npos || slash + 1 == value.size() || value.find('/', slash + 1) != std::string::npos)
        return false;
    return std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/';
    });
}

Json policyJson(const AttestationPolicy& p) {
    return {{"repository", p.repository}, {"signerWorkflow", p.signerWorkflow},
            {"signerDigest", p.signerDigest}, {"sourceDigest", p.sourceDigest},
            {"sourceRef", p.sourceRef}, {"certificateIdentity", p.certificateIdentity},
            {"oidcIssuer", p.oidcIssuer}, {"predicateType", p.predicateType},
            {"denySelfHostedRunners", p.denySelfHostedRunners}};
}

bool regular(const fs::path& path) {
    std::error_code ec;
    return !path.empty() && fs::is_regular_file(path, ec) && !ec;
}

bool subjectMatches(const Json& statement, const std::string& digest, const std::string& predicate) {
    if (!statement.is_object() || statement.value("_type", std::string()) != "https://in-toto.io/Statement/v1" ||
        statement.value("predicateType", std::string()) != predicate ||
        !statement.contains("subject") || !statement.at("subject").is_array()) return false;
    for (const auto& subject : statement.at("subject")) {
        if (!subject.is_object() || !subject.contains("digest") || !subject.at("digest").is_object()) continue;
        if (subject.at("digest").value("sha256", std::string()) == digest) return true;
    }
    return false;
}

} // namespace

bool attestationArguments(const AttestationOptions& o, const AttestationPolicy& p,
                          std::vector<std::string>& argv, std::string& error) {
    error.clear(); argv.clear();
    if (!o.executable.is_absolute() || !validDigest(o.executableSha256) || !safeValue(o.expectedVersion))
        return failure(error, "attestation requires an absolute trusted gh path, SHA-256 and exact version");
    if (!o.artifact.is_absolute() || !safeValue(o.artifact.string()))
        return failure(error, "attestation requires an absolute artifact path");
    if (!repository(p.repository) || !safeValue(p.signerWorkflow) || !gitDigest(p.signerDigest) || !gitDigest(p.sourceDigest))
        return failure(error, "attestation requires repository, signer workflow and fixed signer/source commit digests");
    if (!safeValue(p.oidcIssuer) || !safeValue(p.predicateType) ||
        (!p.sourceRef.empty() && !safeValue(p.sourceRef)) ||
        (!p.certificateIdentity.empty() && !safeValue(p.certificateIdentity)))
        return failure(error, "invalid attestation identity policy");
    if (o.timeoutMs <= 0 || o.timeoutMs > 300000) return failure(error, "invalid attestation timeout");
    if (o.offline && (o.bundle.empty() || o.trustedRoot.empty()))
        return failure(error, "offline verification requires a bundle and separately trusted root");
    if ((!o.bundle.empty() && (!o.bundle.is_absolute() || !safeValue(o.bundle.string()))) ||
        (!o.trustedRoot.empty() && (!o.trustedRoot.is_absolute() || !safeValue(o.trustedRoot.string()) || !validDigest(o.trustedRootSha256))))
        return failure(error, "invalid bundle or pinned trusted-root path");

    argv = {"attestation", "verify", o.artifact.string(), "--repo", p.repository,
            "--signer-workflow", p.signerWorkflow, "--signer-digest", p.signerDigest,
            "--source-digest", p.sourceDigest, "--cert-oidc-issuer", p.oidcIssuer,
            "--predicate-type", p.predicateType, "--digest-alg", "sha256", "--format", "json"};
    if (!p.sourceRef.empty()) { argv.push_back("--source-ref"); argv.push_back(p.sourceRef); }
    if (!p.certificateIdentity.empty()) { argv.push_back("--cert-identity"); argv.push_back(p.certificateIdentity); }
    if (p.denySelfHostedRunners) argv.push_back("--deny-self-hosted-runners");
    if (!o.bundle.empty()) { argv.push_back("--bundle"); argv.push_back(o.bundle.string()); }
    if (!o.trustedRoot.empty()) { argv.push_back("--custom-trusted-root"); argv.push_back(o.trustedRoot.string()); }
    return true;
}

std::string attestationVerificationKey(const AttestationOptions& o, const AttestationPolicy& p, std::string& error) {
    std::vector<std::string> argv;
    if (!attestationArguments(o, p, argv, error)) return "";
    if (!regular(o.executable) || !regular(o.artifact) || (!o.bundle.empty() && !regular(o.bundle)) ||
        (!o.trustedRoot.empty() && !regular(o.trustedRoot))) {
        failure(error, "attestation input is absent or is not a regular file"); return "";
    }
    auto tool = dist::sha256File(o.executable.string());
    auto artifact = dist::sha256File(o.artifact.string());
    auto bundle = o.bundle.empty() ? std::string() : dist::sha256File(o.bundle.string());
    auto root = o.trustedRoot.empty() ? std::string() : dist::sha256File(o.trustedRoot.string());
    if (tool != o.executableSha256 || !validDigest(artifact) ||
        (!o.bundle.empty() && !validDigest(bundle)) || (!o.trustedRoot.empty() && root != o.trustedRootSha256)) {
        failure(error, "attestation input read failed or trusted verifier/root identity mismatched"); return "";
    }
    return dist::sha256Hex(Json{{"type", "topo.attestation-verification"}, {"schemaVersion", SchemaVersion},
                                {"artifact", artifact}, {"bundle", bundle}, {"trustedRoot", root},
                                {"verifier", tool}, {"version", o.expectedVersion}, {"offline", o.offline},
                                {"policy", policyJson(p)}}.dump());
}

AttestationResult verifyGitHubAttestation(const AttestationOptions& o, const AttestationPolicy& p,
                                         AttestationRunner runner) {
    AttestationResult result;
    std::string error;
    std::vector<std::string> args;
    if (!attestationArguments(o, p, args, error)) { result.diagnostics.push_back(error); return result; }
    const auto key = attestationVerificationKey(o, p, error);
    if (key.empty()) { result.diagnostics.push_back(error); return result; }
    if (!runner) runner = [](const std::string& exe, const std::vector<std::string>& argv, int timeout) {
        return platform::runProcessCaptureWithTimeout(exe, argv, timeout);
    };
    try {
        auto version = runner(o.executable.string(), {"--version"}, o.timeoutMs);
        auto lineEnd = version.stdoutOutput.find_first_of("\r\n");
        auto firstLine = version.stdoutOutput.substr(0, lineEnd);
        if (version.exitCode != 0 || firstLine != o.expectedVersion) {
            result.diagnostics.push_back("trusted gh version mismatch or version command failed"); return result;
        }
        result.artifactSha256 = dist::sha256File(o.artifact.string());
        auto process = runner(o.executable.string(), args, o.timeoutMs);
        if (process.exitCode != 0) {
            result.diagnostics.push_back("gh attestation verification failed (exit " + std::to_string(process.exitCode) + "): " +
                                         process.stderrOutput.substr(0, 4096));
            return result;
        }
        // Bind the result to the same bytes and trust policy checked before
        // execution. Strict consumers also hold these files in a protected
        // snapshot until use; this catches ordinary concurrent changes.
        if (attestationVerificationKey(o, p, error) != key) {
            result.diagnostics.push_back("attestation inputs changed during verification"); return result;
        }
        if (process.stdoutOutput.size() > 16 * 1024 * 1024) {
            result.diagnostics.push_back("gh verification output exceeds limit"); return result;
        }
        auto response = Json::parse(process.stdoutOutput);
        if (!response.is_array() || response.empty()) {
            result.diagnostics.push_back("gh returned no verified attestations"); return result;
        }
        for (const auto& item : response) {
            if (!item.is_object() || !item.contains("verificationResult")) continue;
            const auto& verified = item.at("verificationResult");
            if (!verified.is_object() || !verified.contains("statement") || !verified.contains("signature") ||
                !verified.at("signature").is_object() || !verified.at("signature").contains("certificate") ||
                !verified.at("signature").at("certificate").is_object() ||
                verified.at("signature").at("certificate").empty() ||
                !verified.contains("verifiedTimestamps") || !verified.at("verifiedTimestamps").is_array() ||
                verified.at("verifiedTimestamps").empty()) continue;
            if (subjectMatches(verified.at("statement"), result.artifactSha256, p.predicateType))
                result.verifiedStatements.push_back(verified.at("statement"));
        }
        if (result.verifiedStatements.empty()) {
            result.diagnostics.push_back("verified attestation lacks matching subject, predicate or certificate evidence"); return result;
        }
        result.integrity = true;
        result.signerIdentity = true;
        result.verificationKey = key;
    } catch (const std::exception& e) {
        result.diagnostics.push_back(std::string("attestation adapter failed: ") + e.what());
    }
    return result;
}

} // namespace topo::build::evidence
