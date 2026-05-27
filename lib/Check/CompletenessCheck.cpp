#include "topo/Check/CompletenessCheck.h"

#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace topo::check {

namespace {

/// Convert a glob pattern (with * and ?) to a regex.
std::regex globToRegex(const std::string& pattern) {
    std::string rx;
    for (char c : pattern) {
        switch (c) {
        case '*': rx += ".*"; break;
        case '?': rx += "."; break;
        case '.': rx += "\\."; break;
        case '(': rx += "\\("; break;
        case ')': rx += "\\)"; break;
        case '[': rx += "\\["; break;
        case ']': rx += "\\]"; break;
        case '{': rx += "\\{"; break;
        case '}': rx += "\\}"; break;
        case '+': rx += "\\+"; break;
        case '^': rx += "\\^"; break;
        case '$': rx += "\\$"; break;
        case '|': rx += "\\|"; break;
        case '\\': rx += "\\\\"; break;
        default: rx += c; break;
        }
    }
    return std::regex(rx);
}

bool matchesAnyPattern(const std::string& name, const std::vector<std::regex>& patterns) {
    for (const auto& rx : patterns) {
        if (std::regex_match(name, rx)) return true;
    }
    return false;
}

/// Extract the simple (unqualified) name from a possibly qualified name.
/// "app::process_order" -> "process_order", "process_order" -> "process_order"
std::string extractSimpleName(const std::string& qualifiedName) {
    auto pos = qualifiedName.rfind("::");
    if (pos != std::string::npos) return qualifiedName.substr(pos + 2);
    return qualifiedName;
}

bool shouldSkipSymbol(const HostSymbol& sym,
                      const CompletenessConfig& config,
                      const std::vector<std::regex>& patterns) {
    if (config.ignoreConstructors && sym.kind == HostSymbolKind::Constructor) return true;
    if (config.ignoreDestructors && sym.kind == HostSymbolKind::Destructor) return true;
    if (config.ignoreMain && sym.simpleName == "main") return true;
    if (matchesAnyPattern(sym.qualifiedName, patterns)) return true;
    return false;
}

} // anonymous namespace

void checkCompleteness(const std::vector<HostSymbol>& hostSymbols,
                       const SymbolTable& symbols,
                       const std::vector<VisibilityEntry>& visEntries,
                       const CompletenessConfig& config,
                       CheckResult& result) {
    // Pre-compile glob patterns
    std::vector<std::regex> patterns;
    patterns.reserve(config.ignorePatterns.size());
    for (const auto& p : config.ignorePatterns) {
        patterns.push_back(globToRegex(p));
    }

    // Build lookup: visEntries by qualified name, with simple-name fallback
    std::unordered_map<std::string, const VisibilityEntry*> visMap;
    std::unordered_map<std::string, const VisibilityEntry*> visSimpleMap;
    for (const auto& ve : visEntries) {
        visMap[ve.qualifiedName] = &ve;
        visSimpleMap[extractSimpleName(ve.qualifiedName)] = &ve;
    }

    // Set of ignored qualified names (from ignore: sections), with simple-name fallback
    std::unordered_set<std::string> ignoredNames;
    std::unordered_set<std::string> ignoredSimpleNames;
    for (const auto& ve : visEntries) {
        if (ve.visibility == Visibility::Ignore) {
            ignoredNames.insert(ve.qualifiedName);
            ignoredSimpleNames.insert(extractSimpleName(ve.qualifiedName));
        }
    }

    // Host symbols by qualified name and simple name (for Pass 2 reverse lookup)
    std::unordered_set<std::string> hostNameSet;
    std::unordered_set<std::string> hostSimpleNameSet;
    for (const auto& hs : hostSymbols) {
        hostNameSet.insert(hs.qualifiedName);
        hostSimpleNameSet.insert(hs.simpleName);
    }

    // ===== Pass 1: Undeclared symbols =====
    for (const auto& hs : hostSymbols) {
        if (shouldSkipSymbol(hs, config, patterns)) continue;
        if (ignoredNames.count(hs.qualifiedName)) continue;
        if (ignoredSimpleNames.count(hs.simpleName)) continue;

        bool declared = false;

        if (hs.kind == HostSymbolKind::Class || hs.kind == HostSymbolKind::Struct ||
            hs.kind == HostSymbolKind::Enum || hs.kind == HostSymbolKind::Interface ||
            hs.kind == HostSymbolKind::TypeAlias) {
            // Type-shape declarations all route through the class-symbol
            // lookup path. .topo authors continue declaring them as
            // `class`; the new kinds let the host extractor stay honest
            // about whether the source uses `class`, `interface`, or
            // `type X = ...` without forcing the .topo declaration to
            // mirror that distinction.
            declared = (symbols.findClassSymbol(hs.qualifiedName) != nullptr);
            // Simple-name fallback for non-C++ languages
            if (!declared) {
                declared = (symbols.findClassBySimpleName(hs.simpleName) != nullptr);
            }
        } else {
            declared = (symbols.findFunction(hs.qualifiedName) != nullptr);
            // Simple-name fallback for non-C++ languages
            if (!declared) {
                declared = (symbols.findFunctionBySimpleName(hs.simpleName) != nullptr);
            }
            // Check class member functions
            if (!declared && !hs.enclosingClass.empty()) {
                const auto* cls = symbols.findClassSymbol(hs.enclosingClass);
                // Simple-name fallback for enclosing class lookup
                if (!cls) {
                    cls = symbols.findClassBySimpleName(extractSimpleName(hs.enclosingClass));
                }
                if (cls) {
                    for (const auto& mf : cls->memberFunctions) {
                        if (mf == hs.qualifiedName || extractSimpleName(mf) == hs.simpleName) {
                            declared = true;
                            break;
                        }
                    }
                    if (!declared) {
                        for (const auto& ctor : cls->constructors) {
                            if (ctor == hs.qualifiedName || extractSimpleName(ctor) == hs.simpleName) {
                                declared = true;
                                break;
                            }
                        }
                    }
                    if (!declared) {
                        if (cls->destructor == hs.qualifiedName ||
                            extractSimpleName(cls->destructor) == hs.simpleName) {
                            declared = true;
                        }
                    }
                }
            }
        }

        // Also check visibility entries (qualified, then simple-name fallback)
        if (!declared) {
            declared = (visMap.count(hs.qualifiedName) > 0);
            if (!declared) {
                declared = (visSimpleMap.count(hs.simpleName) > 0);
            }
        }

        if (!declared) {
            CheckDiagnostic diag;
            diag.severity = Severity::Error;
            diag.check = "completeness";
            diag.message = "symbol '" + hs.qualifiedName + "' exists in host code but is not declared in .topo";
            diag.file = hs.file;
            diag.line = hs.line;
            result.addDiagnostic(std::move(diag));
        }
    }

    // ===== Pass 2: Dangling declarations =====
    for (const auto& [name, fn] : symbols.functions()) {
        if (fn.bindingTarget.has_value()) continue;
        if (ignoredNames.count(name)) continue;
        if (matchesAnyPattern(name, patterns)) continue;

        if (hostNameSet.find(name) == hostNameSet.end() &&
            hostSimpleNameSet.find(fn.simpleName) == hostSimpleNameSet.end()) {
            CheckDiagnostic diag;
            diag.severity = Severity::Warning;
            diag.check = "completeness";
            diag.message = "function '" + name + "' is declared in .topo but not found in host code";
            diag.file = fn.location.file;
            diag.line = fn.location.line;
            result.addDiagnostic(std::move(diag));
        }
    }

    for (const auto& [name, cls] : symbols.classSymbols()) {
        if (ignoredNames.count(name)) continue;
        if (matchesAnyPattern(name, patterns)) continue;

        if (hostNameSet.find(name) == hostNameSet.end() &&
            hostSimpleNameSet.find(cls.simpleName) == hostSimpleNameSet.end()) {
            CheckDiagnostic diag;
            diag.severity = Severity::Warning;
            diag.check = "completeness";
            diag.message = "class '" + name + "' is declared in .topo but not found in host code";
            diag.file = cls.location.file;
            diag.line = cls.location.line;
            result.addDiagnostic(std::move(diag));
        }
    }

    // ===== Pass 3: Visibility mismatch =====
    for (const auto& hs : hostSymbols) {
        if (shouldSkipSymbol(hs, config, patterns)) continue;
        if (ignoredNames.count(hs.qualifiedName)) continue;
        if (ignoredSimpleNames.count(hs.simpleName)) continue;
        if (!hs.hostVisibility.has_value()) continue;

        const VisibilityEntry* vePtr = nullptr;
        auto vit = visMap.find(hs.qualifiedName);
        if (vit != visMap.end()) {
            vePtr = vit->second;
        } else {
            // Simple-name fallback for non-C++ languages
            auto vsit = visSimpleMap.find(hs.simpleName);
            if (vsit != visSimpleMap.end()) {
                vePtr = vsit->second;
            }
        }
        if (!vePtr) continue;

        const auto& ve = *vePtr;
        if (ve.visibility == Visibility::Ignore) continue;

        Visibility topoVis = ve.visibility;
        Visibility hostVis = *hs.hostVisibility;

        // .topo says Public but source is private -> Error
        if (topoVis == Visibility::Public && (hostVis == Visibility::Private || hostVis == Visibility::Protected)) {
            CheckDiagnostic diag;
            diag.severity = Severity::Error;
            diag.check = "completeness";
            diag.message = "symbol '" + hs.qualifiedName + "' is declared public in .topo but " +
                           visibilityName(hostVis) + " in host code";
            diag.file = hs.file;
            diag.line = hs.line;
            result.addDiagnostic(std::move(diag));
        }
        // .topo says Private but source is public -> Warning
        else if ((topoVis == Visibility::Private || topoVis == Visibility::Internal) && hostVis == Visibility::Public) {
            CheckDiagnostic diag;
            diag.severity = Severity::Warning;
            diag.check = "completeness";
            diag.message = "symbol '" + hs.qualifiedName + "' is declared " + visibilityName(topoVis) +
                           " in .topo but public in host code";
            diag.file = hs.file;
            diag.line = hs.line;
            result.addDiagnostic(std::move(diag));
        }
    }
}

} // namespace topo::check
