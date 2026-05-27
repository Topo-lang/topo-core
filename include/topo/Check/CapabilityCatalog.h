#ifndef TOPO_CHECK_CAPABILITYCATALOG_H
#define TOPO_CHECK_CAPABILITYCATALOG_H

#include <optional>
#include <string>

namespace topo::check {

enum class CapabilityKind { File, Network, Process, DynamicLoad, Memory };

inline const char* capabilityKindName(CapabilityKind k) {
    switch (k) {
    case CapabilityKind::File: return "file";
    case CapabilityKind::Network: return "network";
    case CapabilityKind::Process: return "process";
    case CapabilityKind::DynamicLoad: return "dynamic-load";
    case CapabilityKind::Memory: return "memory";
    }
    return "unknown";
}

/// Unsafe level — detected by checker, not declared by humans.
/// Each function's level is based on its own direct behavior only (no transitivity).
enum class UnsafeLevel { Safe = 0, System = 1, Dep = 2, Input = 3, Escape = 4 };

inline const char* unsafeLevelName(UnsafeLevel l) {
    switch (l) {
    case UnsafeLevel::Safe: return "safe";
    case UnsafeLevel::System: return "system";
    case UnsafeLevel::Dep: return "dep";
    case UnsafeLevel::Input: return "input";
    case UnsafeLevel::Escape: return "escape";
    }
    return "unknown";
}

inline int unsafeLevelValue(UnsafeLevel l) { return static_cast<int>(l); }

/// Classify an import path by capability.
/// Handles C/C++ headers, Java packages, Python modules, and Rust crate paths.
/// Returns nullopt for safe imports (vector, string, algorithm, etc.).
std::optional<CapabilityKind> classifyImport(const std::string& path);

/// Classify an API function/method call by capability.
/// Handles C/C++, Java, Python, and Rust API names.
/// Returns nullopt for safe functions.
std::optional<CapabilityKind> classifyApiCall(const std::string& funcName);

} // namespace topo::check

#endif // TOPO_CHECK_CAPABILITYCATALOG_H
