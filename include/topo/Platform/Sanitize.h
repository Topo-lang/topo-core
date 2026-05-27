#ifndef TOPO_PLATFORM_SANITIZE_H
#define TOPO_PLATFORM_SANITIZE_H

#include <filesystem>
#include <optional>
#include <string>

namespace topo::platform {

/// Canonicalise ``input`` and confirm it stays under ``root``.
///
/// The function:
///   1. Lexically normalises ``input`` (collapses ``.`` / ``..``).
///   2. If the path exists, calls ``fs::weakly_canonical`` so symlink
///      targets are resolved before the containment check (a symlink
///      pointing outside ``root`` is the audited threat).
///   3. Verifies the resolved path lies under the resolved ``root``.
///
/// Returns ``std::nullopt`` if any check fails — ``..`` segments after
/// normalisation, an absolute path escaping ``root``, or a symlink
/// crossing the root boundary. Callers must treat ``nullopt`` as a hard
/// reject and never fall back to the verbatim string.
///
/// Both ``input`` and ``root`` are interpreted relative to ``root``'s
/// own canonical form, so the caller does not need to pre-canonicalise.
std::optional<std::filesystem::path>
sanitizePath(const std::filesystem::path& input,
             const std::filesystem::path& root);

/// True iff ``child`` is identical to or a descendant of ``root`` after
/// both are lexically normalised. No filesystem access — pure path
/// algebra. Use this when you have already canonicalised both sides
/// (e.g. after ``sanitizePath``) and just need to re-affirm the
/// containment claim before opening / writing.
bool is_subpath(const std::filesystem::path& child,
                const std::filesystem::path& root);

/// True iff ``s`` is a "bare filename" with no directory components.
///
/// Accepts ``[A-Za-z0-9._-]+`` only, rejects:
///   - empty string
///   - any ``/`` or ``\``
///   - ``..`` or ``.`` as the whole name
///   - leading ``-`` (so the result cannot be mistaken for a CLI flag)
///   - any other character outside the alphanumeric / dot / underscore /
///     hyphen set (excludes spaces, shell metacharacters, control bytes,
///     and unicode that would not round-trip through POSIX path APIs).
///
/// This is the right check for manifest fields that must be filenames
/// (e.g. ``rules = "0.2-to-0.3.migration.toml"``), not subpaths.
bool is_safe_basename(const std::string& s);

} // namespace topo::platform

#endif // TOPO_PLATFORM_SANITIZE_H
