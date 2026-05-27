#include "topo/Platform/ToolResolution.h"
#include "topo/Platform/Platform.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace topo::platform {

std::string resolveLLVMTool(const std::string& toolName) {
#ifdef TOPO_LLVM_BINDIR
    fs::path bundled = fs::path(TOPO_LLVM_BINDIR) / toolName;
    if constexpr (IsWindows) {
        if (!fs::exists(bundled) && bundled.extension().empty()) {
            bundled = fs::path(TOPO_LLVM_BINDIR) / (toolName + std::string(ExeSuffix));
        }
    }
    if (fs::exists(bundled)) {
        return bundled.string();
    }
#endif
    // Fallback to PATH
    return toolName;
}

namespace {

/// Split PATH into directory components using the platform separator
/// (``;`` on Windows, ``:`` elsewhere).
std::vector<fs::path> pathDirs() {
    std::vector<fs::path> out;
    const char* p = std::getenv("PATH");
    if (p == nullptr) return out;
    std::string s(p);
    std::string sep(PathSeparator);
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(sep, start);
        std::string token = (end == std::string::npos) ? s.substr(start) : s.substr(start, end - start);
        if (!token.empty()) out.emplace_back(token);
        if (end == std::string::npos) break;
        start = end + sep.size();
    }
    return out;
}

/// On Windows, the suffix list a name should be probed with (``""`` is
/// also included to honour names that already carry a ``.exe``/``.bat``
/// suffix). On POSIX, only the empty suffix is meaningful.
std::vector<std::string> pathExtensions() {
    if constexpr (!IsWindows) {
        return {""};
    }
    std::vector<std::string> out;
    out.push_back("");
    const char* pathExt = std::getenv("PATHEXT");
    std::string raw = pathExt ? pathExt : ".COM;.EXE;.BAT;.CMD;.VBS;.JS;.WS;.MSC";
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ';')) {
        if (!token.empty()) out.push_back(token);
    }
    return out;
}

} // namespace

std::string findOnPath(const std::string& name) {
    if (name.empty()) return {};
    // Absolute / relative path with directory: don't search PATH.
    fs::path nameAsPath(name);
    if (nameAsPath.has_parent_path()) {
        std::error_code ec;
        if (fs::is_regular_file(nameAsPath, ec)) return fs::absolute(nameAsPath, ec).string();
        return {};
    }

    auto dirs = pathDirs();
    auto exts = pathExtensions();
    for (const auto& dir : dirs) {
        for (const auto& ext : exts) {
            fs::path candidate = dir / (name + ext);
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) {
                return candidate.string();
            }
        }
    }
    return {};
}

std::vector<std::string> findPythonInterpreter() {
    // Explicit override always wins.
    for (const char* env : {"TOPO_PYTHON", "TOPO_PYTHON_EXE"}) {
        const char* v = std::getenv(env);
        if (v != nullptr && *v) {
            std::string s(v);
            // Honour both an absolute path and a bare command name.
            std::error_code ec;
            if (fs::is_regular_file(fs::path(s), ec)) {
                return {s};
            }
            std::string resolved = findOnPath(s);
            if (!resolved.empty()) return {resolved};
        }
    }

    // Standard candidates: ``python3`` first on POSIX (the conventional
    // launcher name), then plain ``python`` (Windows default and some
    // venv layouts).
    const char* names[] = {"python3", "python"};
    for (const char* n : names) {
        std::string hit = findOnPath(n);
        if (!hit.empty()) return {hit};
    }

    // Windows launcher: ``py -3`` dispatches to whatever Python 3 the
    // user installed.
    std::string py = findOnPath("py");
    if (!py.empty()) return {py, "-3"};

    return {};
}

} // namespace topo::platform
