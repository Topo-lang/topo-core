#include "topo/Platform/SharedLibrary.h"

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace topo::platform {

SharedLibrary::~SharedLibrary() {
    unload();
}

SharedLibrary::SharedLibrary(SharedLibrary&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

SharedLibrary& SharedLibrary::operator=(SharedLibrary&& other) noexcept {
    if (this != &other) {
        unload();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool SharedLibrary::load(const std::string& nameOrPath) {
    unload();
#ifdef _WIN32
    handle_ = static_cast<void*>(LoadLibraryA(nameOrPath.c_str()));
#else
    handle_ = dlopen(nameOrPath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    return handle_ != nullptr;
}

void* SharedLibrary::getSymbol(const std::string& name) {
    if (!handle_) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name.c_str()));
#else
    return dlsym(handle_, name.c_str());
#endif
}

bool SharedLibrary::isLoaded() const {
    return handle_ != nullptr;
}

void SharedLibrary::unload() {
    if (!handle_) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

std::string getExecutableDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";
    return fs::path(std::string(buf, len)).parent_path().string();
#elif defined(__APPLE__)
    char buf[1024];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return "";
    std::error_code ec;
    auto resolved = fs::canonical(fs::path(buf), ec);
    if (ec) return fs::path(buf).parent_path().string();
    return resolved.parent_path().string();
#else
    // Linux: readlink /proc/self/exe
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "";
    buf[len] = '\0';
    return fs::path(buf).parent_path().string();
#endif
}

} // namespace topo::platform
