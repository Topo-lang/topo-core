#ifndef TOPO_PLATFORM_FILEGLOB_H
#define TOPO_PLATFORM_FILEGLOB_H

#include <filesystem>
#include <string>
#include <vector>

namespace topo::platform {

/// Expand a single glob pattern (e.g. "src/*.cpp") relative to baseDir.
/// Supports only trailing '*' wildcard in the filename component.
std::vector<std::string> globExpand(const std::filesystem::path& baseDir, const std::string& pattern);

} // namespace topo::platform

#endif // TOPO_PLATFORM_FILEGLOB_H
