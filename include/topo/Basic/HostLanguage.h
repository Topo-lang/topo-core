#ifndef TOPO_BASIC_HOSTLANGUAGE_H
#define TOPO_BASIC_HOSTLANGUAGE_H

#include <string_view>

namespace topo {

enum class HostLanguage { Cpp, Rust, Java, Python, TypeScript, Mixed };

/// Parse a host language name: "rust" → Rust, "java" → Java, "python" → Python,
/// "typescript" → TypeScript, "mixed" → Mixed, anything else → Cpp.
inline HostLanguage parseHostLanguage(std::string_view s) {
    if (s == "rust") return HostLanguage::Rust;
    if (s == "java") return HostLanguage::Java;
    if (s == "python") return HostLanguage::Python;
    if (s == "typescript") return HostLanguage::TypeScript;
    if (s == "mixed") return HostLanguage::Mixed;
    return HostLanguage::Cpp;
}

} // namespace topo

#endif // TOPO_BASIC_HOSTLANGUAGE_H
