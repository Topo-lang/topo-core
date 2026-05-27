#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Sema/SmartStructMatcher.h"
#include "topo/Sema/TypeRegistry.h"
#include "topo/Sema/TypeResolver.h"
#include <functional>
#include <map>
#include <queue>
#include <set>

namespace topo {

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine& diag) : diag_(diag) {}

std::string SemanticAnalyzer::normalizeName(const std::string& dottedName) {
    // Same logic as VisibilityCollector: convert '.' to '::'
    std::string temp = dottedName;
    for (auto& ch : temp) {
        if (ch == '.') ch = ':';
    }
    std::string normalized;
    for (size_t i = 0; i < temp.size(); ++i) {
        if (temp[i] == ':' && i + 1 < temp.size() && temp[i + 1] == ':') {
            normalized += "::";
            ++i;
        } else if (temp[i] == ':') {
            normalized += "::";
        } else {
            normalized += temp[i];
        }
    }
    return normalized;
}

SymbolTable SemanticAnalyzer::analyze(const TopoFile& root) {
    symbols_ = SymbolTable{};

    // Pass 1: Collect declarations
    collectDeclarations(root);

    // Pass 2: Validate types
    if (!diag_.hasErrors()) {
        validateTypes();
    }

    // Pass 3: Validate fn blocks
    if (!diag_.hasErrors()) {
        validateLogicBlocks();
    }

    return std::move(symbols_);
}

SymbolTable SemanticAnalyzer::analyze(const TopoFile& root, const SymbolTable& importedSymbols) {
    symbols_ = SymbolTable{};

    // Pass 1: Collect declarations from current file
    collectDeclarations(root);

    // Merge imported symbols needed for cross-file type resolution and
    // fn block validation.  Logic blocks are intentionally excluded to
    // avoid re-validating imported fn blocks.
    for (const auto& [name, ta] : importedSymbols.typeAliases()) {
        symbols_.addTypeAlias(ta);
    }
    for (const auto& entry : importedSymbols.imports()) {
        symbols_.addImport(entry);
    }
    for (const auto& [name, cs] : importedSymbols.classSymbols()) {
        if (cs.visibility != Visibility::Internal) {
            symbols_.addClassSymbol(cs);
        }
    }
    for (const auto& [name, cs] : importedSymbols.constraintSymbols()) {
        symbols_.addConstraintSymbol(cs);
    }

    // Merge imported functions (excluding internal) for cross-file validation
    for (const auto& [name, fn] : importedSymbols.functions()) {
        if (fn.visibility != Visibility::Internal) {
            symbols_.addFunction(fn);
        }
    }

    // Pass 1.5: Check cross-file references to internal symbols
    // Scan fn blocks in this file for calls to imported internal functions
    for (const auto& [blockName, block] : symbols_.logicBlocks()) {
        std::string nsPrefix;
        auto lastSep = blockName.rfind("::");
        if (lastSep != std::string::npos) {
            nsPrefix = blockName.substr(0, lastSep + 2);
        }

        for (const auto& callee : block.calledFunctions) {
            if (callee.size() > 8 && callee.substr(0, 8) == "<assign:") {
                continue;
            }
            std::string qualifiedCallee = nsPrefix + normalizeName(callee);
            const auto* calleeSym = importedSymbols.findFunction(qualifiedCallee);
            if (calleeSym && calleeSym->visibility == Visibility::Internal) {
                diag_.error(block.location,
                            "function '" + callee + "' is declared internal in '" + calleeSym->location.file +
                                "' and cannot be accessed from other files");
            }
        }
    }

    // Pass 2: Validate types
    if (!diag_.hasErrors()) {
        validateTypes();
    }

    // Pass 3: Validate fn blocks
    if (!diag_.hasErrors()) {
        validateLogicBlocks();
    }

    return std::move(symbols_);
}

// --- Pass 1: Declaration collection ---

void SemanticAnalyzer::collectDeclarations(const TopoFile& root) {
    // Collect top-level using declarations and std::import
    for (const auto& decl : root.declarations) {
        if (decl->kind == ASTKind::DataDecl) {
            const auto& data = static_cast<const DataDecl&>(*decl);
            if (data.isStdImport()) {
                ImportEntry entry{data.importPath, data.name, data.location};
                symbols_.addImport(entry);
            } else {
                // Type alias: using Name = Type;
                TypeAliasEntry entry{data.name, data.type, data.location};
                if (!symbols_.addTypeAlias(entry)) {
                    diag_.error(data.location, "duplicate type alias '" + data.name + "'");
                }
            }
        } else if (decl->kind == ASTKind::NamespaceDecl) {
            visitNamespace(static_cast<const NamespaceDecl&>(*decl), "", Visibility::Private);
        } else if (decl->kind == ASTKind::DebugDecl) {
            // Defer DebugDecl validation until after every type/data symbol is
            // collected — debug entries name targets by unqualified IDENT and
            // must resolve against the global symbol table.
            // (No-op here; handled in the second sweep below.)
        }
    }

    // Second sweep: resolve DebugDecl target names against
    // the now-populated symbol table and record validated entries.
    for (const auto& decl : root.declarations) {
        if (decl->kind != ASTKind::DebugDecl) continue;
        const auto& d = static_cast<const DebugDecl&>(*decl);

        DebugEntry entry;
        entry.targetTypeName = d.targetTypeName;
        entry.location = d.location;
        entry.views = d.views;
        entry.summaryTemplate = d.summaryTemplate;
        entry.inactiveRegions = d.inactiveRegions;
        entry.renderDecls = d.renderDecls;

        // Resolve target — accept TypeDecl (preferred) or DataDecl alias.
        const ClassSymbol* tsym = symbols_.findClassBySimpleName(d.targetTypeName);
        if (tsym) {
            entry.targetKind = DebugTargetKind::Type;
            entry.targetQualifiedName = tsym->qualifiedName;
        } else {
            // Try type alias (DataDecl with name).
            const TypeAliasEntry* alias = symbols_.findTypeAlias(d.targetTypeName);
            if (alias) {
                entry.targetKind = DebugTargetKind::Data;
                entry.targetQualifiedName = alias->name;
            } else {
                diag_.error(d.location,
                            "unknown debug target '" + d.targetTypeName +
                                "' (must name an existing type or data declaration)");
                continue; // skip recording — debug target is invalid
            }
        }

        // View-expression validation: each view's slice expr must be either
        // a bare `field` identifier or `field[start..end]`. Both forms are
        // enforced by the Parser, so here we only re-verify that the
        // container identifier looks like one. Reject empty container (Parser
        // already errors on missing IDENT, but defense-in-depth).
        for (const auto& v : d.views) {
            if (v.slice.container.empty()) {
                diag_.error(v.location, "view expression must be a named slice");
            } else if (v.slice.isSliced && v.slice.start && v.slice.end &&
                       *v.slice.end < *v.slice.start) {
                diag_.error(v.slice.location,
                            "view slice end (" + std::to_string(*v.slice.end) +
                                ") must be >= start (" + std::to_string(*v.slice.start) + ")");
            }
        }
        // Inactive regions reuse the same slice form — same defense.
        for (const auto& r : d.inactiveRegions) {
            if (r.region.container.empty()) {
                diag_.error(r.location, "inactive_region expression must be a named slice");
            } else if (r.region.isSliced && r.region.start && r.region.end &&
                       *r.region.end < *r.region.start) {
                diag_.error(r.region.location,
                            "inactive_region slice end (" + std::to_string(*r.region.end) +
                                ") must be >= start (" + std::to_string(*r.region.start) + ")");
            }
        }
        // Render blocks: the Parser accepts `render` syntax, but the web
        // rendering semantics (turning a render block into a visual panel)
        // are NOT implemented. Per the no-silent-degradation principle,
        // accepting syntax while producing no behavior is a silent partial
        // feature — explicitly reject instead of passing through. Drop the
        // captured render decls so no downstream consumer mistakes them for
        // an active feature.
        for (const auto& r : d.renderDecls) {
            diag_.error(r.location,
                        "render block: rendering not implemented "
                        "(the `render` block is parsed but no web-rendering "
                        "behavior is generated; remove it until rendering is "
                        "supported)");
        }
        entry.renderDecls.clear();

        symbols_.addDebugEntry(std::move(entry));
    }
}

void SemanticAnalyzer::visitNamespace(const NamespaceDecl& ns,
                                      const std::string& parentPath,
                                      Visibility /*currentVis*/) {
    std::string nsPath = parentPath;
    for (size_t i = 0; i < ns.path.size(); ++i) {
        if (!nsPath.empty()) nsPath += "::";
        nsPath += ns.path[i];
    }

    for (const auto& section : ns.sections) {
        if (section->kind == ASTKind::VisibilitySection) {
            visitSection(static_cast<const VisibilitySection&>(*section), nsPath, ns.isInternal);
        }
    }
}

void SemanticAnalyzer::visitSection(const VisibilitySection& section, const std::string& nsPath, bool forceInternal) {
    Visibility vis = forceInternal ? Visibility::Internal : section.visibility;
    for (const auto& member : section.members) {
        visitMember(*member, nsPath, vis);
    }
}

void SemanticAnalyzer::visitMember(const ASTNode& member, const std::string& nsPath, Visibility visibility) {
    if (member.kind == ASTKind::FnDecl) {
        const auto& func = static_cast<const FnDecl&>(member);

        // Reject external on constructors, destructors, operators
        if (func.isConstructor || func.isDestructor || func.isOperator()) {
            if (func.hasModifier(ModifierData::External)) {
                diag_.error(func.location, "'external' cannot be applied to " +
                    std::string(func.isConstructor ? "constructors" :
                                func.isDestructor ? "destructors" : "operators"));
            }
            return;
        }

        // External + comptime mutual exclusion (check before comptime early return)
        if (func.hasModifier(ModifierData::External) && func.hasModifier(ModifierData::Comptime)) {
            diag_.error(func.location, "'external' and 'comptime' are mutually exclusive");
            return;
        }

        // Comptime fn: just validate structure exists for now
        if (func.isComptime()) {
            return;
        }

        std::string normalized = normalizeName(func.name);
        std::string qualified = nsPath + "::" + normalized;

        FunctionSymbol sym;
        sym.qualifiedName = qualified;
        sym.simpleName = func.name;
        sym.visibility = visibility;
        sym.location = func.location;
        sym.returnType = func.returnType;
        sym.params = func.params;
        sym.isConst = func.isConst;
        sym.isStatic = func.isStatic;
        sym.hasLogicBlock = false;
        sym.isMultiReturn = func.isMultiReturn;
        sym.returnParams = func.returnParams;
        sym.hasUsedReturnsClause = func.hasUsedReturnsClause;
        for (const auto& n : func.declaredUsedReturns) sym.usedReturns.insert(n);
        sym.templateParams = func.templateParams;
        sym.bindingTarget = func.bindingTarget;
        sym.priority = func.priority;
        sym.cardinality = func.cardinality;
        sym.accessPattern = func.accessPattern;
        sym.tiledSize = func.tiledSize;
        sym.isExternal = func.hasModifier(ModifierData::External);

        if (!symbols_.addFunction(sym)) {
            diag_.error(func.location, "duplicate function declaration '" + qualified + "'");
        }
    } else if (member.kind == ASTKind::FnLogicBlock) {
        const auto& block = static_cast<const FnLogicBlock&>(member);

        // Reject external on logic blocks
        if (block.hasModifier(ModifierData::External)) {
            diag_.error(block.location, "'external' cannot be applied to fn blocks");
        }

        std::string normalized = normalizeName(block.name);
        std::string qualified = nsPath + "::" + normalized;

        LogicBlockEntry entry;
        entry.qualifiedName = qualified;
        entry.simpleName = block.name;
        entry.location = block.location;

        if (block.isPipeline()) {
            entry.isPipeline = true;
            entry.edges = block.pipelineEdges;
        }
        entry.isFlow = block.hasModifier(ModifierData::Flow);

        for (const auto& op : block.operations) {
            if (op->kind == ASTKind::OperationDecl) {
                const auto& opDecl = static_cast<const OperationDecl&>(*op);
                if (opDecl.isAssignment()) {
                    entry.calledFunctions.push_back("<assign:" + opDecl.varName + ">");
                } else {
                    entry.calledFunctions.push_back(opDecl.funcName);
                }
                entry.stages.push_back(opDecl.stage);
            }
        }

        if (!symbols_.addLogicBlock(entry)) {
            diag_.error(block.location, "duplicate fn block '" + qualified + "'");
        }
    } else if (member.kind == ASTKind::DataDecl) {
        const auto& data = static_cast<const DataDecl&>(member);

        if (data.hasModifier(ModifierData::External)) {
            diag_.error(data.location, "'external' can only modify function declarations");
        }

        if (data.isStdImport()) {
            ImportEntry entry{data.importPath, data.name, data.location};
            symbols_.addImport(entry);
        } else if (data.isLifetime()) {
            LifetimeGroupEntry entry;
            entry.name = data.name;
            entry.startFunc = data.startFunc;
            entry.endFunc = data.endFunc;
            entry.isOpenEnded = data.isOpenEnded;
            entry.isSingleFunc = data.isSingleFunc;
            entry.location = data.location;
            symbols_.addLifetimeGroup(entry);
        } else if (data.isInstantiate()) {
            visitInstantiateDataDecl(data, nsPath);
        } else {
            // Type alias or member variable (inside type body)
            // If name is set and type has nameParts, it could be either
            if (!data.name.empty() && !data.type.nameParts.empty()) {
                // Could be a type alias (from using) or member var
                // Type aliases come from 'using Name = Type;'
                TypeAliasEntry entry{data.name, data.type, data.location};
                if (!symbols_.addTypeAlias(entry)) {
                    diag_.error(data.location, "duplicate type alias '" + data.name + "'");
                }
            } else if (!data.name.empty()) {
                // Type alias with empty type (shouldn't happen normally)
                TypeAliasEntry entry{data.name, data.type, data.location};
                symbols_.addTypeAlias(entry);
            }
        }
    } else if (member.kind == ASTKind::TypeDecl) {
        const auto& td = static_cast<const TypeDecl&>(member);

        if (td.hasModifier(ModifierData::External)) {
            diag_.error(td.location, "'external' can only modify function declarations");
        }

        if (td.isConstraint()) {
            visitConstraintTypeDecl(td, nsPath);
        } else if (td.isAdapt()) {
            visitAdaptTypeDecl(td, nsPath);
        } else if (td.isTypeFn()) {
            // TypeFn: record for type resolution — validate structure exists
        } else {
            // Regular class/type declaration
            visitTypeDecl(td, nsPath, visibility);
        }
    } else if (member.kind == ASTKind::IfDecl) {
        const auto& ci = static_cast<const IfDecl&>(member);
        for (const auto& m : ci.thenBody) {
            visitMember(*m, nsPath, visibility);
        }
        for (const auto& m : ci.elseBody) {
            visitMember(*m, nsPath, visibility);
        }
    } else if (member.kind == ASTKind::NamespaceDecl) {
        visitNamespace(static_cast<const NamespaceDecl&>(member), nsPath, visibility);
    }
}

void SemanticAnalyzer::visitTypeDecl(const TypeDecl& typeNode, const std::string& nsPath, Visibility visibility) {
    std::string qualified = nsPath + "::" + typeNode.name;

    if (!typeNode.specializationArgs.empty()) {
        qualified += "<";
        for (size_t i = 0; i < typeNode.specializationArgs.size(); ++i) {
            if (i > 0) qualified += ", ";
            qualified += typeNode.specializationArgs[i].toString();
        }
        qualified += ">";
    }

    ClassSymbol classSym;
    classSym.qualifiedName = qualified;
    classSym.simpleName = typeNode.name;
    classSym.visibility = visibility;
    classSym.location = typeNode.location;
    classSym.baseClass = typeNode.baseClass;
    classSym.templateParams = typeNode.templateParams;

    for (const auto& section : typeNode.sections) {
        if (section->kind != ASTKind::VisibilitySection) continue;
        const auto& vis = static_cast<const VisibilitySection&>(*section);

        for (const auto& member : vis.members) {
            if (member->kind == ASTKind::FnDecl) {
                const auto& func = static_cast<const FnDecl&>(*member);

                if (func.isConstructor) {
                    if (func.hasModifier(ModifierData::External)) {
                        diag_.error(func.location, "'external' cannot be applied to constructors");
                    }
                    std::string ctorQualified = qualified + "::" + typeNode.name;
                    std::string ctorKey = ctorQualified + "/" + std::to_string(func.params.size());
                    classSym.constructors.push_back(ctorKey);

                } else if (func.isDestructor) {
                    if (func.hasModifier(ModifierData::External)) {
                        diag_.error(func.location, "'external' cannot be applied to destructors");
                    }
                    std::string dtorQualified = qualified + "::~" + typeNode.name;
                    if (!classSym.destructor.empty()) {
                        diag_.error(member->location, "duplicate destructor in type '" + qualified + "'");
                    }
                    classSym.destructor = dtorQualified;

                } else if (func.isOperator()) {
                    OperatorSymbol opSym;
                    opSym.op = *func.operatorOp;
                    opSym.params = func.params;
                    opSym.returnType = func.returnType;
                    opSym.bindingTarget = func.bindingTarget;
                    opSym.location = func.location;
                    classSym.operatorOverloads.push_back(std::move(opSym));

                } else {
                    // External + comptime mutual exclusion (same as visitMember)
                    if (func.hasModifier(ModifierData::External) && func.hasModifier(ModifierData::Comptime)) {
                        diag_.error(func.location, "'external' and 'comptime' are mutually exclusive");
                        continue;
                    }

                    // Regular member function
                    std::string normalized = normalizeName(func.name);
                    std::string memberQualified = qualified + "::" + normalized;

                    FunctionSymbol sym;
                    sym.qualifiedName = memberQualified;
                    sym.simpleName = func.name;
                    sym.visibility = vis.visibility;
                    sym.location = func.location;
                    sym.returnType = func.returnType;
                    sym.params = func.params;
                    sym.isConst = func.isConst;
                    sym.isStatic = func.isStatic;
                    sym.hasLogicBlock = false;
                    sym.isMultiReturn = func.isMultiReturn;
                    sym.returnParams = func.returnParams;
                    sym.hasUsedReturnsClause = func.hasUsedReturnsClause;
                    for (const auto& n : func.declaredUsedReturns) sym.usedReturns.insert(n);
                    sym.priority = func.priority;
                    sym.cardinality = func.cardinality;
                    sym.accessPattern = func.accessPattern;
                    sym.tiledSize = func.tiledSize;
                    sym.isExternal = func.hasModifier(ModifierData::External);

                    if (!symbols_.addFunction(sym)) {
                        diag_.error(func.location, "duplicate member function declaration '" + memberQualified + "'");
                    }
                    classSym.memberFunctions.push_back(memberQualified);
                }

            } else if (member->kind == ASTKind::DataDecl) {
                const auto& data = static_cast<const DataDecl&>(*member);
                if (!data.isStdImport() && !data.isLifetime() && !data.isInstantiate() &&
                    !data.type.nameParts.empty()) {
                    // Member variable or type alias inside type
                    if (data.name.empty()) continue;

                    // Check if it's a type alias (from using) by seeing if type
                    // looks like a binding. For member vars, they have concrete types.
                    // The parser produces DataDecl for both — distinguish by checking
                    // if this came from a 'using' context (type alias has no
                    // ownership qualifiers or modifiers typically, but we just
                    // store as-is since the SymbolTable differentiates).
                    MemberVarSymbol mvs;
                    mvs.name = data.name;
                    mvs.type = data.type;
                    mvs.isStatic = data.isStatic();
                    mvs.location = data.location;
                    classSym.memberVars.push_back(std::move(mvs));
                }
            }
        }
    }

    // Validate operator overloads
    for (size_t i = 0; i < classSym.operatorOverloads.size(); ++i) {
        const auto& opSym = classSym.operatorOverloads[i];
        size_t arity = opSym.params.size();

        bool isUnary = (opSym.op == OverloadableOp::Tilde || opSym.op == OverloadableOp::Bang);
        if (isUnary && arity != 1) {
            diag_.error(opSym.location,
                        "unary operator '" + std::string(overloadableOpName(opSym.op)) +
                            "' requires exactly 1 parameter, got " + std::to_string(arity));
        } else if (!isUnary && arity != 2) {
            diag_.error(opSym.location,
                        "binary operator '" + std::string(overloadableOpName(opSym.op)) +
                            "' requires exactly 2 parameters, got " + std::to_string(arity));
        }

        bool hasClassParam = false;
        for (const auto& param : opSym.params) {
            if (!param.type.nameParts.empty() && param.type.nameParts.back() == typeNode.name) {
                hasClassParam = true;
                break;
            }
        }
        if (!hasClassParam) {
            diag_.error(opSym.location,
                        "operator '" + std::string(overloadableOpName(opSym.op)) +
                            "' must have at least one parameter of type '" + typeNode.name + "'");
        }

        for (size_t j = 0; j < i; ++j) {
            if (classSym.operatorOverloads[j].op == opSym.op) {
                diag_.error(opSym.location,
                            "duplicate operator '" + std::string(overloadableOpName(opSym.op)) + "' in type '" +
                                qualified + "'");
                break;
            }
        }
    }

    if (!symbols_.addClassSymbol(classSym)) {
        diag_.error(typeNode.location, "duplicate type declaration '" + qualified + "'");
    }
}

void SemanticAnalyzer::visitConstraintTypeDecl(const TypeDecl& constraint, const std::string& nsPath) {
    std::string qualified = nsPath + "::" + constraint.name;

    ConstraintSymbol sym;
    sym.qualifiedName = qualified;
    sym.simpleName = constraint.name;
    sym.parentConstraint = constraint.parentConstraint;
    sym.members = constraint.constraintMembers;
    sym.location = constraint.location;

    if (!symbols_.addConstraintSymbol(sym)) {
        diag_.error(constraint.location, "duplicate constraint declaration '" + qualified + "'");
    }
}

void SemanticAnalyzer::visitAdaptTypeDecl(const TypeDecl& adapt,
                                          [[maybe_unused]] const std::string& nsPath) {
    auto abstractKind = TypeRegistry::classifyAbstractName(adapt.adaptConstraintName);
    if (abstractKind) {
        bool hasFrom = false, hasTo = false;
        for (const auto& mapping : adapt.adaptMappings) {
            if (mapping.memberName == "from") hasFrom = true;
            if (mapping.memberName == "to") hasTo = true;
        }
        if (!hasFrom) {
            diag_.error(adapt.location,
                        "type adapter for '" + adapt.adaptConstraintName + "' is missing 'from' mapping");
        }
        if (!hasTo) {
            diag_.error(adapt.location, "type adapter for '" + adapt.adaptConstraintName + "' is missing 'to' mapping");
        }

        AdaptEntry entry;
        entry.constraintName = adapt.adaptConstraintName;
        entry.targetType = adapt.adaptTargetType;
        entry.mappings = adapt.adaptMappings;
        entry.location = adapt.location;
        symbols_.addAdapt(entry);
        return;
    }

    const auto* constraint = symbols_.findConstraintSymbol(adapt.adaptConstraintName);
    if (!constraint) {
        AdaptEntry entry;
        entry.constraintName = adapt.adaptConstraintName;
        entry.targetType = adapt.adaptTargetType;
        entry.mappings = adapt.adaptMappings;
        entry.location = adapt.location;
        symbols_.addAdapt(entry);
        return;
    }

    for (const auto& cm : constraint->members) {
        bool found = false;
        for (const auto& mapping : adapt.adaptMappings) {
            if (mapping.memberName == cm.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            diag_.error(
                adapt.location,
                "adapt for '" + adapt.adaptConstraintName + "' is missing mapping for member '" + cm.name + "'");
        }
    }

    AdaptEntry entry;
    entry.constraintName = adapt.adaptConstraintName;
    entry.targetType = adapt.adaptTargetType;
    entry.mappings = adapt.adaptMappings;
    entry.location = adapt.location;
    symbols_.addAdapt(entry);
}

void SemanticAnalyzer::visitInstantiateDataDecl(const DataDecl& inst, const std::string& nsPath) {
    InstantiateEntry entry;
    entry.type = inst.type;
    entry.location = inst.location;

    std::string typeName = inst.type.toString();
    if (!nsPath.empty() && !inst.type.nameParts.empty()) {
        entry.qualifiedName = nsPath + "::" + typeName;
    } else {
        entry.qualifiedName = typeName;
    }

    symbols_.addInstantiate(entry);
}

// --- Pass 2: Type validation ---

void SemanticAnalyzer::validateTypes() {
    TypeResolver resolver(symbols_);

    for (const auto& [name, sym] : symbols_.functions()) {
        std::string reason;

        // If this function is a member of a class template, add class template
        // params to scope first
        for (const auto& [clsName, clsSym] : symbols_.classSymbols()) {
            if (!clsSym.templateParams.empty() && name.substr(0, clsName.size()) == clsName &&
                name.size() > clsName.size() && name[clsName.size()] == ':') {
                resolver.addTemplateParamNames(clsSym.templateParams);
                break;
            }
        }

        // If this is a template function, add template param names to scope
        if (!sym.templateParams.empty()) {
            resolver.addTemplateParamNames(sym.templateParams);

            // Check for duplicate template parameter names (T003)
            std::set<std::string> seen;
            for (const auto& tp : sym.templateParams) {
                if (!seen.insert(tp.name).second) {
                    diag_.error(tp.location, "duplicate template parameter name '" + tp.name + "' (T003)");
                }
            }
        }

        if (sym.isMultiReturn) {
            // Multi-return: validate each return param type
            if (sym.returnParams.size() < 2) {
                diag_.error(sym.location, "multi-return function '" + name + "' requires at least 2 return parameters");
            }

            // Check for duplicate return param names
            std::set<std::string> seenNames;
            for (const auto& rp : sym.returnParams) {
                if (!seenNames.insert(rp.name).second) {
                    diag_.error(rp.loc, "duplicate return parameter name '" + rp.name + "' in function '" + name + "'");
                }
                if (!resolver.isValidType(rp.type, reason)) {
                    const auto& loc = rp.type.location.endLine > 0 ? rp.type.location : rp.loc;
                    diag_.error(loc,
                                "invalid return parameter type for '" + rp.name + "' in '" + name + "': " + reason,
                                "unknown-type");
                }
            }

            // Validate the `with returns(a, _)` selective-return clause.  The
            // parser already rejects empty clauses and within-clause
            // duplicates; here we verify name membership (each listed name
            // must match a declared return parameter).
            if (sym.hasUsedReturnsClause) {
                std::set<std::string> declaredParamNames;
                for (const auto& rp : sym.returnParams) declaredParamNames.insert(rp.name);
                for (const auto& n : sym.usedReturns) {
                    if (!declaredParamNames.count(n)) {
                        diag_.error(sym.location,
                                    "'with returns(...)' in '" + name + "' names unknown return parameter '" + n +
                                        "' — must match a declared return name");
                    }
                }
            }
        } else {
            // Single return: validate return type
            if (!resolver.isValidType(sym.returnType, reason)) {
                const auto& loc = sym.returnType.location.endLine > 0 ? sym.returnType.location : sym.location;
                diag_.error(loc, "invalid return type for '" + name + "': " + reason, "unknown-type");
            }
            validateTemplateArgs(sym.returnType, resolver);
        }

        // Validate parameter types
        for (const auto& param : sym.params) {
            if (!resolver.isValidType(param.type, reason)) {
                const auto& loc = param.type.location.endLine > 0 ? param.type.location : sym.location;
                diag_.error(loc,
                            "invalid parameter type for '" + param.name + "' in '" + name + "': " + reason,
                            "unknown-type");
            }
            validateTemplateArgs(param.type, resolver);
            validateOwnershipRules(param.type,
                                   "parameter '" + param.name + "' in '" + name + "'",
                                   param.type.location.endLine > 0 ? param.type.location : sym.location);
        }

        // S-OWN-003: weak return type is an error
        if (!sym.isMultiReturn && sym.returnType.ownership == OwnershipKind::Weak) {
            diag_.error(sym.returnType.location.endLine > 0 ? sym.returnType.location : sym.location,
                        "return type of '" + name +
                            "' cannot have 'weak' qualifier — caller "
                            "cannot guarantee lifetime (S-OWN-003)");
        }
        if (sym.isMultiReturn) {
            for (const auto& rp : sym.returnParams) {
                if (rp.type.ownership == OwnershipKind::Weak) {
                    diag_.error(rp.loc,
                                "return parameter '" + rp.name + "' in '" + name +
                                    "' cannot have 'weak' qualifier — "
                                    "caller cannot guarantee lifetime (S-OWN-003)");
                }
            }
        }

        // Validate cardinality hints
        if (sym.cardinality) {
            const auto& card = *sym.cardinality;
            if (card.min < 0 && card.max < 0) {
                diag_.error(sym.location, "cardinality hint for '" + name + "' must specify at least min value");
            }
            if (card.min >= 0 && card.max >= 0 && card.min > card.max) {
                diag_.error(sym.location,
                            "cardinality hint for '" + name + "' has min (" + std::to_string(card.min) + ") > max (" +
                                std::to_string(card.max) + ")");
            }
        }

        if (sym.accessPattern == AccessPattern::Tiled && sym.tiledSize <= 0) {
            diag_.error(sym.location, "access(tiled) for '" + name + "' requires a positive tile size");
        }

        // Clear template param scope after each function
        resolver.clearTemplateParamNames();
    }

    // Validate class member types and base classes
    for (const auto& [name, cls] : symbols_.classSymbols()) {
        // Add class template params to scope
        if (!cls.templateParams.empty()) {
            resolver.addTemplateParamNames(cls.templateParams);
        }

        // Validate base class type
        if (cls.baseClass) {
            std::string reason;
            if (!resolver.isValidType(*cls.baseClass, reason)) {
                const auto& loc = cls.baseClass->location.endLine > 0 ? cls.baseClass->location : cls.location;
                diag_.error(loc, "invalid base class type for '" + name + "': " + reason, "unknown-type");
            }
            validateTemplateArgs(*cls.baseClass, resolver);
        }

        // Validate member variable types
        for (const auto& mv : cls.memberVars) {
            std::string reason;
            if (!resolver.isValidType(mv.type, reason)) {
                const auto& loc = mv.type.location.endLine > 0 ? mv.type.location : mv.location;
                diag_.error(loc,
                            "invalid member variable type for '" + mv.name + "' in '" + name + "': " + reason,
                            "unknown-type");
            }
            validateTemplateArgs(mv.type, resolver);
            validateOwnershipRules(mv.type,
                                   "member '" + mv.name + "' in class '" + name + "'",
                                   mv.type.location.endLine > 0 ? mv.type.location : mv.location);
        }

        // Clear class template param scope
        if (!cls.templateParams.empty()) {
            resolver.clearTemplateParamNames();
        }
    }

    // Validate ownership graph for shared cycle detection (S-OWN-004)
    validateOwnershipGraph();

    // Validate constraint declarations
    for (const auto& [name, cs] : symbols_.constraintSymbols()) {
        // Check parent constraint exists
        if (cs.parentConstraint) {
            if (!symbols_.findConstraintSymbol(*cs.parentConstraint)) {
                diag_.error(cs.location,
                            "parent constraint '" + *cs.parentConstraint + "' not found for constraint '" + name + "'");
            } else {
                // Cycle detection: walk the parent chain and check for loops
                std::set<std::string> visited;
                visited.insert(name);
                std::string current = *cs.parentConstraint;
                while (!current.empty()) {
                    if (visited.count(current)) {
                        diag_.error(
                            cs.location,
                            "circular constraint inheritance: '" + name + "' forms a cycle through '" + current + "'");
                        break;
                    }
                    visited.insert(current);
                    const auto* parentCs = symbols_.findConstraintSymbol(current);
                    if (!parentCs || !parentCs->parentConstraint) break;
                    current = *parentCs->parentConstraint;
                }
            }
        }
    }

    // Validate template parameter constraint references
    for (const auto& [fname, sym] : symbols_.functions()) {
        for (const auto& tp : sym.templateParams) {
            if (tp.kind == TemplateParamDecl::TypeParam && !tp.constraintType.nameParts.empty()) {
                const auto& constraintName = tp.constraintType.nameParts[0];
                if (!symbols_.findConstraintSymbol(constraintName)) {
                    diag_.error(
                        tp.location,
                        "unknown constraint '" + constraintName + "' on template parameter '" + tp.name + "' (T001)");
                }
            }
        }
    }
    for (const auto& [cname, cls] : symbols_.classSymbols()) {
        for (const auto& tp : cls.templateParams) {
            if (tp.kind == TemplateParamDecl::TypeParam && !tp.constraintType.nameParts.empty()) {
                const auto& constraintName = tp.constraintType.nameParts[0];
                if (!symbols_.findConstraintSymbol(constraintName)) {
                    diag_.error(
                        tp.location,
                        "unknown constraint '" + constraintName + "' on template parameter '" + tp.name + "' (T001)");
                }
            }
        }
    }
}

// --- Template argument count validation (T002) ---

void SemanticAnalyzer::validateTemplateArgs(const TypeNode& type, const TypeResolver& resolver) {
    if (type.templateArgs.empty()) return;

    // Find the class symbol by simple name (single-part) or qualified name
    const ClassSymbol* targetClass = nullptr;
    if (type.nameParts.size() == 1) {
        for (const auto& [qname, cls] : symbols_.classSymbols()) {
            if (cls.simpleName == type.nameParts[0]) {
                targetClass = &cls;
                break;
            }
        }
    }

    if (targetClass && !targetClass->templateParams.empty()) {
        size_t expected = targetClass->templateParams.size();
        size_t actual = type.templateArgs.size();
        if (actual != expected) {
            diag_.error(type.location,
                        "template argument count mismatch for '" + type.nameParts[0] + "': expected " +
                            std::to_string(expected) + ", got " + std::to_string(actual) + " (T002)");
        }
    }

    // Recursively validate template arguments' own template args
    for (const auto& arg : type.templateArgs) {
        validateTemplateArgs(arg, resolver);
    }
}

// --- Callee resolution (supports cross-namespace references) ---

std::string SemanticAnalyzer::resolveCallee(const std::string& callee, const std::string& nsPrefix) const {
    // 1. Try same-namespace: nsPrefix + callee
    if (!nsPrefix.empty()) {
        std::string local = nsPrefix + callee;
        if (symbols_.findFunction(local)) return local;
    }

    // 2. If callee contains '::', treat as qualified reference.
    if (callee.find("::") != std::string::npos) {
        // Try relative: nsPrefix + callee (already tried above)
        // Try as absolute path
        if (symbols_.findFunction(callee)) return callee;

        // Try anchoring to parent namespaces of nsPrefix
        std::string prefix = nsPrefix;
        int maxIter = 100;
        while (!prefix.empty() && --maxIter > 0) {
            // Remove trailing "::"
            if (prefix.size() >= 2 && prefix[prefix.size() - 1] == ':' && prefix[prefix.size() - 2] == ':') {
                prefix = prefix.substr(0, prefix.size() - 2);
            }
            auto sep = prefix.rfind("::");
            if (sep != std::string::npos) {
                prefix = prefix.substr(0, sep + 2);
                std::string candidate = prefix + callee;
                if (symbols_.findFunction(candidate)) return candidate;
            } else {
                break;
            }
        }
    }

    // 3. For unqualified callee (no '::'), walk parent namespaces
    if (callee.find("::") == std::string::npos) {
        std::string prefix = nsPrefix;
        int maxIter2 = 100;
        while (!prefix.empty() && --maxIter2 > 0) {
            if (prefix.size() >= 2 && prefix[prefix.size() - 1] == ':' && prefix[prefix.size() - 2] == ':') {
                prefix = prefix.substr(0, prefix.size() - 2);
            }
            auto sep = prefix.rfind("::");
            if (sep != std::string::npos) {
                prefix = prefix.substr(0, sep + 2);
                std::string candidate = prefix + callee;
                if (symbols_.findFunction(candidate)) return candidate;
            } else {
                // Top-level
                if (symbols_.findFunction(callee)) return callee;
                break;
            }
        }
    }

    return ""; // not found
}

// --- Pass 3: fn block validation ---

void SemanticAnalyzer::validateLogicBlocks() {
    for (const auto& [blockName, block] : symbols_.logicBlocks()) {
        // Check 1: Orphan fn block — must have a matching FunctionDecl.
        // A `flow` is exempt: the flow name *is* the pipeline's identity,
        // it is self-declaring by construction (no companion signature to
        // pair with), so the orphan rule does not apply to it.
        if (symbols_.findFunction(blockName) == nullptr && !block.isFlow) {
            diag_.error(block.location, "fn block '" + blockName + "' has no matching function declaration");
        }

        // Mark the function as having a logic block
        auto* funcSym = const_cast<FunctionSymbol*>(symbols_.findFunction(blockName));
        if (funcSym) {
            funcSym->hasLogicBlock = true;

            // Binding functions must not have logic blocks
            if (funcSym->bindingTarget) {
                diag_.error(
                    block.location,
                    "fn block '" + blockName + "' conflicts with binding target '" + *funcSym->bindingTarget + "'");
            }
        }

        auto* mutableBlock = const_cast<LogicBlockEntry*>(symbols_.findLogicBlock(blockName));

        // Pipeline blocks have their own validation path
        if (block.isPipeline) {
            if (mutableBlock) {
                validatePipeline(blockName, *mutableBlock);
            }
            continue;
        }

        // --- Normal (non-pipeline) fn block validation ---

        // Check 2: Undeclared references
        std::string nsPrefix;
        auto lastSep = blockName.rfind("::");
        if (lastSep != std::string::npos) {
            nsPrefix = blockName.substr(0, lastSep + 2);
        }

        for (size_t i = 0; i < block.calledFunctions.size(); ++i) {
            const auto& callee = block.calledFunctions[i];
            // Skip assignment entries
            if (callee.size() > 8 && callee.substr(0, 8) == "<assign:") {
                continue;
            }
            std::string normalized = normalizeName(callee);
            std::string qualifiedCallee = resolveCallee(normalized, nsPrefix);
            if (qualifiedCallee.empty()) {
                diag_.error(block.location,
                            "fn block '" + blockName + "' references undeclared function '" + callee + "'",
                            "unknown-function");
                continue;
            }
        }

        // Check 3: Stage monotonicity
        int lastExplicitStage = 0;
        for (size_t i = 0; i < block.stages.size(); ++i) {
            int stage = block.stages[i];
            if (stage >= 0) {
                if (stage < lastExplicitStage) {
                    diag_.error(block.location,
                                "fn block '" + blockName + "': stage<" + std::to_string(stage) +
                                    "> is less than previous stage<" + std::to_string(lastExplicitStage) + ">");
                }
                lastExplicitStage = stage;
            }
        }

        // Check 4: Auto-stage assignment
        int maxStage = 0;
        for (int s : block.stages) {
            if (s > maxStage) maxStage = s;
        }
        int nextAutoStage = maxStage + 1;

        if (mutableBlock) {
            for (size_t i = 0; i < mutableBlock->stages.size(); ++i) {
                if (mutableBlock->stages[i] == -1) {
                    mutableBlock->stages[i] = nextAutoStage++;
                }
            }
        }

        // Check 5: S-OWN-002 — owned parameters in concurrent stage functions
        // Build a map: stage → list of function indices
        std::map<int, std::vector<size_t>> stageToOps;
        const auto& stages = mutableBlock ? mutableBlock->stages : block.stages;
        for (size_t i = 0; i < stages.size(); ++i) {
            if (stages[i] >= 0) {
                stageToOps[stages[i]].push_back(i);
            }
        }

        for (const auto& [stageNum, ops] : stageToOps) {
            if (ops.size() < 2) continue; // only check concurrent operations

            // Collect owned parameter type names across concurrent functions
            std::map<std::string, std::vector<std::string>> ownedTypeToFuncs;
            for (size_t idx : ops) {
                if (idx >= block.calledFunctions.size()) continue;
                const auto& callee = block.calledFunctions[idx];
                if (callee.size() > 8 && callee.substr(0, 8) == "<assign:") continue;

                std::string normalized = normalizeName(callee);
                std::string qualifiedCallee = resolveCallee(normalized, nsPrefix);
                const auto* funcSym = symbols_.findFunction(qualifiedCallee);
                if (!funcSym) continue;

                for (const auto& param : funcSym->params) {
                    if (param.type.ownership == OwnershipKind::Owned) {
                        std::string typeName = param.type.toString();
                        ownedTypeToFuncs[typeName].push_back(callee);
                    }
                }
            }

            for (const auto& [typeName, funcs] : ownedTypeToFuncs) {
                if (funcs.size() >= 2) {
                    diag_.warning(block.location,
                                  "fn block '" + blockName + "': owned parameter type '" + typeName + "' appears in " +
                                      std::to_string(funcs.size()) + " concurrent functions in stage<" +
                                      std::to_string(stageNum) + "> — potential ownership conflict (S-OWN-002)");
                }
            }
        }
    }
}

// --- Pipeline DAG validation ---

void SemanticAnalyzer::validatePipeline(const std::string& blockName, LogicBlockEntry& block) {
    // Resolve namespace prefix
    std::string nsPrefix;
    auto lastSep = blockName.rfind("::");
    if (lastSep != std::string::npos) {
        nsPrefix = blockName.substr(0, lastSep + 2);
    }

    // Collect all nodes and build adjacency / in-degree
    std::set<std::string> allNodes;
    std::map<std::string, std::vector<std::string>> adj; // source -> targets
    std::map<std::string, int> inDegree;
    int terminalCount = 0;
    std::string terminalNode;
    std::string terminalType;

    for (auto& edge : block.edges) {
        allNodes.insert(edge.source);
        if (!adj.count(edge.source)) adj[edge.source] = {};

        // Resolve terminal edges: Parser marks `void` as terminal.
        // For all other targets, Sema checks if the target is a known
        // type (type alias, std::import, or qualified std:: name) rather
        // than a function — if so, it's a terminal edge.
        // This is the correct place for this check because only Sema
        // has access to the SymbolTable with type information.
        if (!edge.isTerminal && !edge.target.empty()) {
            bool isType = false;

            // Check direct type alias match (e.g., target == "int"
            // when `using int = std::cpp17::int;` is declared)
            if (symbols_.findTypeAlias(edge.target)) {
                isType = true;
            }
            // Check std::import types
            else if (symbols_.isImportedType(edge.target)) {
                isType = true;
            }
            // Check if target is the resolved form of any type alias
            // (e.g., target == "std::cpp17::int" which is the RHS of
            // `using int = std::cpp17::int;`)
            else {
                for (const auto& taPair : symbols_.typeAliases()) {
                    if (taPair.second.targetType.toString() == edge.target) {
                        isType = true;
                        break;
                    }
                }
            }

            if (isType) {
                edge.isTerminal = true;
                edge.terminalType = edge.target;
                edge.target.clear();
            }
        }

        if (edge.isTerminal) {
            terminalCount++;
            terminalNode = edge.source;
            terminalType = edge.terminalType;
        } else {
            allNodes.insert(edge.target);
            adj[edge.source].push_back(edge.target);
            inDegree[edge.target]++;
            if (!inDegree.count(edge.source)) inDegree[edge.source] = inDegree[edge.source]; // ensure entry
        }
    }

    // Ensure all in-degree entries exist
    for (const auto& node : allNodes) {
        if (!inDegree.count(node)) inDegree[node] = 0;
    }

    // Terminal validation: exactly one terminal edge
    if (terminalCount == 0) {
        diag_.error(block.location,
                    "pipeline '" + blockName + "' has no terminal edge (need exactly one 'node -> Type;')");
        return;
    }
    if (terminalCount > 1) {
        diag_.error(
            block.location,
            "pipeline '" + blockName + "' has " + std::to_string(terminalCount) + " terminal edges (need exactly one)");
        return;
    }

    // Resolve all node names to qualified function names.
    // Supports cross-namespace references (e.g., "stages::filter").
    std::unordered_map<std::string, std::string> resolvedNodes;
    for (const auto& node : allNodes) {
        std::string qualified = resolveCallee(node, nsPrefix);
        if (!qualified.empty()) {
            resolvedNodes[node] = qualified;
        }
    }

    // Node existence: all nodes must be declared functions
    for (const auto& node : allNodes) {
        if (resolvedNodes.find(node) == resolvedNodes.end()) {
            diag_.error(block.location,
                        "pipeline '" + blockName + "' references undeclared function '" + node + "'",
                        "unknown-function");
        }
    }

    // Safety: limit maximum pipeline node count to prevent runaway analysis
    if (allNodes.size() > 10000) {
        diag_.error(block.location, "pipeline '" + blockName + "' exceeds maximum node count (10000)");
        return;
    }

    // Kahn's algorithm for topological sort (cycle detection)
    std::queue<std::string> queue;
    for (const auto& [node, deg] : inDegree) {
        if (deg == 0) queue.push(node);
    }

    std::vector<std::string> topoOrder;
    std::map<std::string, int> stageMap;
    std::vector<std::string> sourceNodes;

    // Initialize source nodes (no incoming edges) at stage 0
    {
        std::queue<std::string> tmpQ = queue;
        while (!tmpQ.empty()) {
            sourceNodes.push_back(tmpQ.front());
            stageMap[tmpQ.front()] = 0;
            tmpQ.pop();
        }
    }

    auto localInDeg = inDegree; // working copy
    while (!queue.empty()) {
        std::string node = queue.front();
        queue.pop();
        topoOrder.push_back(node);

        for (const auto& next : adj[node]) {
            // Stage inference: next node's stage = max(predecessors) + 1
            int candidateStage = stageMap[node] + 1;
            if (stageMap.find(next) == stageMap.end() || candidateStage > stageMap[next]) {
                stageMap[next] = candidateStage;
            }

            localInDeg[next]--;
            if (localInDeg[next] == 0) {
                queue.push(next);
            }
        }
    }

    if (topoOrder.size() != allNodes.size()) {
        diag_.error(block.location, "pipeline '" + blockName + "' contains a cycle");
        return;
    }

    // Helper: resolve a node name to its qualified function name
    auto qualifyNode = [&](const std::string& node) -> std::string {
        auto it = resolvedNodes.find(node);
        return (it != resolvedNodes.end()) ? it->second : node;
    };

    // Node-level type matching via SmartStructMatcher
    // For each non-source node, aggregate return params from all predecessors
    // and match against the node's parameter list.
    std::map<std::string, std::vector<std::string>> reverseAdj; // target -> sources
    for (const auto& edge : block.edges) {
        if (!edge.isTerminal) {
            reverseAdj[edge.target].push_back(edge.source);
        }
    }

    for (const auto& [targetNode, predecessors] : reverseAdj) {
        std::string tgtQualified = qualifyNode(targetNode);
        const auto* tgtFunc = symbols_.findFunction(tgtQualified);
        if (!tgtFunc) continue;

        // Aggregate all return params from predecessors
        std::vector<ReturnParam> aggregatedReturns;
        std::string predNames;
        bool anyMultiReturn = false;
        const FunctionSymbol* singleReturnPred = nullptr;
        int predCount = 0;
        for (const auto& pred : predecessors) {
            std::string srcQualified = qualifyNode(pred);
            const auto* srcFunc = symbols_.findFunction(srcQualified);
            if (!srcFunc) continue;

            if (!predNames.empty()) predNames += ", ";
            predNames += pred;
            ++predCount;

            if (srcFunc->isMultiReturn) {
                anyMultiReturn = true;
                for (const auto& rp : srcFunc->returnParams) {
                    aggregatedReturns.push_back(rp);
                }
            } else {
                singleReturnPred = srcFunc;
            }
        }

        if (aggregatedReturns.empty()) {
            // No multi-return producer fed this node. A `handler` whose Out
            // is a scalar or a single `record<...>` is not isMultiReturn, so
            // its edge never reaches SmartStructMatcher above. For a `flow`
            // (handler chain) the declaration model is exact-match — the
            // signature *is* the contract: a lone producer's whole return
            // type must equal the consumer's single input type, with no
            // record-width subtyping, so a wide record<...> into a narrower
            // `In` is an error.
            //
            // Scoped to `flow` blocks (`block.isFlow`): a regular `fn`
            // pipeline wires side-effecting stage functions (often
            // `void`-returning) and its edges express ordering, not
            // value threading — type-matching those would be wrong.
            if (block.isFlow && predCount == 1 && !anyMultiReturn &&
                singleReturnPred && tgtFunc->params.size() == 1) {
                const std::string produced =
                    singleReturnPred->returnType.toString();
                const std::string expected =
                    tgtFunc->params[0].type.toString();
                if (produced != expected) {
                    diag_.error(block.location,
                        "pipeline node '" + targetNode + "' (from [" +
                        predNames + "]): input type '" + expected +
                        "' does not match producer return type '" +
                        produced + "'");
                }
            }
            continue;
        }

        auto matchResult = SmartStructMatcher::match(aggregatedReturns, tgtFunc->params);
        if (!matchResult.success) {
            for (const auto& err : matchResult.errors) {
                diag_.error(block.location, "pipeline node '" + targetNode + "' (from [" + predNames + "]): " + err);
            }
        }
        for (const auto& warn : matchResult.warnings) {
            diag_.warning(block.location, "pipeline node '" + targetNode + "' (from [" + predNames + "]): " + warn);
        }
    }

    // Reverse demand analysis: from terminal backward
    std::map<std::string, std::set<std::string>> demand;
    // Terminal node: all return params are demanded (if multi-return)
    {
        std::string termQualified = qualifyNode(terminalNode);
        const auto* termFunc = symbols_.findFunction(termQualified);
        if (termFunc && termFunc->isMultiReturn) {
            for (const auto& rp : termFunc->returnParams) {
                demand[terminalNode].insert(rp.name);
            }
        }
    }

    // Process in reverse topological order — propagate demand
    for (auto it = topoOrder.rbegin(); it != topoOrder.rend(); ++it) {
        const std::string& node = *it;

        // For this node as a target, aggregate all predecessors and match
        if (reverseAdj.count(node)) {
            std::string tgtQualified = qualifyNode(node);
            const auto* tgtFunc = symbols_.findFunction(tgtQualified);
            if (!tgtFunc) continue;

            // Build aggregated returns with source tracking
            struct TaggedReturn {
                ReturnParam param;
                std::string sourceNode;
                size_t originalIndex;
            };
            std::vector<TaggedReturn> tagged;
            std::vector<ReturnParam> aggregated;

            for (const auto& pred : reverseAdj[node]) {
                std::string srcQ = qualifyNode(pred);
                const auto* srcFunc = symbols_.findFunction(srcQ);
                if (!srcFunc || !srcFunc->isMultiReturn) continue;

                for (size_t i = 0; i < srcFunc->returnParams.size(); ++i) {
                    tagged.push_back({srcFunc->returnParams[i], pred, i});
                    aggregated.push_back(srcFunc->returnParams[i]);
                }
            }

            auto matchResult = SmartStructMatcher::match(aggregated, tgtFunc->params);
            if (matchResult.success) {
                for (const auto& [si, ti] : matchResult.matches) {
                    const auto& t = tagged[si];
                    demand[t.sourceNode].insert(t.param.name);
                }
            }
        }
    }

    // Generate CallSiteInfo for each node
    for (const auto& node : allNodes) {
        CallSiteInfo info;
        info.caller = blockName;
        info.callee = qualifyNode(node);
        info.loc = block.location;
        if (demand.count(node)) {
            info.usedReturns = demand[node];
            info.style = CallSiteInfo::SmartPass;
        } else {
            info.style = CallSiteInfo::Full;
        }

        // Apply the callee's declared selective-return ceiling, if any.
        // `with returns(...)` declares that only named fields are observable;
        // demand from this call site is clamped to that ceiling. When no
        // pipeline demand exists (style == Full), the ceiling itself becomes
        // the demand — unnamed positions (`_`) are declared unobservable.
        if (const auto* calleeFn = symbols_.findFunction(info.callee);
            calleeFn && calleeFn->hasUsedReturnsClause) {
            if (info.style == CallSiteInfo::Full) {
                info.usedReturns = calleeFn->usedReturns;
                info.style = CallSiteInfo::SmartPass;
            } else {
                // Intersect pipeline demand with declared ceiling.
                std::set<std::string> clamped;
                for (const auto& n : info.usedReturns) {
                    if (calleeFn->usedReturns.count(n)) clamped.insert(n);
                }
                info.usedReturns = std::move(clamped);
            }
        }
        symbols_.addCallSite(info);
    }

    // Populate calledFunctions and stages from DAG nodes (qualified names).
    // Pipeline blocks use edges instead of operations, so calledFunctions
    // is not filled by the generic path.  PipelineCodeGenPass, TopoParallelPass,
    // TopoReorderPass, TopoLayoutPass, and IREmbed all rely on this list to
    // map simple node names to IR symbols.  The stages vector must be parallel
    // to calledFunctions for TopoReorderPass/TopoLayoutPass.
    for (const auto& node : topoOrder) {
        std::string qualified = qualifyNode(node);
        block.calledFunctions.push_back(qualified);
        block.stages.push_back(stageMap[node]);
    }

    // Store PipelineAnalysis
    PipelineAnalysis analysis;
    analysis.stages = stageMap;
    analysis.sourceNodes = sourceNodes;
    analysis.terminalNode = terminalNode;
    analysis.terminalType = terminalType;
    analysis.demand = demand;
    block.pipelineAnalysis = std::move(analysis);
}

// --- Ownership validation helpers ---

void SemanticAnalyzer::validateOwnershipRules(const TypeNode& type,
                                              const std::string& context,
                                              const SourceLocation& loc) {
    if (type.ownership == OwnershipKind::None) return;

    // S-OWN-001: ownership qualifier + &/* modifier are mutually exclusive
    if (type.modifier != TypeNode::None) {
        std::string modStr = (type.modifier == TypeNode::Ref) ? "&" : "*";
        diag_.error(loc,
                    context + ": ownership qualifier '" + ownershipKindName(type.ownership) +
                        "' is mutually exclusive with modifier '" + modStr +
                        "' — ownership implies indirection (S-OWN-001)");
    }

    // S-OWN-005: ownership qualifier on primitive types
    if (!type.nameParts.empty() && type.nameParts.size() == 1) {
        const auto& baseName = type.nameParts[0];
        TypeRegistry registry = TypeRegistry::createDefault();
        if (baseName == "void" || registry.isPrimitive(baseName)) {
            diag_.error(loc,
                        context + ": ownership qualifier '" + ownershipKindName(type.ownership) +
                            "' is meaningless on primitive type '" + baseName + "' (S-OWN-005)");
        }
    }
}

void SemanticAnalyzer::validateOwnershipGraph() {
    // S-OWN-004: detect shared → shared cycles among class member fields.
    // Build directed graph: class A --shared--> class B (for each shared field)
    // then DFS for cycles.

    // Map: class simple name → list of shared field target class names
    std::map<std::string, std::vector<std::string>> sharedGraph;
    // Map: class simple name → location (for error reporting)
    std::map<std::string, SourceLocation> classLocations;

    for (const auto& [qname, cls] : symbols_.classSymbols()) {
        classLocations[cls.simpleName] = cls.location;
        for (const auto& mv : cls.memberVars) {
            if (mv.type.ownership == OwnershipKind::Shared && !mv.type.nameParts.empty()) {
                // Target type is the last name part (handles qualified names)
                const auto& targetName = mv.type.nameParts.back();
                sharedGraph[cls.simpleName].push_back(targetName);
            }
        }
    }

    // DFS cycle detection
    for (const auto& entry : sharedGraph) {
        const std::string& startClass = entry.first;
        std::set<std::string> visited;
        std::vector<std::string> path;

        static constexpr int kMaxDfsDepth = 500;
        std::function<bool(const std::string&, int)> dfs = [&](const std::string& node, int depth) -> bool {
            if (depth > kMaxDfsDepth) {
                auto locIt = classLocations.find(startClass);
                SourceLocation loc = locIt != classLocations.end() ? locIt->second : SourceLocation{};
                diag_.error(loc,
                            "ownership graph analysis exceeded maximum depth (" + std::to_string(kMaxDfsDepth) +
                                ") starting from class '" + startClass + "'");
                return true;
            }
            if (visited.count(node)) {
                // Found a cycle — check if it includes startClass
                for (size_t i = 0; i < path.size(); ++i) {
                    if (path[i] == node) {
                        // Build cycle description
                        std::string cycle;
                        for (size_t j = i; j < path.size(); ++j) {
                            if (!cycle.empty()) cycle += " -> ";
                            cycle += path[j];
                        }
                        cycle += " -> " + node;

                        auto locIt = classLocations.find(startClass);
                        SourceLocation loc = locIt != classLocations.end() ? locIt->second : SourceLocation{};
                        diag_.error(loc,
                                    "shared ownership cycle detected: " + cycle +
                                        " — use 'weak' to break the cycle "
                                        "(S-OWN-004)");
                        return true;
                    }
                }
                return false;
            }
            visited.insert(node);
            path.push_back(node);

            auto it = sharedGraph.find(node);
            if (it != sharedGraph.end()) {
                for (const auto& target : it->second) {
                    if (dfs(target, depth + 1)) return true;
                }
            }

            path.pop_back();
            return false;
        };

        dfs(startClass, 0);
    }
}

} // namespace topo
