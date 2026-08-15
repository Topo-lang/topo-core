#ifndef TOPO_PLATFORM_FILEGLOB_H
#define TOPO_PLATFORM_FILEGLOB_H

#include <filesystem>
#include <string>
#include <vector>

namespace topo::platform {

/// Expand a single glob pattern (e.g. "src/*.cpp", "src/**/*.cpp")
/// relative to baseDir.
///
/// Supported syntax:
///  - A single '*' in the filename component matches any run of characters
///    (anchored prefix/suffix around the star).
///  - '**' as a complete path component matches zero or more directory
///    segments (recursive): "src/**/*.cpp" covers every .cpp below src/ at
///    any depth, and a trailing '**' collects every file below the
///    preceding directory. Symlinked directories are not followed during
///    '**' descent.
///  - All other components are matched literally, including wildcard
///    characters in directory positions other than a complete '**'.
/// A pattern with no wildcard is existence-checked literally (files and
/// directories both match). Results are sorted and duplicate-free.
std::vector<std::string> globExpand(const std::filesystem::path& baseDir, const std::string& pattern);

} // namespace topo::platform

#endif // TOPO_PLATFORM_FILEGLOB_H
