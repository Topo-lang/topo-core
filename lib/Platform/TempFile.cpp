#include "topo/Platform/TempFile.h"

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace topo::platform {

namespace {

/// Atomic counter to disambiguate temp files created within the same
/// process. Combined with the pid this gives a unique key without
/// depending on a system random source.
std::atomic<unsigned long> g_counter{0};

int currentPid() {
#ifdef _WIN32
    return static_cast<int>(_getpid());
#else
    return static_cast<int>(::getpid());
#endif
}

bool createExclusive(const fs::path& p) {
#ifdef _WIN32
    HANDLE h = CreateFileA(p.string().c_str(),
                           GENERIC_WRITE,
                           0,
                           nullptr,
                           CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
#else
    int fd = ::open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) return false;
    ::close(fd);
    return true;
#endif
}

#ifdef _WIN32
std::error_code lastPathError() {
    const DWORD error = GetLastError();
    return {static_cast<int>(error ? error : ERROR_GEN_FAILURE), std::system_category()};
}
#endif

} // namespace

fs::path tempDirectory() {
    auto tryEnv = [](const char* name) -> fs::path {
        const char* v = std::getenv(name);
        if (v == nullptr) return {};
        std::string s(v);
        if (s.empty()) return {};
        fs::path p(s);
        std::error_code ec;
        if (fs::is_directory(p, ec)) return p;
        return {};
    };

    // POSIX-first: TMPDIR is the documented override for tools / CI /
    // containers. On Linux/macOS this is what shell scripts and the
    // standard library both honour.
    if (auto p = tryEnv("TMPDIR"); !p.empty()) return p;

    // systemd's per-user runtime dir on Linux; some confined
    // environments only allow writes here.
    if (auto p = tryEnv("XDG_RUNTIME_DIR"); !p.empty()) return p;

    // Windows convention: GetTempPath honours TMP / TEMP / USERPROFILE
    // in that order. We probe them explicitly first so the *user*
    // override semantics are uniform with POSIX (env wins outright).
    if (auto p = tryEnv("TEMP"); !p.empty()) return p;
    if (auto p = tryEnv("TMP"); !p.empty()) return p;

    // Standard library fallback: GetTempPath on Windows, /tmp on POSIX.
    std::error_code ec;
    fs::path sysTemp = fs::temp_directory_path(ec);
    if (!ec && !sysTemp.empty()) return sysTemp;

    // Last-ditch: relative cwd. Never reached on a sane system, but
    // beats throwing in a header-only call path.
    return fs::path(".");
}

fs::path homeDirectory() {
    auto fromEnv = [](const char* name) -> fs::path {
        const char* v = std::getenv(name);
        if (v == nullptr || *v == '\0') return {};
        return fs::path(v);
    };

#ifdef _WIN32
    if (auto p = fromEnv("USERPROFILE"); !p.empty()) return p;
    const char* drive = std::getenv("HOMEDRIVE");
    const char* path = std::getenv("HOMEPATH");
    if (drive != nullptr && *drive != '\0' && path != nullptr && *path != '\0') {
        return fs::path(std::string(drive) + path);
    }
    return {};
#else
    if (auto p = fromEnv("HOME"); !p.empty()) return p;
    return {};
#endif
}

fs::path canonicalPath(const fs::path& path, std::error_code& error) {
#ifdef _WIN32
    error.clear();
    const HANDLE handle = CreateFileW(path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = lastPathError();
        return {};
    }
    struct ClosePathHandle {
        HANDLE value;
        ~ClosePathHandle() { CloseHandle(value); }
    } close{handle};

    // Bound both allocation and retries if another process renames the opened
    // target while Windows is reporting the required buffer length.
    constexpr DWORD maxPathCharacters = 32768;
    std::wstring resolved(256, L'\0');
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        const DWORD length = GetFinalPathNameByHandleW(handle, resolved.data(),
            static_cast<DWORD>(resolved.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length == 0) {
            error = lastPathError();
            return {};
        }
        if (length > maxPathCharacters) {
            error = {ERROR_FILENAME_EXCED_RANGE, std::system_category()};
            return {};
        }
        if (length >= resolved.size()) {
            // A short-buffer result already includes the terminating null.
            resolved.resize(length);
            continue;
        }
        resolved.resize(length);
        if (resolved.compare(0, 8, L"\\\\?\\UNC\\") == 0) {
            resolved.replace(0, 8, L"\\\\");
        } else if (resolved.size() >= 7 && resolved.compare(0, 4, L"\\\\?\\") == 0 &&
                   ((resolved[4] >= L'A' && resolved[4] <= L'Z') ||
                    (resolved[4] >= L'a' && resolved[4] <= L'z')) &&
                   resolved[5] == L':' && resolved[6] == L'\\') {
            resolved.erase(0, 4);
        }
        return fs::path(std::move(resolved));
    }
    error = {ERROR_RETRY, std::system_category()};
    return {};
#else
    return fs::canonical(path, error);
#endif
}

bool isPathRedirect(const fs::path& path, std::error_code& error) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        error = lastPathError();
        return false;
    }
    error.clear();
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    const auto status = fs::symlink_status(path, error);
    if (!error && status.type() == fs::file_type::not_found)
        error = std::make_error_code(std::errc::no_such_file_or_directory);
    return !error && fs::is_symlink(status);
#endif
}

TempFile::TempFile(const std::string& stem, const std::string& ext)
    : TempFile(tempDirectory(), stem, ext) {}

void atomicReplaceFile(const fs::path& source, const fs::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(GetLastError(), std::system_category(), "atomic file replacement");
#else
    fs::rename(source, destination);
#endif
}

TempFile::TempFile(const fs::path& dir, const std::string& stem, const std::string& ext) {
    int pid = currentPid();

    // Probe up to 1024 candidate names with an exclusive-create. The
    // counter+pid combination is unique within a process; collisions
    // only happen if two unrelated processes race on the same stem,
    // which the create-with-EXCL semantics already resolve.
    for (int attempt = 0; attempt < 1024; ++attempt) {
        unsigned long n = g_counter.fetch_add(1, std::memory_order_relaxed);
        std::string filename = stem + "-" + std::to_string(pid) + "-" + std::to_string(n) + ext;
        fs::path candidate = dir / filename;
        if (createExclusive(candidate)) {
            path_ = std::move(candidate);
            return;
        }
    }

    throw std::runtime_error("TempFile: could not create unique file in '" + dir.string() +
                             "' after 1024 attempts");
}

TempFile::TempFile(TempFile&& other) noexcept : path_(std::move(other.path_)), owned_(other.owned_) {
    other.owned_ = false;
}

TempFile& TempFile::operator=(TempFile&& other) noexcept {
    if (this != &other) {
        if (owned_ && !path_.empty()) {
            std::error_code ec;
            fs::remove(path_, ec);
        }
        path_ = std::move(other.path_);
        owned_ = other.owned_;
        other.owned_ = false;
    }
    return *this;
}

TempFile::~TempFile() {
    if (owned_ && !path_.empty()) {
        std::error_code ec;
        fs::remove(path_, ec);
    }
}

fs::path TempFile::release() {
    owned_ = false;
    return path_;
}

} // namespace topo::platform
