#include "topo/Platform/FileGlob.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace topo::platform {

std::vector<std::string> globExpand(const fs::path& baseDir, const std::string& pattern) {
    fs::path pat(pattern);
    fs::path dir = baseDir / pat.parent_path();
    std::string filePattern = pat.filename().string();

    // Check if the pattern contains a wildcard
    auto starPos = filePattern.find('*');
    if (starPos == std::string::npos) {
        // No wildcard — literal path
        fs::path full = baseDir / pattern;
        if (fs::exists(full)) return {full.string()};
        return {};
    }

    std::string prefix = filePattern.substr(0, starPos);
    std::string suffix = filePattern.substr(starPos + 1);

    std::vector<std::string> result;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        bool matchPrefix = (prefix.empty() || name.substr(0, prefix.size()) == prefix);
        bool matchSuffix = (suffix.empty() || (name.size() >= suffix.size() &&
                                               name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0));
        if (matchPrefix && matchSuffix) {
            result.push_back(entry.path().string());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace topo::platform
