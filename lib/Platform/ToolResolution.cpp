#include "topo/Platform/ToolResolution.h"
#include "topo/Platform/Platform.h"
#include "topo/Platform/Process.h"
#include "topo/Platform/TempFile.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace topo::platform {

std::string resolveLLVMTool(const std::string& toolName) {
    // Thin shim — the BYO-priority chain lives in resolveLLVMToolchain().
    return llvmToolPath(toolName);
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

namespace {

/// Suffix probe order for findSpawnableOnPath. Differs from PATHEXT (used
/// by findOnPath) in two deliberate ways: only suffixes the spawn layer can
/// actually execute are listed (.exe directly; .cmd/.bat via cmd.exe), and
/// the bare name comes LAST — npm-style installs stage a POSIX-shell shim
/// beside `<name>.cmd` in the same directory, and the shell shim is not a
/// valid CreateProcess image. A name that already carries an extension is
/// trusted as-given first.
std::vector<std::string> spawnSuffixes(const std::string& name) {
    if constexpr (!IsWindows) {
        return {""};
    }
    if (!fs::path(name).extension().empty()) {
        return {"", ".exe", ".cmd", ".bat"};
    }
    return {".exe", ".cmd", ".bat", ""};
}

} // namespace

std::string findSpawnableOnPath(const std::string& name) {
    if (name.empty()) return {};
    const auto exts = spawnSuffixes(name);
    std::error_code ec;

    // Absolute / relative path with directory: skip the PATH walk, but
    // still probe the spawnable suffixes so "C:\\tools\\npm" finds
    // "C:\\tools\\npm.cmd".
    fs::path nameAsPath(name);
    if (nameAsPath.has_parent_path()) {
        for (const auto& ext : exts) {
            fs::path candidate(name + ext);
            if (fs::is_regular_file(candidate, ec)) {
                return fs::absolute(candidate, ec).string();
            }
        }
        return {};
    }

    for (const auto& dir : pathDirs()) {
        for (const auto& ext : exts) {
            fs::path candidate = dir / (name + ext);
            if (fs::is_regular_file(candidate, ec)) {
                // PATH entries may be relative; the spawn layer needs an
                // absolute path (it may pass the result to cmd.exe, whose
                // working directory is not ours to control).
                return fs::absolute(candidate, ec).string();
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

// ---------------------------------------------------------------------------
// LLVM toolchain resolution (BYO-priority chain)
// ---------------------------------------------------------------------------

namespace {

/// The LLVM major this build is pinned to (seeded from topo-llvm/.llvm-version
/// via the TOPO_LLVM_MAJOR compile-def). Empty when undefined — the chain then
/// resolves leniently without a major gate.
std::string pinnedMajor() {
#ifdef TOPO_LLVM_MAJOR
    return std::string(TOPO_LLVM_MAJOR);
#else
    return {};
#endif
}

std::string majorOf(const std::string& ver) {
    size_t dot = ver.find('.');
    return dot == std::string::npos ? ver : ver.substr(0, dot);
}

/// Extract the "X.Y.Z" core from `clang --version` output.
std::string parseClangVersion(const std::string& out) {
    size_t pos = out.find("version");
    if (pos == std::string::npos) return {};
    pos += 7;
    while (pos < out.size() && !std::isdigit(static_cast<unsigned char>(out[pos]))) pos++;
    std::string ver;
    while (pos < out.size() &&
           (std::isdigit(static_cast<unsigned char>(out[pos])) || out[pos] == '.')) {
        ver += out[pos++];
    }
    return ver;
}

/// Run `<binDir>/clang --version` and return the parsed core version, or "".
std::string probeClangVersion(const std::string& binDir) {
    fs::path clang = fs::path(binDir) / ("clang" + std::string(ExeSuffix));
    std::error_code ec;
    if (!fs::exists(clang, ec)) return {};
    auto r = runProcessCaptureWithTimeout(clang.string(), {"--version"}, 10000, false);
    if (r.exitCode != 0) return {};
    return parseClangVersion(r.stdoutOutput);
}

/// `root/lib/clang/<major>` if present; otherwise the first `root/lib/clang/*`
/// directory; otherwise "". Derived at runtime so no resource dir is baked.
std::string deriveResourceDir(const std::string& root, const std::string& major) {
    std::error_code ec;
    if (!major.empty()) {
        fs::path rd = fs::path(root) / "lib" / "clang" / major;
        if (fs::is_directory(rd, ec)) return rd.string();
    }
    fs::path base = fs::path(root) / "lib" / "clang";
    if (fs::is_directory(base, ec)) {
        for (const auto& e : fs::directory_iterator(base, ec)) {
            if (e.is_directory(ec)) return e.path().string();
        }
    }
    return {};
}

/// Validate a candidate LLVM root (it must hold `bin/clang`) and populate the
/// derived fields. When `probe` is set, `clang --version` fills `.version`.
std::optional<LLVMToolchain> tryRoot(const std::string& root,
                                     LLVMToolchain::Source source, bool probe) {
    if (root.empty()) return std::nullopt;
    std::error_code ec;
    fs::path binDir = fs::path(root) / "bin";
    fs::path clang = binDir / ("clang" + std::string(ExeSuffix));
    if (!fs::exists(clang, ec)) return std::nullopt;
    LLVMToolchain tc;
    tc.root = root;
    tc.binDir = binDir.string();
    tc.libDir = (fs::path(root) / "lib").string();
    tc.source = source;
    if (probe) tc.version = probeClangVersion(tc.binDir);
    std::string major = !tc.version.empty() ? majorOf(tc.version) : pinnedMajor();
    tc.resourceDir = deriveResourceDir(root, major);
    return tc;
}

/// The explicit CLI / Topo.toml override root (highest priority). Set via
/// setLLVMToolchainOverride() before the first resolution.
std::string& overrideRoot() {
    static std::string s;
    return s;
}

/// TOPO_LLVM_DIR (root) — also accepts a `/bin`-suffixed value and the legacy
/// TOPO_LLVM_BINDIR env var, treating either as a bindir whose parent is root.
std::optional<std::string> envToolchainRoot() {
    for (const char* name : {"TOPO_LLVM_DIR", "TOPO_LLVM_BINDIR"}) {
        if (const char* v = std::getenv(name); v && *v) {
            fs::path p(v);
            if (p.filename() == "bin") return p.parent_path().string();
            return p.string();
        }
    }
    return std::nullopt;
}

/// `$TOPO_HOME/toolchains/llvm/<major|default>` or the `~/.topo` equivalent —
/// where the (not-yet-implemented) toolchain downloader installs LLVM. Mirrors
/// BackendCache's $TOPO_HOME → ~/.topo root convention.
std::optional<std::string> topoCacheToolchainRoot(const std::string& major) {
    fs::path base;
    if (const char* th = std::getenv("TOPO_HOME"); th && *th) base = th;
    else base = homeDirectory() / ".topo";
    if (base.empty()) return std::nullopt;
    std::error_code ec;
    if (!major.empty()) {
        fs::path m = base / "toolchains" / "llvm" / major;
        if (fs::exists(m / "bin", ec)) return m.string();
    }
    fs::path d = base / "toolchains" / "llvm" / "default";
    if (fs::exists(d / "bin", ec)) return d.string();
    return std::nullopt;
}

/// Stable package-manager install locations to probe, most-specific first.
std::vector<std::string> wellKnownPrefixes(const std::string& major) {
    std::vector<std::string> out;
    if constexpr (IsMacOS) {
        if (!major.empty()) {
            out.push_back("/opt/homebrew/opt/llvm@" + major);
            out.push_back("/usr/local/opt/llvm@" + major);
        }
        out.push_back("/opt/homebrew/opt/llvm");
        out.push_back("/usr/local/opt/llvm");
    } else if constexpr (IsLinux) {
        if (!major.empty()) out.push_back("/usr/lib/llvm-" + major);
    } else { // Windows
        if (const char* up = std::getenv("USERPROFILE"); up && *up) {
            out.push_back(std::string(up) + "\\scoop\\apps\\llvm\\current");
        }
        out.push_back("C:\\Program Files\\LLVM");
    }
    return out;
}

/// Diagnostic captured during resolution for the actionable error message.
struct ResolveDiag {
    std::string wrongMajorVersion;  // a found-but-wrong-major LLVM, if any
};
ResolveDiag& diag() {
    static ResolveDiag d;
    return d;
}

LLVMToolchain computeToolchain() {
    const std::string major = pinnedMajor();
    auto majorOk = [&](const LLVMToolchain& tc) -> bool {
        if (major.empty()) return true;        // no pin → accept
        if (tc.version.empty()) return true;   // unprobeable → can't disprove
        return majorOf(tc.version) == major;
    };

    // 1. Explicit CLI / Topo.toml override — refuse to fall through on a
    //    wrong-major: an explicit choice must win or fail, never be ignored.
    if (!overrideRoot().empty()) {
        if (auto tc = tryRoot(overrideRoot(), LLVMToolchain::Source::CliOverride, true)) {
            if (majorOk(*tc)) return *tc;
            diag().wrongMajorVersion = tc->version;
        }
        return LLVMToolchain{};
    }

    // 2. TOPO_LLVM_DIR env (explicit) — same hard-fail semantics.
    if (auto envRoot = envToolchainRoot()) {
        if (auto tc = tryRoot(*envRoot, LLVMToolchain::Source::EnvVar, true)) {
            if (majorOk(*tc)) return *tc;
            diag().wrongMajorVersion = tc->version;
        }
        return LLVMToolchain{};
    }

    // 3. Compile-time default IF it still exists — the LLVM this binary was
    //    built against. Trusted without a probe so dev/build-tree runs are
    //    byte-for-byte unchanged and link against an ABI-matched libLLVM.
    //    Relocatable packaging builds set this def empty, so a shipped binary
    //    skips the tier and falls through to BYO discovery below.
#ifdef TOPO_LLVM_BINDIR
    {
        std::string bindir = TOPO_LLVM_BINDIR;
        if (!bindir.empty()) {
            fs::path root = fs::path(bindir).filename() == "bin"
                                ? fs::path(bindir).parent_path()
                                : fs::path(bindir);
            if (auto tc = tryRoot(root.string(),
                                  LLVMToolchain::Source::CompileTimeDefault, false))
                return *tc;
        }
    }
#endif

    // 4. ~/.topo managed toolchain (the downloader's install location).
    if (auto cacheRoot = topoCacheToolchainRoot(major)) {
        if (auto tc = tryRoot(*cacheRoot, LLVMToolchain::Source::TopoCache, true))
            if (majorOk(*tc)) return *tc;
    }

    // 5. PATH: clang-<major> then clang; root = <bin>/.. of the hit.
    {
        std::vector<std::string> names;
        if (!major.empty()) names.push_back("clang-" + major);
        names.push_back("clang");
        for (const auto& n : names) {
            std::string hit = findOnPath(n);
            if (hit.empty()) continue;
            fs::path root = fs::path(hit).parent_path().parent_path();
            if (auto tc = tryRoot(root.string(), LLVMToolchain::Source::PathDiscovery, true))
                if (majorOk(*tc)) return *tc;
        }
    }

    // 6. Well-known package-manager prefixes.
    for (const auto& p : wellKnownPrefixes(major)) {
        if (auto tc = tryRoot(p, LLVMToolchain::Source::WellKnownPrefix, true))
            if (majorOk(*tc)) return *tc;
    }

    return LLVMToolchain{};  // unresolved — surfaced lazily on first use
}

void reportUnresolvedOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        const std::string maj = pinnedMajor();
        std::cerr << "error: no LLVM " << (maj.empty() ? "" : maj + " ")
                  << "toolchain found.\n";
        if (!diag().wrongMajorVersion.empty()) {
            std::cerr << "  (found LLVM " << diag().wrongMajorVersion
                      << " at the requested location, but LLVM " << maj
                      << " is required)\n";
        }
        std::cerr << "  Install it (macOS: brew install llvm@" << maj
                  << "; Linux: apt install llvm-" << maj << " clang-" << maj
                  << "; Windows: scoop install llvm@" << maj << "),\n"
                  << "  set TOPO_LLVM_DIR=<llvm-root>, or run the Topo toolchain"
                  << " downloader.\n";
    });
}

} // namespace

const LLVMToolchain& resolveLLVMToolchain() {
    static LLVMToolchain tc = computeToolchain();
    return tc;
}

void setLLVMToolchainOverride(const std::string& root) {
    overrideRoot() = root;
}

std::string llvmToolPath(const std::string& toolName) {
    const LLVMToolchain& tc = resolveLLVMToolchain();
    if (tc.valid()) {
        fs::path p = fs::path(tc.binDir) / toolName;
        std::error_code ec;
        if (!fs::exists(p, ec) && fs::path(toolName).extension().empty()) {
            p = fs::path(tc.binDir) / (toolName + std::string(ExeSuffix));
        }
        if (fs::exists(p, ec)) return p.string();
        return toolName;  // not in the resolved bindir — let the OS try PATH.
    }
    reportUnresolvedOnce();
    // A pinned major with nothing satisfying it: refuse to silently use an
    // unknown LLVM. Empty path → the spawn fails loudly (error already shown).
    if (!pinnedMajor().empty()) return {};
    return toolName;  // no pin → lenient PATH fallback.
}

std::string llvmClangxx() {
    return llvmToolPath("clang++");
}

std::string llvmResourceDir() {
    return resolveLLVMToolchain().resourceDir;
}

void ensureLLVMLoaderPathForChildren() {
    const LLVMToolchain& tc = resolveLLVMToolchain();
    if (!tc.valid() || tc.libDir.empty()) return;

    // macOS: DYLD_FALLBACK_LIBRARY_PATH is consulted only when a dependency's
    // recorded path fails to resolve, so it redirects a relocated binary's
    // libLLVM/libclang without ever shadowing a correctly-loading libc++.
    // Linux: LD_LIBRARY_PATH. Windows: PATH (the DLL search path).
    const char* var =
        IsWindows ? "PATH" : (IsMacOS ? "DYLD_FALLBACK_LIBRARY_PATH" : "LD_LIBRARY_PATH");
    std::string cur;
    if (const char* v = std::getenv(var); v && *v) cur = v;

    const std::string sep(PathSeparator);
    for (size_t start = 0; start <= cur.size();) {
        size_t end = cur.find(sep, start);
        std::string tok =
            (end == std::string::npos) ? cur.substr(start) : cur.substr(start, end - start);
        if (tok == tc.libDir) return;  // already present — idempotent.
        if (end == std::string::npos) break;
        start = end + sep.size();
    }

    std::string updated = tc.libDir;
    if (!cur.empty()) {
        updated += sep + cur;
    } else if (IsMacOS) {
        // Preserve the implicit default DYLD_FALLBACK_LIBRARY_PATH had when unset.
        updated += sep + std::string("/usr/local/lib") + sep + std::string("/usr/lib");
    }
#ifdef _WIN32
    _putenv_s(var, updated.c_str());
#else
    setenv(var, updated.c_str(), 1);
#endif
}

} // namespace topo::platform
