#ifndef TOPO_PLATFORM_TOOLRESOLUTION_H
#define TOPO_PLATFORM_TOOLRESOLUTION_H

#include <string>
#include <vector>

namespace topo::platform {

/// Resolve an LLVM tool (e.g. "clang++", "llvm-ar") to a full path.
/// Priority: bundled TOPO_LLVM_BINDIR > PATH fallback.
std::string resolveLLVMTool(const std::string& toolName);

/// Locate an executable on PATH (cross-platform ``which``).
///
/// On Windows the search honours ``PATHEXT`` (so a request for
/// ``"python"`` correctly finds ``python.exe``); on POSIX the name is
/// looked up verbatim. Returns an empty string if not found.
std::string findOnPath(const std::string& name);

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
