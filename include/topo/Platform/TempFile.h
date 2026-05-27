#ifndef TOPO_PLATFORM_TEMPFILE_H
#define TOPO_PLATFORM_TEMPFILE_H

#include <filesystem>
#include <string>

namespace topo::platform {

/// Resolve the platform temp directory.
///
/// Resolution order:
///   1. `TMPDIR` (POSIX convention, honoured by tools / containers)
///   2. `XDG_RUNTIME_DIR` (systemd user-runtime dir on Linux)
///   3. `TEMP` / `TMP` (Windows fallbacks before std::filesystem)
///   4. `std::filesystem::temp_directory_path()` (delegates to
///      `GetTempPath` on Windows, `/tmp` on POSIX)
///
/// Hard-coding `/tmp/` is the audited mistake — Windows has no `/tmp/`
/// directory at all, and the POSIX call sites that hard-coded it also
/// had a race-condition footgun (predictable path + later `open`).
std::filesystem::path tempDirectory();

/// Resolve the current user's home directory, cross-platform.
///
/// Resolution order:
///   - Windows: `USERPROFILE`, then `HOMEDRIVE+HOMEPATH`
///   - POSIX: `HOME`
///
/// Returns an empty path when no env var is set (rare in any sane
/// environment, but the caller MUST handle it — falling back to e.g.
/// the temp directory is a reasonable strategy for caches). The bare
/// `getenv("HOME")` pattern is forbidden by the no-bare-home-getenv
/// audit gate because it silently breaks on Windows.
std::filesystem::path homeDirectory();

/// RAII handle to a freshly created, uniquely-named temporary file.
///
/// On construction:
///   - Resolves the platform temp directory (see `tempDirectory()`).
///   - Picks a filename of the form `<stem>-<pid>-<counter>.<ext>` that
///     does not collide with any existing file in the temp dir.
///   - Creates the file (atomic O_CREAT|O_EXCL on POSIX, CreateFileA
///     CREATE_NEW on Windows). This eliminates the
///     predictable-name-then-open race.
///
/// On destruction:
///   - Removes the file from disk (best effort; ignores errors). Call
///     `release()` to keep the file (e.g. when the caller has moved it
///     to a permanent location).
///
/// Move-only. Not thread-safe per-instance; the unique-name probe is
/// thread-safe across instances (uses a static atomic counter + pid).
class TempFile {
public:
    /// Create a new temp file.
    ///
    /// `stem` becomes the leading component of the filename (defaults to
    /// `"topo"`). `ext` is appended verbatim including any leading `.`
    /// (defaults to empty — no extension). Throws `std::runtime_error`
    /// on creation failure.
    explicit TempFile(const std::string& stem = "topo", const std::string& ext = "");

    /// Move construction is fine.
    TempFile(TempFile&& other) noexcept;
    TempFile& operator=(TempFile&& other) noexcept;

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    /// Best-effort remove. Errors are swallowed.
    ~TempFile();

    /// Absolute path of the temp file.
    const std::filesystem::path& path() const { return path_; }

    /// Release ownership: the file is *not* removed at destruction.
    /// Returns the path so the caller can record it.
    std::filesystem::path release();

private:
    std::filesystem::path path_;
    bool owned_ = true;
};

} // namespace topo::platform

#endif // TOPO_PLATFORM_TEMPFILE_H
