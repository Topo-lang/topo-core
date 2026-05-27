#include "topo/Platform/Sanitize.h"

#include <cctype>
#include <system_error>

namespace fs = std::filesystem;

namespace topo::platform {

namespace {

/// Resolve the path filesystem-side when possible (follows symlinks),
/// otherwise return a lexically normalised form. ``weakly_canonical``
/// gives us "as much resolution as the on-disk parts allow" so a not-
/// yet-created destination path still gets the existing prefix
/// resolved — exactly the semantics we need for the containment check.
fs::path resolveOrNormalize(const fs::path& p) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(p, ec);
    if (ec || resolved.empty()) {
        return p.lexically_normal();
    }
    return resolved;
}

bool hasParentRefSegment(const fs::path& p) {
    // Lexically normalised paths still carry leading ".." segments when
    // they would escape the start point (lexically_normal cannot know
    // there is no "above"). We treat any ".." after normalisation as
    // a hard reject; callers that need a "go up but stay inside root"
    // shape should pass an absolute path so the relative ".." is
    // already collapsed.
    for (const auto& seg : p) {
        if (seg == "..") return true;
    }
    return false;
}

} // namespace

std::optional<fs::path> sanitizePath(const fs::path& input,
                                     const fs::path& root) {
    if (input.empty() || root.empty()) return std::nullopt;

    fs::path canonicalRoot = resolveOrNormalize(root);
    fs::path joined = input.is_absolute() ? input : (canonicalRoot / input);

    // Lexical reject of any residual ``..`` *before* we touch the
    // filesystem. A path like ``root/../etc/passwd`` resolves to
    // ``/etc/passwd``; that is exactly what an attacker wants and what
    // the containment check on the resolved form would correctly reject
    // anyway, but doing the lexical reject first means a malicious
    // input cannot exercise the on-disk follow path at all.
    fs::path normalisedJoin = joined.lexically_normal();
    if (hasParentRefSegment(normalisedJoin)) {
        // After normalising an absolute path, no ".." should remain
        // unless the path tried to escape the filesystem root, which is
        // already nonsense; treat as a reject. For relative inputs the
        // join above guarantees an absolute starting point, so any
        // surviving ".." is an escape attempt.
        return std::nullopt;
    }

    fs::path canonicalInput = resolveOrNormalize(normalisedJoin);
    if (!is_subpath(canonicalInput, canonicalRoot)) return std::nullopt;
    return canonicalInput;
}

bool is_subpath(const fs::path& child, const fs::path& root) {
    fs::path c = child.lexically_normal();
    fs::path r = root.lexically_normal();

    auto cit = c.begin();
    auto rit = r.begin();
    for (; rit != r.end(); ++rit, ++cit) {
        if (cit == c.end()) return false;
        if (*cit != *rit) return false;
    }
    return true;
}

bool is_safe_basename(const std::string& s) {
    if (s.empty()) return false;
    if (s == "." || s == "..") return false;
    if (s.front() == '-') return false;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = std::isalnum(u) || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    // Reject ``..`` substring used to compose a parent-ref segment even
    // though no single char matched. ``foo..bar`` is fine; bare ``..``
    // already failed above. ``a/../b`` would not reach here because
    // ``/`` is not in the allowed set.
    return true;
}

} // namespace topo::platform
