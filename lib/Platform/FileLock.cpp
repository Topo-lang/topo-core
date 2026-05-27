#include "topo/Platform/FileLock.h"

#include <cstring>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace topo::platform {

namespace {

#ifdef _WIN32

std::string lastWin32Error() {
    DWORD err = ::GetLastError();
    LPSTR buf = nullptr;
    DWORD len = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buf), 0, nullptr);
    std::string msg;
    if (len > 0 && buf) {
        msg.assign(buf, len);
        // FormatMessage appends \r\n; strip.
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
            msg.pop_back();
        }
    } else {
        msg = "Win32 error " + std::to_string(err);
    }
    if (buf) ::LocalFree(buf);
    return msg;
}

#endif

} // namespace

FileLock::FileLock(const std::filesystem::path& lockPath)
    : lockPath_(lockPath) {
#ifdef _WIN32
    HANDLE h = ::CreateFileW(
        lockPath_.wstring().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error_ = "FileLock open(" + lockPath_.string() + "): " + lastWin32Error();
        handle_ = nullptr;
        return;
    }
    handle_ = h;
#else
    int fd = ::open(lockPath_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        error_ = "FileLock open(" + lockPath_.string() + "): " + std::strerror(errno);
        fd_ = -1;
        return;
    }
    fd_ = fd;
#endif
}

FileLock::~FileLock() {
    release();
#ifdef _WIN32
    if (handle_) {
        ::CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

FileLock::FileLock(FileLock&& other) noexcept
    : lockPath_(std::move(other.lockPath_)),
      held_(other.held_),
      error_(std::move(other.error_))
#ifdef _WIN32
    , handle_(other.handle_)
#else
    , fd_(other.fd_)
#endif
{
    other.held_ = false;
#ifdef _WIN32
    other.handle_ = nullptr;
#else
    other.fd_ = -1;
#endif
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        release();
#ifdef _WIN32
        if (handle_) ::CloseHandle(static_cast<HANDLE>(handle_));
#else
        if (fd_ >= 0) ::close(fd_);
#endif
        lockPath_ = std::move(other.lockPath_);
        held_ = other.held_;
        error_ = std::move(other.error_);
#ifdef _WIN32
        handle_ = other.handle_;
        other.handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        other.held_ = false;
    }
    return *this;
}

bool FileLock::ok() const {
#ifdef _WIN32
    return handle_ != nullptr;
#else
    return fd_ >= 0;
#endif
}

bool FileLock::acquire() {
    if (held_) return true;
    if (!ok()) return false;
#ifdef _WIN32
    OVERLAPPED ov{};
    BOOL r = ::LockFileEx(
        static_cast<HANDLE>(handle_),
        LOCKFILE_EXCLUSIVE_LOCK,
        0,
        MAXDWORD, MAXDWORD,
        &ov);
    if (!r) {
        error_ = "FileLock acquire(" + lockPath_.string() + "): " + lastWin32Error();
        return false;
    }
    held_ = true;
    return true;
#else
    if (::flock(fd_, LOCK_EX) != 0) {
        error_ = "FileLock acquire(" + lockPath_.string() + "): " + std::strerror(errno);
        return false;
    }
    held_ = true;
    return true;
#endif
}

bool FileLock::tryAcquire() {
    if (held_) return true;
    if (!ok()) return false;
#ifdef _WIN32
    OVERLAPPED ov{};
    BOOL r = ::LockFileEx(
        static_cast<HANDLE>(handle_),
        LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
        0,
        MAXDWORD, MAXDWORD,
        &ov);
    if (!r) {
        DWORD err = ::GetLastError();
        if (err == ERROR_LOCK_VIOLATION || err == ERROR_IO_PENDING) {
            return false; // contended, not error
        }
        error_ = "FileLock tryAcquire(" + lockPath_.string() + "): " + lastWin32Error();
        return false;
    }
    held_ = true;
    return true;
#else
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            return false; // contended, not error
        }
        error_ = "FileLock tryAcquire(" + lockPath_.string() + "): " + std::strerror(errno);
        return false;
    }
    held_ = true;
    return true;
#endif
}

void FileLock::release() {
    if (!held_) return;
#ifdef _WIN32
    if (handle_) {
        OVERLAPPED ov{};
        ::UnlockFileEx(static_cast<HANDLE>(handle_), 0, MAXDWORD, MAXDWORD, &ov);
    }
#else
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
    }
#endif
    held_ = false;
}

} // namespace topo::platform
