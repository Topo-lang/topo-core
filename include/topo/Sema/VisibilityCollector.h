#ifndef TOPO_IR_VISIBILITYCOLLECTOR_H
#define TOPO_IR_VISIBILITYCOLLECTOR_H

#include "topo/AST/ASTNode.h"
#include <optional>
#include <string>
#include <vector>

namespace topo {

/// Per-parameter const and ownership information for IR attribute propagation.
struct ParamConstInfo {
    bool isConst = false;                          // parameter type is const
    TypeNode::Modifier modifier = TypeNode::None;  // Ref or Ptr
    OwnershipKind ownership = OwnershipKind::None; // ownership qualifier
};

struct VisibilityEntry {
    std::string qualifiedName; // e.g. "engine::core::math::detail::clamp"
    Visibility visibility;

    // const propagation (Issue 004)
    bool isConst = false;                    // const member function
    std::vector<ParamConstInfo> paramConsts; // per-parameter const info

    // Function binding target (e.g. "std::sort")
    std::optional<std::string> bindingTarget;

    // Scheduling priority
    PriorityLevel priority = PriorityLevel::Normal;
};

class VisibilityCollector {
public:
    std::vector<VisibilityEntry> collect(const TopoFile& root);

private:
    void visitNamespace(const NamespaceDecl& ns, const std::string& parentPath);
    void visitSection(const VisibilitySection& section, const std::string& nsPath, bool forceInternal = false);
    void visitMember(const ASTNode& member, const std::string& nsPath, Visibility vis, bool forceInternal);

    std::vector<VisibilityEntry> entries_;
};

} // namespace topo

#endif // TOPO_IR_VISIBILITYCOLLECTOR_H
