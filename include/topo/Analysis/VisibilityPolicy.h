#ifndef TOPO_ANALYSIS_VISIBILITYPOLICY_H
#define TOPO_ANALYSIS_VISIBILITYPOLICY_H

#include "topo/AST/ASTNode.h"
#include "topo/Sema/VisibilityCollector.h"

#include <string>
#include <vector>

namespace topo {
namespace analysis {

/// Platform-independent visibility policy result for a single symbol.
/// Describes what actions the backend should take, without referencing
/// any LLVM-specific types.
struct VisibilityAction {
    /// Whether the symbol should have internal (module-local) linkage.
    bool internalLinkage = false;

    /// Whether the symbol should receive an inline hint.
    bool inlineHint = false;

    /// Whether the symbol should be forced to always inline.
    bool alwaysInline = false;

    /// Whether the symbol's debug info should be stripped (release mode).
    bool stripDebug = false;

    /// Whether the symbol should be exported (for shared libraries).
    bool isExport = false;

    /// Whether the symbol should be marked dso_local.
    bool dsoLocal = false;

    /// Whether this is a const function (read-only memory effects).
    bool isConst = false;

    /// Per-parameter const/ownership info (forwarded from VisibilityEntry).
    std::vector<ParamConstInfo> paramConsts;
};

/// Target platform description for visibility policy decisions.
enum class TargetPlatform {
    ELF,  // Linux, most Unix
    COFF, // Windows
    MachO // macOS
};

/// Output type for visibility policy decisions.
enum class PolicyOutputType { Executable, SharedLibrary, StaticLibrary };

/// Compute the visibility action for a single entry.
///
/// Pure decision logic: given a visibility level, output type, platform,
/// and debug mode, returns what the backend should do. No LLVM dependency.
VisibilityAction computeVisibilityAction(const VisibilityEntry& entry,
                                         PolicyOutputType outputType,
                                         TargetPlatform platform,
                                         bool debugInternal = false);

/// Compute visibility actions for all entries.
/// Returns a parallel vector: actions[i] corresponds to entries[i].
std::vector<VisibilityAction> computeVisibilityActions(const std::vector<VisibilityEntry>& entries,
                                                       PolicyOutputType outputType,
                                                       TargetPlatform platform,
                                                       bool debugInternal = false);

} // namespace analysis
} // namespace topo

#endif // TOPO_ANALYSIS_VISIBILITYPOLICY_H
