#ifndef TOPO_TRANSPILE_TOPOSOURCEBUILDER_H
#define TOPO_TRANSPILE_TOPOSOURCEBUILDER_H

#include "topo/AST/ASTNode.h"
#include "topo/Transpile/TranspileModel.h"
#include <string>
#include <vector>

namespace topo::transpile {

// ---------------------------------------------------------------------------
// TopoSourceBuilder — the `.topo`-source frontend for topo-transpile.
//
// This is the `.topo`-source-side counterpart to the existing host-source
// extractors (topo-extract-cpp/rust/java): it consumes a parsed `.topo` AST
// (a `TopoFile`, as produced by the existing Parser) and builds a
// `TranspileModule` that the existing 5 Emitters consume unchanged.
//
//   - A function declared with a matching `fn` logic block (a *composite*
//     function — typically a `flow`) gets a body generated directly from the
//     logic block: operation-mode blocks lower to a declaration-ordered call
//     sequence + return bindings; pipeline-mode blocks lower to a naive DAG
//     orchestration. This is *naive orchestration only* — stage reordering,
//     pipeline cascade specialization, and every other optimization-license
//     construct is deliberately NOT expressed (it belongs to the backends).
//
//   - A function declared with only a signature (a *leaf* function — no `fn`
//     logic block) has no body to generate. The builder marks it with an
//     "unresolved body" sentinel (see `kLeafUnresolvedMarker`). The adapter
//     resolver (`AdapterResolver`) fills these in, or applies the traceable
//     degradation when no adapter is found.
// ---------------------------------------------------------------------------

// Sentinel pushed onto `TranspileFunction::unsupported` by the builder to
// mark a leaf function whose body could not be generated from a logic block.
// The adapter resolver recognizes a leaf by this exact marker, removes it on
// a successful resolve, and replaces it with a precise diagnostic on a failed
// resolve. Carried inside the existing `unsupported` mechanism so no new
// TranspileModel field is introduced.
inline constexpr const char* kLeafUnresolvedMarker = "__topo_leaf_unresolved__";

// Returns true iff `fn` is a leaf function still carrying the builder's
// unresolved marker (i.e. the adapter resolver has not yet resolved it).
bool isUnresolvedLeaf(const TranspileFunction& fn);

struct BuildResult {
    TranspileModule module;
    std::vector<std::string> warnings; // non-fatal notes (e.g. dropped optimization construct)
    std::vector<std::string> errors;   // fatal: builder could not proceed
    bool success = false;
};

class TopoSourceBuilder {
public:
    // Build a TranspileModule from a parsed `.topo` file.
    //
    // `topoFile` must be the root node of a successfully parsed `.topo` file
    // (the Parser's `parseTopoFile()` output). Walks namespaces and
    // visibility sections, collecting every `FnDecl` and `FnLogicBlock`.
    BuildResult build(const TopoFile& topoFile);
};

} // namespace topo::transpile

#endif // TOPO_TRANSPILE_TOPOSOURCEBUILDER_H
