#ifndef TOPO_DISTRIBUTION_SEMVER_H
#define TOPO_DISTRIBUTION_SEMVER_H

/// Minimal SemVer 2.0.0 support for backend distribution.
///
/// Two needs only:
///  - order two backend versions (`update` picks the newest),
///  - test a `core_compat` range against the running topo-core version.
///
/// The range grammar accepted is the comma-separated comparator set the spec
/// uses, e.g. `">=4.0.0, <5.0.0"`. Comparators: `=`, `>`, `>=`, `<`, `<=`.
/// Pre-release / build metadata is parsed but compared only by the numeric
/// core triple (sufficient for the distribution use case).

#include <string>
#include <vector>

namespace topo::dist {

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease;  // text after '-', before '+'
    bool valid = false;

    /// Compare core triples only. Returns -1/0/1.
    int compareCore(const SemVer& other) const;
};

/// Parse "X.Y.Z" (optionally "-pre" / "+build"). On failure, .valid is false.
SemVer parseSemVer(const std::string& text);

/// Test whether `version` satisfies the comma-separated comparator `range`.
/// An empty range matches everything. A malformed range fails closed (false).
bool satisfiesRange(const SemVer& version, const std::string& range);

/// Convenience: parse both, then test. False if either side is malformed.
bool satisfiesRange(const std::string& version, const std::string& range);

} // namespace topo::dist

#endif // TOPO_DISTRIBUTION_SEMVER_H
