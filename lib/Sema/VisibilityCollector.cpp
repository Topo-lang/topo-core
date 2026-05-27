#include "topo/Sema/VisibilityCollector.h"

namespace topo {

/// Extract per-parameter const info from a FnDecl.
static std::vector<ParamConstInfo> extractParamConsts(const FnDecl& func) {
    std::vector<ParamConstInfo> result;
    result.reserve(func.params.size());
    for (const auto& param : func.params) {
        result.push_back({param.type.isConst, param.type.modifier, param.type.ownership});
    }
    return result;
}

std::vector<VisibilityEntry> VisibilityCollector::collect(const TopoFile& root) {
    entries_.clear();
    for (const auto& decl : root.declarations) {
        if (decl->kind == ASTKind::NamespaceDecl) {
            visitNamespace(static_cast<const NamespaceDecl&>(*decl), "");
        }
    }
    return entries_;
}

void VisibilityCollector::visitNamespace(const NamespaceDecl& ns, const std::string& parentPath) {
    std::string nsPath = parentPath;
    for (size_t i = 0; i < ns.path.size(); ++i) {
        if (!nsPath.empty()) nsPath += "::";
        nsPath += ns.path[i];
    }

    for (const auto& section : ns.sections) {
        if (section->kind == ASTKind::VisibilitySection) {
            const auto& vis = static_cast<const VisibilitySection&>(*section);
            visitSection(vis, nsPath, ns.isInternal);
        }
    }
}

void VisibilityCollector::visitSection(const VisibilitySection& section,
                                       const std::string& nsPath,
                                       bool forceInternal) {
    Visibility vis = forceInternal ? Visibility::Internal : section.visibility;

    for (const auto& member : section.members) {
        visitMember(*member, nsPath, vis, forceInternal);
    }
}

void VisibilityCollector::visitMember(const ASTNode& member,
                                      const std::string& nsPath,
                                      Visibility vis,
                                      bool forceInternal) {
    if (member.kind == ASTKind::FnDecl) {
        const auto& func = static_cast<const FnDecl&>(member);

        // Skip constructors, destructors, operators, comptime fns
        if (func.isConstructor || func.isDestructor || func.isOperator() || func.isComptime()) {
            return;
        }

        // Convert dotted name to :: separator (spec: . is :: sugar)
        std::string funcName = func.name;
        for (auto& ch : funcName) {
            if (ch == '.') ch = ':';
        }
        std::string normalized;
        for (size_t i = 0; i < funcName.size(); ++i) {
            if (funcName[i] == ':' && i + 1 < funcName.size() && funcName[i + 1] == ':') {
                normalized += "::";
                ++i;
            } else if (funcName[i] == ':') {
                normalized += "::";
            } else {
                normalized += funcName[i];
            }
        }

        std::string qualified = nsPath + "::" + normalized;
        VisibilityEntry entry;
        entry.qualifiedName = qualified;
        entry.visibility = vis;
        entry.isConst = func.isConst;
        entry.paramConsts = extractParamConsts(func);
        entry.bindingTarget = func.bindingTarget;
        entry.priority = func.priority;
        entries_.push_back(std::move(entry));
    } else if (member.kind == ASTKind::NamespaceDecl) {
        visitNamespace(static_cast<const NamespaceDecl&>(member), nsPath);
    } else if (member.kind == ASTKind::TypeDecl) {
        const auto& cls = static_cast<const TypeDecl&>(member);

        // Skip constraint, adapt, typefn — they don't have visibility entries
        if (cls.isConstraint() || cls.isAdapt() || cls.isTypeFn()) {
            return;
        }

        std::string classPath = nsPath + "::" + cls.name;

        for (const auto& clsSection : cls.sections) {
            if (clsSection->kind != ASTKind::VisibilitySection) continue;
            const auto& clsVis = static_cast<const VisibilitySection&>(*clsSection);
            Visibility memberVis = forceInternal ? Visibility::Internal : clsVis.visibility;

            for (const auto& clsMember : clsVis.members) {
                if (clsMember->kind == ASTKind::FnDecl) {
                    const auto& func = static_cast<const FnDecl&>(*clsMember);
                    // Skip constructors, destructors, operators
                    if (func.isConstructor || func.isDestructor || func.isOperator()) {
                        continue;
                    }
                    std::string qualified = classPath + "::" + func.name;
                    VisibilityEntry entry;
                    entry.qualifiedName = qualified;
                    entry.visibility = memberVis;
                    entry.isConst = func.isConst;
                    entry.paramConsts = extractParamConsts(func);
                    entry.bindingTarget = func.bindingTarget;
                    entry.priority = func.priority;
                    entries_.push_back(std::move(entry));
                }
            }
        }
    } else if (member.kind == ASTKind::IfDecl) {
        const auto& ci = static_cast<const IfDecl&>(member);
        for (const auto& m : ci.thenBody) {
            visitMember(*m, nsPath, vis, forceInternal);
        }
        for (const auto& m : ci.elseBody) {
            visitMember(*m, nsPath, vis, forceInternal);
        }
    } else if (member.kind == ASTKind::DataDecl) {
        // Data declarations (lifetime, member vars, etc.) don't affect visibility
    } else if (member.kind == ASTKind::FnLogicBlock) {
        // Logic blocks don't have visibility entries
    }
}

} // namespace topo
