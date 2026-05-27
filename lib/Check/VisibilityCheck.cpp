#include "topo/Check/VisibilityCheck.h"

#include <unordered_map>

namespace topo::check {

void checkVisibilityConsistency(const SymbolTable& symbols,
                                const std::vector<VisibilityEntry>& visEntries,
                                const std::vector<CallEdge>& callEdges,
                                CheckResult& result) {
    // Build visibility map: qualifiedName -> visibility
    std::unordered_map<std::string, Visibility> visMap;
    for (const auto& entry : visEntries) {
        visMap[entry.qualifiedName] = entry.visibility;
    }

    // Build namespace membership: qualifiedName -> namespace prefix
    // e.g. "engine::core::init" -> "engine::core"
    auto getNamespace = [](const std::string& qualifiedName) -> std::string {
        auto pos = qualifiedName.rfind("::");
        if (pos == std::string::npos) return "";
        return qualifiedName.substr(0, pos);
    };

    // Check each call edge: if callee is private/internal, caller must be in scope
    for (const auto& edge : callEdges) {
        auto visIt = visMap.find(edge.callee);
        if (visIt == visMap.end()) continue;

        Visibility vis = visIt->second;

        if (vis == Visibility::Private) {
            // Private: caller must be in the same namespace
            std::string calleeNs = getNamespace(edge.callee);
            std::string callerNs = getNamespace(edge.caller);

            if (callerNs != calleeNs) {
                CheckDiagnostic diag;
                diag.severity = Severity::Error;
                diag.check = "visibility";
                diag.message =
                    "private function '" + edge.callee + "' called from outside its namespace by '" + edge.caller + "'";
                diag.file = edge.file;
                diag.line = edge.line;
                result.diagnostics.push_back(std::move(diag));
                result.errorCount++;
            }
        } else if (vis == Visibility::Internal) {
            // Internal: caller must be within the module (same .topo file scope).
            // For cross-module calls, the caller should not reference internal symbols.
            // We approximate by checking if the caller is known in the symbol table.
            auto callerFn = symbols.findFunction(edge.caller);
            if (!callerFn) {
                // Caller is external (not declared in .topo) — violation
                CheckDiagnostic diag;
                diag.severity = Severity::Error;
                diag.check = "visibility";
                diag.message =
                    "internal function '" + edge.callee + "' called from external function '" + edge.caller + "'";
                diag.file = edge.file;
                diag.line = edge.line;
                result.diagnostics.push_back(std::move(diag));
                result.errorCount++;
            }
        }
    }
}

} // namespace topo::check
