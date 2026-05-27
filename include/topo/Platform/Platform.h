#ifndef TOPO_PLATFORM_PLATFORM_H
#define TOPO_PLATFORM_PLATFORM_H

#include <cstdint>
#include <string>
#include <string_view>

namespace topo::platform {

/// Check if str ends with the given suffix.
inline bool endsWith(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

#ifdef _WIN32
inline constexpr bool IsWindows = true;
inline constexpr bool IsMacOS = false;
inline constexpr bool IsLinux = false;
#elif defined(__APPLE__)
inline constexpr bool IsWindows = false;
inline constexpr bool IsMacOS = true;
inline constexpr bool IsLinux = false;
#else
inline constexpr bool IsWindows = false;
inline constexpr bool IsMacOS = false;
inline constexpr bool IsLinux = true;
#endif

inline constexpr std::string_view ExeSuffix = IsWindows ? ".exe" : "";

inline constexpr std::string_view SharedLibSuffix = IsWindows ? ".dll" : (IsMacOS ? ".dylib" : ".so");

inline constexpr std::string_view StaticLibSuffix = IsWindows ? ".lib" : ".a";

inline constexpr std::string_view ObjectFileSuffix = IsWindows ? ".obj" : ".o";

inline constexpr std::string_view NullDevice = IsWindows ? "NUL" : "/dev/null";

inline constexpr std::string_view PathSeparator = IsWindows ? ";" : ":";

/// Returns total physical RAM in bytes, or 0 on failure.
uint64_t totalPhysicalMemory();

/// Returns currently available (free + reclaimable) RAM in bytes, or 0 on failure.
uint64_t availableMemory();

} // namespace topo::platform

#endif // TOPO_PLATFORM_PLATFORM_H
