#include "topo/Build/ArtifactAttestation.h"
#include "topo/Distribution/Sha256.h"
#include "topo/Platform/TempFile.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <sys/acl.h>
#include <membership.h>
#endif

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
            {"denySelfHostedRunners", p.denySelfHostedRunners}, {"revoked", p.revoked}};
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

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool within(const fs::path& path, const fs::path& root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

#ifndef _WIN32
bool protectedAcl(const fs::path& path, std::string& error) {
#ifdef __APPLE__
    // Darwin allow ACLs can grant write access even when mode bits are 0700.
    // Preserve owner/root grants and read-only/deny entries; other identities
    // must not be able to modify data, descendants, ownership or permissions.
    acl_t acl = ::acl_get_file(path.c_str(), ACL_TYPE_EXTENDED);
    if (!acl) {
        struct stat status {};
        // Darwin represents an absent extended ACL as ENOENT even when the
        // filesystem object exists. Distinguish it from a disappeared path.
        if (errno == ENOENT && ::lstat(path.c_str(), &status) == 0) return true;
        return failure(error, "cannot inspect attestation cache ACL: " + path.string());
    }
    uuid_t owner, root;
    if (::mbr_uid_to_uuid(::geteuid(), owner) != 0 || ::mbr_uid_to_uuid(0, root) != 0) {
        ::acl_free(acl); return failure(error, "cannot resolve protected cache owner identities");
    }
    acl_entry_t entry;
    for (int index = ACL_FIRST_ENTRY; ::acl_get_entry(acl, index, &entry) == 0; index = ACL_NEXT_ENTRY) {
        acl_tag_t tag;
        if (::acl_get_tag_type(entry, &tag) != 0 || (tag != ACL_EXTENDED_ALLOW && tag != ACL_EXTENDED_DENY)) {
            ::acl_free(acl); return failure(error, "cannot inspect attestation cache ACL entry");
        }
        if (tag == ACL_EXTENDED_DENY) continue;
        void* principal = ::acl_get_qualifier(entry);
        if (!principal) { ::acl_free(acl); return failure(error, "cannot resolve attestation cache ACL identity"); }
        const bool trusted = std::memcmp(principal, owner, sizeof(uuid_t)) == 0 ||
                             std::memcmp(principal, root, sizeof(uuid_t)) == 0;
        ::acl_free(principal);
        if (trusted) continue;
        acl_permset_t permissions;
        if (::acl_get_permset(entry, &permissions) != 0) {
            ::acl_free(acl); return failure(error, "cannot inspect attestation cache ACL permissions");
        }
        for (auto permission : {ACL_WRITE_DATA, ACL_APPEND_DATA, ACL_DELETE, ACL_DELETE_CHILD,
                                ACL_WRITE_ATTRIBUTES, ACL_WRITE_EXTATTRIBUTES, ACL_WRITE_SECURITY, ACL_CHANGE_OWNER}) {
            if (::acl_get_perm_np(permissions, permission) == 0) continue;
            ::acl_free(acl);
            return failure(error, "attestation cache path has untrusted writable extended ACL grants: " + path.string());
        }
    }
    ::acl_free(acl);
#else
    (void)path; (void)error;
#endif
    return true;
}
#endif

bool privateStorePath(const fs::path& path, bool directory, std::string& error) {
#ifdef _WIN32
    (void)path; (void)directory;
    return failure(error, "persistent attestation caching requires a supported protected store; Windows ACL validation is unavailable");
#else
    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0 || status.st_uid != ::geteuid() ||
        (status.st_mode & (S_IRWXG | S_IRWXO)) != 0 ||
        (directory ? !S_ISDIR(status.st_mode) : (!S_ISREG(status.st_mode) || status.st_nlink != 1)))
        return failure(error, "attestation cache must use reviewer-owned private directories and singly linked regular files: " + path.string());
    return protectedAcl(path, error);
#endif
}

bool cacheStore(const AttestationOptions& o, const AttestationCacheOptions& c,
                fs::path& directory, fs::path& projectRoot, std::string& error) {
    if (o.trustedRoot.empty())
        return failure(error, "persistent attestation caching requires an explicitly pinned trusted root");
    if (!c.directory.is_absolute() || !c.projectRoot.is_absolute() || !safeValue(c.purpose) ||
        !safeValue(c.trustEpoch) || c.purpose.size() > 256 || c.trustEpoch.size() > 256 ||
        c.maxAgeSeconds < 1 || c.maxAgeSeconds > 86400)
        return failure(error, "attestation cache requires absolute store/project paths, purpose, trust epoch and max age 1..86400 seconds");
    std::error_code ec;
    projectRoot = platform::canonicalPath(c.projectRoot, ec);
    if (ec || !fs::is_directory(projectRoot, ec) || ec)
        return failure(error, "attestation cache requires an existing untrusted project root");
    for (const auto& input : {o.artifact, o.bundle}) {
        if (input.empty()) return failure(error, "persistent attestation caching requires an explicit artifact and bundle");
        const auto resolved = platform::canonicalPath(input, ec);
        if (ec || !within(resolved, projectRoot))
            return failure(error, "attestation artifact and bundle must lie within the declared untrusted project root");
    }
    // Inspect the requested final component before canonicalization so a link
    // cannot borrow an otherwise private directory's owner/permission checks.
    if (!privateStorePath(c.directory, true, error)) return false;
    directory = platform::canonicalPath(c.directory, ec);
    if (ec || within(directory, projectRoot))
        return failure(error, "attestation cache must be outside the untrusted project root");
#ifndef _WIN32
    for (auto ancestor = directory.parent_path(); !ancestor.empty(); ancestor = ancestor.parent_path()) {
        struct stat status {};
        if (::lstat(ancestor.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) ||
            (status.st_uid != 0 && status.st_uid != ::geteuid()) ||
            ((status.st_mode & (S_IWGRP | S_IWOTH)) != 0 &&
             !(status.st_uid == 0 && (status.st_mode & S_ISVTX))))
            return failure(error, "attestation cache has an unprotected ancestor: " + ancestor.string());
        if (!protectedAcl(ancestor, error)) return false;
        if (ancestor == ancestor.root_path()) break;
    }
#endif
    return true;
}

std::string cacheIdentity(const std::string& key, const AttestationCacheOptions& c, const fs::path& projectRoot) {
    return dist::sha256Hex(Json{{"type", "topo.attestation-cache"}, {"schemaVersion", 1},
                               {"verificationKey", key}, {"projectRoot", projectRoot.generic_string()},
                               {"purpose", c.purpose}, {"trustEpoch", c.trustEpoch},
                               {"maxAgeSeconds", c.maxAgeSeconds}}.dump());
}

bool cachedResult(const fs::path& path, const std::string& cacheKey, const std::string& verificationKey,
                  const std::string& artifactDigest, const AttestationPolicy& policy,
                  const AttestationCacheOptions& cache, AttestationResult& result) {
    try {
        if (fs::file_size(path) > 16 * 1024 * 1024) return false;
        std::ifstream in(path, std::ios::binary);
        const auto record = Json::parse(in);
        const std::set<std::string> fields = {"type", "schemaVersion", "cacheKey", "verificationKey",
                                             "artifactSha256", "verifiedAt", "expiresAt", "verifiedStatements"};
        if (!record.is_object() || record.size() != fields.size()) return false;
        for (const auto& [field, value] : record.items()) {
            (void)value;
            if (!fields.count(field)) return false;
        }
        if (record.at("type") != "topo.attestation-cache-result" || record.at("schemaVersion") != 1 ||
            record.at("cacheKey") != cacheKey || record.at("verificationKey") != verificationKey ||
            record.at("artifactSha256") != artifactDigest ||
            !record.at("verifiedAt").is_number_integer() || !record.at("expiresAt").is_number_integer()) return false;
        const auto verifiedAt = record.at("verifiedAt").get<std::int64_t>();
        const auto expiresAt = record.at("expiresAt").get<std::int64_t>();
        const auto now = nowSeconds();
        if (verifiedAt <= 0 || verifiedAt > now || expiresAt <= now ||
            expiresAt <= verifiedAt || expiresAt - verifiedAt != cache.maxAgeSeconds) return false;
        const auto& statements = record.at("verifiedStatements");
        if (!statements.is_array() || statements.empty()) return false;
        for (const auto& statement : statements)
            if (!subjectMatches(statement, artifactDigest, policy.predicateType)) return false;
        result.integrity = true;
        result.signerIdentity = true;
        result.cacheHit = true;
        result.cacheExpiresAt = expiresAt;
        result.artifactSha256 = artifactDigest;
        result.verificationKey = verificationKey;
        result.verifiedStatements = statements;
        return true;
    } catch (const std::exception&) {
        return false; // malformed/old/partial records require a fresh verifier run
    }
}

} // namespace

bool attestationArguments(const AttestationOptions& o, const AttestationPolicy& p,
                          std::vector<std::string>& argv, std::string& error) {
    error.clear(); argv.clear();
    if (p.revoked) return failure(error, "attestation identity is revoked by current reviewer policy");
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
        platform::ProcessCaptureLimits limits;
        limits.timeoutMs = timeout;
        limits.maxStdoutBytes = argv == std::vector<std::string>{"--version"} ? 16 * 1024 : 16 * 1024 * 1024;
        limits.maxStderrBytes = 1024 * 1024;
        auto bounded = platform::runProcessCaptureBounded(exe, argv, limits);
        if (!bounded.diagnostic.empty()) bounded.stderrOutput = bounded.diagnostic + "\n" + bounded.stderrOutput;
        return static_cast<platform::CapturedProcessResult>(std::move(bounded));
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

AttestationResult verifyGitHubAttestationCached(const AttestationOptions& o, const AttestationPolicy& p,
                                               const AttestationCacheOptions& c, AttestationRunner runner) {
    AttestationResult result;
    std::string error;
    try {
        // Current policy and pinned input failures win even when a previous
        // acceptance exists. The cache stores successful signature checks only.
        const auto key = attestationVerificationKey(o, p, error);
        if (key.empty()) { result.diagnostics.push_back(error); return result; }
        fs::path directory, projectRoot;
        if (!cacheStore(o, c, directory, projectRoot, error)) {
            result.diagnostics.push_back(error); return result;
        }
        const auto cacheKey = cacheIdentity(key, c, projectRoot);
        const auto path = directory / (cacheKey + ".json");
        std::error_code ec;
        const auto status = fs::symlink_status(path, ec);
        if (ec && ec != std::errc::no_such_file_or_directory)
            throw std::runtime_error("cannot inspect attestation cache entry: " + ec.message());
        if (fs::exists(status)) {
            if (!privateStorePath(path, false, error)) { result.diagnostics.push_back(error); return result; }
            if (cachedResult(path, cacheKey, key, dist::sha256File(o.artifact.string()), p, c, result)) {
                if (attestationVerificationKey(o, p, error) != key) {
                    result = {};
                    result.diagnostics.push_back("attestation inputs changed during cache lookup"); return result;
                }
                // Rehashing a large artifact can cross the record's deadline.
                // A hot hit must still be valid after that final validation;
                // otherwise discard it and perform a fresh signature check.
                if (result.cacheExpiresAt > nowSeconds()) return result;
                result = {};
            }
        }
        const auto started = nowSeconds();
        result = verifyGitHubAttestation(o, p, std::move(runner));
        if (!result.integrity || !result.signerIdentity) return result;
        // Include verifier execution time in the lifetime; never refresh age on
        // a hit. A clock rollback or overlong verification prevents persistence.
        const auto now = nowSeconds();
        if (now < started || now >= started + c.maxAgeSeconds) return result;
        if (result.verificationKey != key || attestationVerificationKey(o, p, error) != key) {
            result = {}; result.diagnostics.push_back("attestation inputs changed before cache publication"); return result;
        }
        fs::path checkedDirectory, checkedProject;
        if (!cacheStore(o, c, checkedDirectory, checkedProject, error) || checkedDirectory != directory || checkedProject != projectRoot) {
            result = {}; result.diagnostics.push_back("attestation cache storage changed before publication: " + error); return result;
        }
        platform::TempFile temporary(directory, "attestation", ".tmp");
        if (!privateStorePath(temporary.path(), false, error)) {
            result.diagnostics.push_back(error); return result;
        }
        const auto expiresAt = started + c.maxAgeSeconds;
        const Json record = {{"type", "topo.attestation-cache-result"}, {"schemaVersion", 1},
                             {"cacheKey", cacheKey}, {"verificationKey", key},
                             {"artifactSha256", result.artifactSha256}, {"verifiedAt", started},
                             {"expiresAt", expiresAt}, {"verifiedStatements", result.verifiedStatements}};
        std::ofstream out(temporary.path(), std::ios::binary | std::ios::trunc);
        out << record.dump(); out.close();
        if (!out) throw std::runtime_error("cannot write attestation cache result");
        platform::atomicReplaceFile(temporary.path(), path);
        result.cacheExpiresAt = expiresAt;
    } catch (const std::exception& e) {
        result.diagnostics.push_back(std::string("attestation cache failed: ") + e.what());
    }
    return result;
}

} // namespace topo::build::evidence
