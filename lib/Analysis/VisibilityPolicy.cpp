#include "topo/Analysis/VisibilityPolicy.h"

namespace topo {
namespace analysis {

VisibilityAction computeVisibilityAction(const VisibilityEntry& entry,
                                         PolicyOutputType outputType,
                                         TargetPlatform platform,
                                         bool debugInternal) {
    VisibilityAction action;
    action.isConst = entry.isConst;
    action.paramConsts = entry.paramConsts;

    bool isShared = (outputType == PolicyOutputType::SharedLibrary);

    switch (entry.visibility) {
    case Visibility::Public:
        if (isShared) {
            action.isExport = true;
        }
        // Executable: skip dso_local — linker resolves locally anyway,
        // and setting it can shift LLVM's inlining heuristics on templates.
        break;

    case Visibility::Protected:
        action.internalLinkage = true;
        action.inlineHint = true;
        break;

    case Visibility::Private:
        action.internalLinkage = true;
        action.inlineHint = true;
        break;

    case Visibility::Internal:
        action.internalLinkage = true;
        action.alwaysInline = true;
        if (!debugInternal) {
            action.stripDebug = true;
        }
        break;

    case Visibility::Ignore:
        // Marker that the policy should not touch this symbol — return the
        // default-initialised action so no linkage / export / inline-hint
        // attributes are stamped on the IR.
        break;
    }

    return action;
}

std::vector<VisibilityAction> computeVisibilityActions(const std::vector<VisibilityEntry>& entries,
                                                       PolicyOutputType outputType,
                                                       TargetPlatform platform,
                                                       bool debugInternal) {
    std::vector<VisibilityAction> actions;
    actions.reserve(entries.size());

    for (const auto& entry : entries) {
        actions.push_back(computeVisibilityAction(entry, outputType, platform, debugInternal));
    }

    return actions;
}

} // namespace analysis
} // namespace topo
