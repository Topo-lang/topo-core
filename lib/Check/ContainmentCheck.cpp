#include "topo/Check/ContainmentCheck.h"
#include "topo/Check/CapabilityCatalog.h"
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace topo::check {

void checkContainment(const SymbolTable& symbols,
                      const std::vector<HostImport>& imports,
                      const std::vector<DetectedCallSite>& callSites,
                      const ContainmentConfig& config,
                      CheckResult& result,
                      const std::string& separator) {
    if (!config.isEnabled()) return;

    Severity sev = (config.mode == FeatureMode::Force) ? Severity::Error : Severity::Warning;

    // Build separate sets for qualified and simple external names to avoid collision
    std::unordered_set<std::string> externalQualifiedNames;
    std::unordered_set<std::string> externalSimpleNames;
    // Build set of files that contain at least one external function
    std::unordered_set<std::string> filesWithExternal;
    for (const auto& [name, fn] : symbols.functions()) {
        if (fn.isExternal) {
            externalQualifiedNames.insert(fn.qualifiedName);
            externalSimpleNames.insert(fn.simpleName);
            if (!fn.location.file.empty()) {
                filesWithExternal.insert(fn.location.file);
            }
        }
    }

    // Coincidence detector — shared by caller and callee simple-name fallback paths.
    // Returns true if any non-external function in the SymbolTable shares the given
    // simple name. When this is true, a simple-name match against externalSimpleNames
    // is ambiguous and must not grant external boundary delegation.
    auto hasNonExternalWithSimpleName = [&](const std::string& simpleName) -> bool {
        for (const auto& [qn, fn] : symbols.functions()) {
            if (fn.simpleName == simpleName && !fn.isExternal) return true;
        }
        return false;
    };

    // Check if a caller qualified name matches any external function.
    // Qualified match first, then simple-name fallback for all callers.
    // Guard: if the simple name is a known non-external function in the
    // SymbolTable, the match is coincidental — don't treat it as external.
    // The simple-name fallback is needed because host-language qualified names
    // may include intermediate scopes not in .topo (e.g. Java class names:
    // call site "app::App::read_file" vs .topo "app::read_file").
    auto isExternalCaller = [&](const std::string& callerQN) -> bool {
        if (externalQualifiedNames.count(callerQN)) return true;
        // Extract simple name (last component after the language separator).
        std::string simpleName = callerQN;
        if (!separator.empty()) {
            auto lastSep = callerQN.rfind(separator);
            if (lastSep != std::string::npos) {
                simpleName = callerQN.substr(lastSep + separator.size());
            }
        }
        if (!externalSimpleNames.count(simpleName)) return false;
        return !hasNonExternalWithSimpleName(simpleName);
    };

    // Check if a call site's callee pattern (a simple name from L1 extraction)
    // refers to a declared external function. Mirrors the caller-side coincidence
    // protection: if any non-external function shares the simple name, the L1
    // pattern is ambiguous and the call must be checked, not skipped as boundary
    // delegation. Without this, a non-external method named identically to an
    // external (e.g. user `Reader::read` vs .topo `io::read`) silently bypasses
    // containment.
    auto isExternalCallee = [&](const std::string& calleePattern) -> bool {
        if (!externalSimpleNames.count(calleePattern)) return false;
        return !hasNonExternalWithSimpleName(calleePattern);
    };

    // Supplement filesWithExternal using call site data (F1.7 fix).
    // fn.location.file stores .topo paths, but Pass 1 checks host file paths.
    // Use call sites to discover which host files contain external functions.
    for (const auto& site : callSites) {
        if (isExternalCaller(site.callerQualifiedName)) {
            filesWithExternal.insert(site.file);
        }
    }

    // --- Pass 1: Import check ---
    // Group imports by file, check if file has restricted imports but no external functions
    std::unordered_map<std::string, std::vector<const HostImport*>> importsByFile;
    for (const auto& imp : imports) {
        importsByFile[imp.file].push_back(&imp);
    }

    for (const auto& [file, fileImports] : importsByFile) {
        // Check if any import is restricted (by CapabilityCatalog or UnsafeLevel)
        bool hasRestricted = false;
        for (const auto* imp : fileImports) {
            auto cap = classifyImport(imp->normalizedPath);
            if (cap || imp->unsafeLevel != UnsafeLevel::Safe) {
                hasRestricted = true;
                break;
            }
        }

        if (!hasRestricted) continue;

        // Check if this file has any external function declared
        if (filesWithExternal.count(file)) continue;

        // Violation: file has restricted imports but no external function
        for (const auto* imp : fileImports) {
            auto cap = classifyImport(imp->normalizedPath);
            if (!cap && imp->unsafeLevel == UnsafeLevel::Safe) continue;

            // Extract just filename for readable diagnostic
            std::string shortFile = file;
            auto slashPos = file.rfind('/');
            if (slashPos != std::string::npos) shortFile = file.substr(slashPos + 1);

            std::string kindStr = cap ? capabilityKindName(*cap)
                                      : unsafeLevelName(imp->unsafeLevel);

            CheckDiagnostic diag;
            diag.severity = sev;
            diag.check = "containment";
            diag.message = "'" + shortFile + "' includes <" + imp->normalizedPath +
                           "> (" + kindStr +
                           ") but has no external function";
            diag.file = file;
            diag.line = imp->line;
            result.addDiagnostic(std::move(diag));
        }
    }

    // --- Pass 2: Call site check (UnsafeLevel-driven) ---
    for (const auto& site : callSites) {
        // Skip safe call sites (defensive check)
        if (site.unsafeLevel == UnsafeLevel::Safe) continue;

        // If caller is external -> skip (external functions are allowed to be unsafe)
        if (isExternalCaller(site.callerQualifiedName)) continue;

        // If callee is a declared external function -> skip (boundary delegation)
        if (isExternalCallee(site.calleePattern)) continue;

        // Non-external caller with unsafe behavior -> violation
        std::string kindStr = site.capability ? capabilityKindName(*site.capability)
                                              : unsafeLevelName(site.unsafeLevel);
        CheckDiagnostic diag;
        diag.severity = sev;
        diag.check = "containment";
        diag.message = "non-external function '" + site.callerQualifiedName +
                       "' uses '" + site.calleePattern + "' (" + kindStr +
                       ", level " + std::to_string(unsafeLevelValue(site.unsafeLevel)) +
                       ") at " + site.file + ":" + std::to_string(site.line);
        diag.file = site.file;
        diag.line = site.line;
        result.addDiagnostic(std::move(diag));
    }
}

} // namespace topo::check
