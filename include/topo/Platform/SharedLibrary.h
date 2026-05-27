#ifndef TOPO_PLATFORM_SHAREDLIBRARY_H
#define TOPO_PLATFORM_SHAREDLIBRARY_H

#include <string>

namespace topo::platform {

/// Cross-platform shared library loader.
/// Windows: LoadLibrary/GetProcAddress/FreeLibrary
/// Unix: dlopen/dlsym/dlclose
class SharedLibrary {
public:
    SharedLibrary() = default;
    ~SharedLibrary();

    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    SharedLibrary(SharedLibrary&& other) noexcept;
    SharedLibrary& operator=(SharedLibrary&& other) noexcept;

    /// Load a shared library by name or path.
    /// Returns true on success.
    bool load(const std::string& nameOrPath);

    /// Get a symbol address from the loaded library.
    /// Returns nullptr if not found.
    void* getSymbol(const std::string& name);

    /// Check if a library is currently loaded.
    bool isLoaded() const;

    /// Unload the library.
    void unload();

private:
    void* handle_ = nullptr;
};

/// Get the directory containing the current executable.
/// Returns empty string on failure.
std::string getExecutableDir();

} // namespace topo::platform

#endif // TOPO_PLATFORM_SHAREDLIBRARY_H
