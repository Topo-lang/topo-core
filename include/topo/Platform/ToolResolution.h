#ifndef TOPO_PLATFORM_TOOLRESOLUTION_H
#define TOPO_PLATFORM_TOOLRESOLUTION_H

#include <string>
#include <vector>

namespace topo::platform {

/// A resolved LLVM toolchain: a single root from which every tool, the
/// clang resource dir, and the lib dir derive. Resolved once per process by
/// resolveLLVMToolchain() using a bring-your-own (BYO) priority chain so a
/// pre-compiled binary locates LLVM wherever the user installed it, rather
/// than the build host's absolute path.
struct LLVMToolchain {
    enum class Source {
        None,               // nothing resolved
        CliOverride,        // setLLVMToolchainOverride()
        EnvVar,             // TOPO_LLVM_DIR (or legacy TOPO_LLVM_BINDIR env)
        TopoCache,          // ~/.topo/toolchains/llvm/<ver>
        PathDiscovery,      // clang-<major> / clang on PATH
        WellKnownPrefix,    // brew / apt / scoop standard locations
        CompileTimeDefault  // baked TOPO_LLVM_BINDIR (dev/build-tree only)
    };
    std::string root;        // LLVM install prefix (root/bin holds clang)
    std::string binDir;      // root + "/bin"
    std::string libDir;      // root + "/lib"
    std::string resourceDir; // root + "/lib/clang/<major>" (derived; "" if absent)
    std::string version;     // "X.Y.Z" when probed, else ""
    Source source = Source::None;

    bool valid() const { return !root.empty(); }
};

/// Resolve the LLVM toolchain once (cached process-wide) via the BYO-priority
/// chain. Subsequent calls return the cached result without re-deriving.
const LLVMToolchain& resolveLLVMToolchain();

/// Pin the toolchain root (the explicit CLI flag / Topo.toml tier — highest
/// priority). Must be called before the first resolveLLVMToolchain(); a later
/// call has no effect. An empty root clears any pin.
void setLLVMToolchainOverride(const std::string& root);

/// Full path to an LLVM tool from the resolved toolchain, honouring the
/// Windows ``.exe`` suffix. Falls back to the bare tool name (so the OS
/// resolves it on PATH at exec) when no toolchain is resolved.
std::string llvmToolPath(const std::string& toolName);

/// The resolved clang++ path. Convenience for ``llvmToolPath("clang++")``.
std::string llvmClangxx();

/// The resolved clang resource dir (``root/lib/clang/<major>``), or an empty
/// string when no toolchain resolved or the directory is absent. Callers pass
/// it to libclang/clang via ``-resource-dir`` so builtin headers are found.
std::string llvmResourceDir();

/// Prepend the resolved toolchain's lib dir to this process's dynamic-loader
/// search path so child processes spawned afterwards (e.g. topo-build-llvm-cpp,
/// topo-extract-cpp) locate libLLVM/libclang from the resolved — possibly
/// relocated — prefix. macOS uses DYLD_FALLBACK_LIBRARY_PATH (consulted only
/// when an absolute install path fails to resolve, so it never shadows a
/// correctly-loading libc++); Linux uses LD_LIBRARY_PATH; Windows uses PATH.
/// Idempotent and a no-op when the toolchain is unresolved.
void ensureLLVMLoaderPathForChildren();

/// Resolve an LLVM tool (e.g. "clang++", "llvm-ar") to a full path.
/// Thin shim over llvmToolPath() — kept for existing call sites.
std::string resolveLLVMTool(const std::string& toolName);

/// Locate an executable on PATH (cross-platform ``which``).
///
/// On Windows the search honours ``PATHEXT`` (so a request for
/// ``"python"`` correctly finds ``python.exe``); on POSIX the name is
/// looked up verbatim. Returns an empty string if not found.
std::string findOnPath(const std::string& name);

/// Spawn-oriented executable resolution. Returns the ABSOLUTE path of the
/// on-disk file a spawn of ``name`` should execute, or an empty string when
/// nothing resolvable is found.
///
/// On POSIX this is a plain PATH walk for the bare name (execvp semantics).
/// On Windows it exists because ``CreateProcessW`` appends only ``.exe``
/// when resolving a bare program name, so script launchers staged as
/// ``<name>.cmd`` / ``<name>.bat`` (npm shims, jdtls, the staged
/// ``topo-extract-*`` tools) are never found. Per PATH directory the probe
/// order is ``<name>.exe``, ``<name>.cmd``, ``<name>.bat``, then the bare
/// ``<name>`` — executable images first, and the extensionless name last
/// because npm-style installs stage a POSIX-shell shim *beside* the
/// ``.cmd`` and the shell shim is not a valid CreateProcess image. When
/// ``name`` already carries an extension it is probed as-given first. Other
/// ``PATHEXT`` entries (``.VBS``, ``.JS``, ...) are not probed: the spawn
/// layer can only run a PE image directly or a batch script via
/// ``cmd.exe /c``. Names containing a directory separator skip the PATH
/// walk and are probed (with the same suffix list) relative to the caller's
/// working directory.
std::string findSpawnableOnPath(const std::string& name);

/// Locate a Python 3 interpreter and return the argv prefix to invoke
/// it with. The first element is the executable; subsequent elements
/// are leading flags (e.g. ``{"py.exe", "-3"}`` for the Windows launcher).
///
/// Resolution order:
///   1. ``TOPO_PYTHON`` / ``TOPO_PYTHON_EXE`` env var (explicit override).
///   2. ``python3`` on PATH (POSIX convention).
///   3. ``python`` on PATH (Windows convention; also some POSIX installs).
///   4. ``py -3`` (Windows launcher).
///
/// Returns an empty vector when no interpreter is found; callers should
/// surface that with an actionable diagnostic (set TOPO_PYTHON ...) so
/// the user is not left guessing at a generic "exited with code 1".
std::vector<std::string> findPythonInterpreter();

} // namespace topo::platform

#endif // TOPO_PLATFORM_TOOLRESOLUTION_H
