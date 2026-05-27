#ifndef TOPO_PLATFORM_FILELOCK_H
#define TOPO_PLATFORM_FILELOCK_H

#include <filesystem>
#include <string>

namespace topo::platform {

/// Cross-platform advisory file lock.
///
/// POSIX backend uses ``flock(LOCK_EX)``; Windows backend uses
/// ``LockFileEx(LOCKFILE_EXCLUSIVE_LOCK)``. The lock is advisory —
/// other code that does not call ``FileLock`` can still write the
/// guarded file. Use this as the discipline boundary for cooperating
/// processes (multiple ``tpm`` invocations, an editor save concurrent
/// with a CLI run) that all agree to take the lock before mutating
/// the shared resource.
///
/// Semantics:
///   - Construction: opens / creates ``lockPath`` (whose parent must
///     exist) but does NOT take the lock. Call ``acquire`` to block
///     until the lock is held; ``tryAcquire`` for the non-blocking
///     variant.
///   - Destruction: releases the lock if held and closes the file
///     descriptor / handle. The lock file itself is left on disk —
///     the same path is reused by the next process.
///   - Move-only. Copying would double-release.
///
/// Failure modes are reported through the ``ok()`` / ``error()``
/// accessors instead of exceptions so the destructor stays
/// noexcept-safe.
class FileLock {
public:
    /// Construct, opening (creating if absent) ``lockPath``. The
    /// directory of ``lockPath`` MUST already exist — the helper does
    /// not create intermediate directories.
    explicit FileLock(const std::filesystem::path& lockPath);
    ~FileLock();

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;

    /// True iff the lock file was successfully opened. On false,
    /// ``error()`` carries the diagnostic and ``acquire`` /
    /// ``tryAcquire`` will return false without attempting anything.
    bool ok() const;

    /// Last error message, or empty when ok.
    const std::string& error() const { return error_; }

    /// Block until the lock is acquired. Returns false on
    /// open / lock failure (see ``error()``).
    bool acquire();

    /// Non-blocking acquire. Returns true if the lock was taken,
    /// false if another process holds it OR an error occurred (use
    /// ``error()`` to distinguish — error is set only on real
    /// failure, not on "lock contended").
    bool tryAcquire();

    /// True iff this instance currently holds the lock.
    bool held() const { return held_; }

    /// Release the lock if held. Idempotent; safe to call from the
    /// destructor.
    void release();

private:
    std::filesystem::path lockPath_;
    bool held_ = false;
    std::string error_;
#ifdef _WIN32
    void* handle_ = nullptr; // HANDLE
#else
    int fd_ = -1;
#endif
};

} // namespace topo::platform

#endif // TOPO_PLATFORM_FILELOCK_H
