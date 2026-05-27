#ifndef TOPO_DISTRIBUTION_BACKENDMANIFEST_H
#define TOPO_DISTRIBUTION_BACKENDMANIFEST_H

/// In-memory model + parser for `topo-backend.toml`.
///
/// Every distributable backend ships exactly one of these at its package
/// root; it is the single source of truth for identity, compatibility, and
/// payload layout.

#include <map>
#include <string>
#include <vector>

namespace topo::dist {

/// `[backend]` — identity.
struct BackendIdentity {
    std::string name;                     // [a-z0-9-]+, registry-unique
    std::string version;                  // SemVer 2.0.0, backend-owned
    std::vector<std::string> language;    // HostLanguage spellings
    std::string coreCompat;               // SemVer range, e.g. ">=4.0.0, <5.0.0"
    std::string description;              // optional one-liner
};

/// `[platform]` — the platform triple this package targets.
struct PlatformTriple {
    std::string os;     // linux | macos | windows
    std::string arch;   // x86_64 | aarch64
    std::string libc;   // glibc | musl (Linux only); empty elsewhere

    /// Canonical "<os>-<arch>" (libc omitted) used by the registry index.
    std::string str() const;
};

/// One entry in `[runtime.*]` — a named non-executable payload group.
struct RuntimeGroup {
    std::string name;                  // table key, e.g. "c-abi"
    std::string kind;                  // native-lib | headers | jar | toolchain
    std::vector<std::string> files;    // package-relative; trailing '/' = dir
    std::string install;               // path under the cache install root
    std::map<std::string, std::string> env;  // PATH fragments the CLI exposes
};

/// `[signature]` — Ed25519 signature block.
struct SignatureBlock {
    std::string algorithm;   // fixed "ed25519"
    std::string keyId;       // publisher key fingerprint (hex)
    std::string value;       // base64 Ed25519 signature
};

/// A fully-parsed `topo-backend.toml`.
struct BackendManifest {
    BackendIdentity backend;
    std::map<std::string, std::string> binaries;  // tool-name -> rel-path
    std::vector<RuntimeGroup> runtime;
    PlatformTriple platform;
    SignatureBlock signature;

    /// Every package-relative payload path named in [binaries] and
    /// [runtime], sorted — the basis of the canonical digest.
    std::vector<std::string> payloadPaths() const;
};

/// Parse result. `ok` is false when a required key is missing or malformed.
struct ManifestParseResult {
    bool ok = false;
    BackendManifest manifest;
    std::string error;   // human-readable, empty when ok
};

/// Parse a `topo-backend.toml` from a file path.
ManifestParseResult parseBackendManifestFile(const std::string& path);

/// Parse a `topo-backend.toml` from an in-memory string (sourceLabel is used
/// only for error messages).
ManifestParseResult parseBackendManifest(const std::string& tomlText,
                                         const std::string& sourceLabel);

} // namespace topo::dist

#endif // TOPO_DISTRIBUTION_BACKENDMANIFEST_H
