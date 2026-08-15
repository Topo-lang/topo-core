#include "topo/Platform/FileGlob.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace topo::platform {

namespace {

// Split a pattern into its path components. Empty segments (trailing or
// duplicate separators) and "." are dropped; "**" survives as a complete
// component so the matcher can give it recursive meaning.
std::vector<std::string> splitComponents(const fs::path& pattern) {
    std::vector<std::string> comps;
    for (const auto& part : pattern) {
        std::string comp = part.string();
        if (comp.empty() || comp == ".") continue;
        comps.push_back(std::move(comp));
    }
    return comps;
}

bool hasWildcard(const std::vector<std::string>& comps) {
    return std::any_of(comps.begin(), comps.end(), [](const std::string& c) {
        return c.find('*') != std::string::npos;
    });
}

// Match a filename against one pattern component. A single '*' is anchored
// prefix/suffix (the pre-'**' behaviour); a component without '*' compares
// literally.
bool matchFilename(const std::string& name, const std::string& pattern) {
    auto starPos = pattern.find('*');
    if (starPos == std::string::npos) return name == pattern;
    std::string prefix = pattern.substr(0, starPos);
    std::string suffix = pattern.substr(starPos + 1);
    bool matchPrefix = prefix.empty() || name.substr(0, prefix.size()) == prefix;
    bool matchSuffix = suffix.empty() ||
                       (name.size() >= suffix.size() &&
                        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0);
    return matchPrefix && matchSuffix;
}

// Recursive matcher over the pattern components.
//  - "**" matches zero or more directory segments: the remainder of the
//    pattern is tried at the current level (zero segments) and at every
//    deeper level (each subdirectory consumes one segment of "**"). As the
//    final component it also collects every regular file at and below the
//    current directory.
//  - Any other component is literal until the last one; the final component
//    is matched against regular-file names via matchFilename.
// Symlinked directories are never descended into (symlink_status, no
// follow), so a link cycle cannot cause unbounded recursion.
void collectMatches(const fs::path& dir, const std::vector<std::string>& comps,
                    size_t index, std::vector<std::string>& out) {
    if (index >= comps.size()) return;

    if (comps[index] == "**") {
        const bool isTail = (index + 1 == comps.size());
        std::error_code ec;
        fs::directory_iterator it(dir, ec);
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            const fs::directory_entry& entry = *it;
            fs::file_status status = entry.symlink_status(ec);
            if (ec) break;
            if (fs::is_directory(status)) {
                // A subdirectory consumes one segment of "**".
                collectMatches(entry.path(), comps, index, out);
            } else if (isTail && fs::is_regular_file(status)) {
                out.push_back(entry.path().string());
            }
        }
        // Zero segments: skip "**" and try the remainder of the pattern here.
        collectMatches(dir, comps, index + 1, out);
        return;
    }

    if (index + 1 == comps.size()) {
        std::error_code ec;
        fs::directory_iterator it(dir, ec);
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            const fs::directory_entry& entry = *it;
            if (!entry.is_regular_file(ec)) continue;
            if (matchFilename(entry.path().filename().string(), comps[index])) {
                out.push_back(entry.path().string());
            }
        }
        return;
    }

    // Intermediate literal component.
    fs::path next = dir / comps[index];
    std::error_code ec;
    if (fs::is_directory(next, ec)) {
        collectMatches(next, comps, index + 1, out);
    }
}

} // namespace

std::vector<std::string> globExpand(const fs::path& baseDir, const std::string& pattern) {
    std::vector<std::string> comps = splitComponents(fs::path(pattern));

    if (!hasWildcard(comps)) {
        // No wildcard — literal path. A file OR a directory the caller
        // consumes whole (e.g. sources = ["src"] for the java/python/
        // typescript drivers that recurse internally).
        fs::path full = baseDir / pattern;
        std::error_code ec;
        if (fs::exists(full, ec)) return {full.string()};
        return {};
    }

    std::vector<std::string> result;
    collectMatches(baseDir, comps, 0, result);
    std::sort(result.begin(), result.end());
    // Adjacent '**' segments (e.g. "src/**/**/*.cpp") match the same file
    // through more than one decomposition of the path — collapse them, as
    // common glob implementations do.
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace topo::platform
