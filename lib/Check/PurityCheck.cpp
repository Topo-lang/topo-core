#include "topo/Check/PurityCheck.h"

#include <string>
#include <unordered_set>

namespace topo::check {

namespace {

// Last `::`-delimited segment, e.g. "app::audit" -> "audit".
// The operation form (`stage<N> f();`) stores bare callee names in
// calledFunctions, while the pipeline/flow form stores namespace-qualified
// node names (`qualifyNode` -> "app::audit"). Host symbol-access extractors
// vary too: C++/Rust qualify the caller while Python emits a bare scope name.
// Matching on the simple segment as a fallback keeps the qualified-vs-bare
// asymmetry between these two channels from silently dropping violations,
// the same tolerance the call-edge-consuming sibling checks already have.
std::string simpleSegment(const std::string& name) {
    auto pos = name.rfind("::");
    return pos == std::string::npos ? name : name.substr(pos + 2);
}

} // namespace

void checkPurity(const SymbolTable& symbols,
                 const std::vector<SymbolAccess>& accesses,
                 CheckResult& result) {
    // Identify functions that appear in parallel stages.
    // A logic block with multiple stages where stage > 0 implies parallel operations.
    std::unordered_set<std::string> parallelFunctions;
    std::unordered_set<std::string> parallelSimpleNames;

    for (const auto& [blockName, block] : symbols.logicBlocks()) {
        // Check if this block has parallel stages (stages with same number > 1 occurrence)
        std::unordered_map<int, int> stageCounts;
        for (int stage : block.stages) {
            stageCounts[stage]++;
        }

        for (size_t i = 0; i < block.calledFunctions.size(); ++i) {
            int stage = (i < block.stages.size()) ? block.stages[i] : 0;
            // A stage with multiple operations is a parallel stage
            if (stageCounts[stage] > 1) {
                parallelFunctions.insert(block.calledFunctions[i]);
                parallelSimpleNames.insert(simpleSegment(block.calledFunctions[i]));
            }
        }
    }

    if (parallelFunctions.empty()) return;

    // Analysis-boundary disclosure. PurityCheck verifies DIRECT global
    // reads/writes by parallel-stage functions only: it does not do
    // transitive call-graph taint propagation (a global write performed
    // through a called helper is invisible — see
    // PurityCheckTest.ParallelCallImpureHelper_DirectOnly) and does not
    // verify that same-stage operations actually commute. Emitted as a Note
    // (never Error/Warning, so it cannot change pass/fail) so a passing
    // purity check is not read as a silent proof of full parallel safety;
    // this shallow-but-loud trade-off is deliberate.
    {
        CheckDiagnostic boundary;
        boundary.severity = Severity::Note;
        boundary.check = "purity";
        boundary.message =
            "parallel-stage purity verified at direct-global-access depth "
            "only — transitive purity (state mutated via called helper "
            "functions) and operation commutativity are NOT verified; a "
            "passing check is not a proof of full parallel safety";
        result.diagnostics.push_back(std::move(boundary));
    }

    // Check: parallel functions should not access global/shared mutable state.
    // Prefer an exact (qualified-to-qualified) match; fall back to the simple
    // segment so a qualified flow callee still matches a bare host caller.
    for (const auto& access : accesses) {
        if (parallelFunctions.count(access.function) == 0 &&
            parallelSimpleNames.count(simpleSegment(access.function)) == 0)
            continue;

        if (access.isWrite) {
            CheckDiagnostic diag;
            diag.severity = Severity::Error;
            diag.check = "purity";
            diag.message = "function '" + access.function + "' in parallel stage writes to global symbol '" +
                           access.symbol +
                           "' — parallel stages must be pure "
                           "(data dependencies only through parameters)";
            diag.file = access.file;
            diag.line = access.line;
            result.diagnostics.push_back(std::move(diag));
            result.errorCount++;
        } else {
            // Read of global state in parallel context is a warning
            CheckDiagnostic diag;
            diag.severity = Severity::Warning;
            diag.check = "purity";
            diag.message = "function '" + access.function + "' in parallel stage reads global symbol '" +
                           access.symbol + "' — consider passing as parameter";
            diag.file = access.file;
            diag.line = access.line;
            result.diagnostics.push_back(std::move(diag));
            result.warningCount++;
        }
    }
}

} // namespace topo::check
