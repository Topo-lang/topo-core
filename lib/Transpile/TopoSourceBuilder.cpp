#include "topo/Transpile/TopoSourceBuilder.h"

#include "topo/Basic/TokenKinds.h"

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace topo::transpile {

namespace {

// --- AST expression-operator → TranspileModel operator mapping --------------

std::optional<BinaryOp> mapBinaryOp(TokenKind k) {
    switch (k) {
    case TokenKind::Plus: return BinaryOp::Add;
    case TokenKind::Minus: return BinaryOp::Sub;
    case TokenKind::Star: return BinaryOp::Mul;
    case TokenKind::Slash: return BinaryOp::Div;
    case TokenKind::Percent: return BinaryOp::Mod;
    case TokenKind::EqEq: return BinaryOp::Eq;
    case TokenKind::NotEq: return BinaryOp::NotEq;
    case TokenKind::LAngle: return BinaryOp::Less;
    case TokenKind::RAngle: return BinaryOp::Greater;
    case TokenKind::LessEq: return BinaryOp::LessEq;
    case TokenKind::GreaterEq: return BinaryOp::GreaterEq;
    case TokenKind::AmpAmp: return BinaryOp::And;
    case TokenKind::PipePipe: return BinaryOp::Or;
    case TokenKind::Amp: return BinaryOp::BitAnd;
    case TokenKind::Pipe: return BinaryOp::BitOr;
    case TokenKind::Caret: return BinaryOp::BitXor;
    case TokenKind::ShiftLeft: return BinaryOp::Shl;
    case TokenKind::ShiftRight: return BinaryOp::Shr;
    default: return std::nullopt;
    }
}

std::optional<UnaryOp> mapUnaryOp(TokenKind k) {
    switch (k) {
    case TokenKind::Minus: return UnaryOp::Negate;
    case TokenKind::Bang: return UnaryOp::Not;
    case TokenKind::Tilde: return UnaryOp::BitNot;
    // `&` and `*` appear as unary operators in the fn-block expression
    // grammar (address-of / deref). The TranspileModel has no language-
    // agnostic node for those; callers fall back to Unsupported.
    default: return std::nullopt;
    }
}

LiteralKind mapLiteralKind(TokenKind k, const std::string& value) {
    switch (k) {
    case TokenKind::StringLiteral: return LiteralKind::String;
    case TokenKind::KW_true:
    case TokenKind::KW_false: return LiteralKind::Boolean;
    case TokenKind::IntegerLiteral:
        // The lexer carries every numeric literal as IntegerLiteral; a `.`
        // in the spelling means the source wrote a floating-point literal.
        return value.find('.') != std::string::npos ? LiteralKind::Float
                                                    : LiteralKind::Integer;
    default: return LiteralKind::Integer;
    }
}

// ---------------------------------------------------------------------------
// Builder implementation state.
// ---------------------------------------------------------------------------

class BuilderImpl {
public:
    BuildResult run(const TopoFile& topoFile) {
        // Pass 1: collect every FnDecl and FnLogicBlock, qualified by the
        // enclosing namespace path.
        std::vector<std::string> nsPrefix;
        for (const auto& decl : topoFile.declarations) {
            walkTopLevel(*decl, nsPrefix);
        }

        // Pass 1b: index every collected function's declared return type by
        // both its qualified name and its bare simple name. Operation/pipeline
        // bodies call functions by simple name; threading the callee's
        // declared return type onto a binding's VarDecl gives the emitters a
        // concrete type to render (an untyped VarDecl is not valid in C++ /
        // Rust / Java).
        for (const auto& fd : fnDecls_) {
            returnTypeByName_[fd.qualifiedName] = fd.decl->returnType;
            std::string simple = fd.qualifiedName;
            auto pos = simple.rfind("::");
            if (pos != std::string::npos) simple = simple.substr(pos + 2);
            // A simple name may be ambiguous across namespaces; first-seen
            // wins. The naive orchestration only ever calls within one block's
            // own namespace, so this is sufficient for the supported scope.
            returnTypeByName_.emplace(simple, fd.decl->returnType);
        }

        // Pass 2: turn each collected FnDecl into a TranspileFunction.
        for (const auto& fd : fnDecls_) {
            buildFunction(fd.decl, fd.qualifiedName, fd.accessModifier);
            builtFromDecl_.insert(fd.qualifiedName);
        }

        // Pass 3: a `flow` desugars to an FnLogicBlock that has NO matching
        // FnDecl (the flow's name IS the pipeline identity). Emit a
        // TranspileFunction for every such orphan composite block.
        for (const auto& [qn, block] : logicBlocks_) {
            if (builtFromDecl_.count(qn)) continue; // already paired with an FnDecl
            buildOrphanComposite(qn, *block);
        }

        result_.success = result_.errors.empty();
        return std::move(result_);
    }

private:
    struct CollectedFn {
        const FnDecl* decl;
        std::string qualifiedName;
        std::string accessModifier; // "public"/"protected"/"private"/""
    };

    BuildResult result_;
    std::vector<CollectedFn> fnDecls_;
    // qualifiedName → logic block. A composite function has an entry here.
    std::unordered_map<std::string, const FnLogicBlock*> logicBlocks_;
    // Qualified names already emitted from an FnDecl in pass 2 — a logic
    // block whose name is NOT here is an orphan composite (a `flow`).
    std::set<std::string> builtFromDecl_;
    // Declared return type of each collected function, keyed by both its
    // qualified and simple name (pass 1b). Used to type a binding VarDecl.
    std::unordered_map<std::string, TypeNode> returnTypeByName_;

    // Look up the declared return type of a called function (by the name as
    // written in the operation/pipeline body). Returns an empty TypeNode when
    // the callee is unknown — the emitter then falls back to its own rules.
    TypeNode calleeReturnType(const std::string& name) const {
        auto it = returnTypeByName_.find(name);
        return it != returnTypeByName_.end() ? it->second : TypeNode{};
    }

    static std::string joinQualified(const std::vector<std::string>& parts,
                                     const std::string& name) {
        std::string out;
        for (const auto& p : parts) {
            out += p;
            out += "::";
        }
        out += name;
        return out;
    }

    // A `.topo` fn name may use dots ("telemetry.init"); the qualified name
    // uses "::" segment separators. Normalize a logic-block / fn-decl name to
    // a single comparable key by replacing dots with "::".
    static std::string normalizeDotted(const std::string& name) {
        std::string out;
        for (char c : name) out += (c == '.') ? std::string("::") : std::string(1, c);
        return out;
    }

    void walkTopLevel(const ASTNode& node, std::vector<std::string>& nsPrefix) {
        switch (node.kind) {
        case ASTKind::NamespaceDecl: {
            const auto& ns = static_cast<const NamespaceDecl&>(node);
            size_t pushed = ns.path.size();
            for (const auto& seg : ns.path) nsPrefix.push_back(seg);
            for (const auto& sec : ns.sections) {
                walkTopLevel(*sec, nsPrefix);
            }
            for (size_t i = 0; i < pushed; ++i) nsPrefix.pop_back();
            break;
        }
        case ASTKind::VisibilitySection: {
            const auto& vs = static_cast<const VisibilitySection&>(node);
            std::string access = visibilityToAccess(vs.visibility);
            for (const auto& m : vs.members) {
                walkMember(*m, nsPrefix, access);
            }
            break;
        }
        case ASTKind::FnDecl:
            collectFnDecl(static_cast<const FnDecl&>(node), nsPrefix, "");
            break;
        case ASTKind::FnLogicBlock:
            collectLogicBlock(static_cast<const FnLogicBlock&>(node), nsPrefix);
            break;
        default:
            // Imports, type aliases, type declarations, comptime-if, debug
            // blocks etc. are not function bodies — the transpile model only
            // carries functions+types and this milestone scope is functions.
            break;
        }
    }

    void walkMember(const ASTNode& node, std::vector<std::string>& nsPrefix,
                    const std::string& access) {
        switch (node.kind) {
        case ASTKind::FnDecl:
            collectFnDecl(static_cast<const FnDecl&>(node), nsPrefix, access);
            break;
        case ASTKind::FnLogicBlock:
            collectLogicBlock(static_cast<const FnLogicBlock&>(node), nsPrefix);
            break;
        case ASTKind::VisibilitySection: {
            // nested section (rare, but the AST permits it)
            const auto& vs = static_cast<const VisibilitySection&>(node);
            std::string inner = visibilityToAccess(vs.visibility);
            for (const auto& m : vs.members) walkMember(*m, nsPrefix, inner);
            break;
        }
        default:
            break;
        }
    }

    static std::string visibilityToAccess(Visibility v) {
        switch (v) {
        case Visibility::Public: return "public";
        case Visibility::Protected: return "protected";
        case Visibility::Private: return "private";
        case Visibility::Internal: return "private"; // closest target-language access
        case Visibility::Ignore: return "";
        }
        return "";
    }

    void collectFnDecl(const FnDecl& decl, const std::vector<std::string>& nsPrefix,
                       const std::string& access) {
        // Operators and ctors/dtors are not part of the `.topo`-source
        // transpile MVP — they have no leaf/composite body story here.
        if (decl.isOperator() || decl.isConstructor || decl.isDestructor) {
            return;
        }
        fnDecls_.push_back(
            {&decl, joinQualified(nsPrefix, normalizeDotted(decl.name)), access});
    }

    void collectLogicBlock(const FnLogicBlock& block,
                            const std::vector<std::string>& nsPrefix) {
        logicBlocks_[joinQualified(nsPrefix, normalizeDotted(block.name))] = &block;
    }

    void buildFunction(const FnDecl* decl, const std::string& qualifiedName,
                        const std::string& access) {
        TranspileFunction fn;
        fn.qualifiedName = qualifiedName;
        fn.returnType = decl->returnType;
        fn.params = decl->params;
        fn.accessModifier = access;
        fn.templateParams = decl->templateParams;
        fn.fidelity = Fidelity::Source;

        auto lbIt = logicBlocks_.find(qualifiedName);
        if (lbIt != logicBlocks_.end()) {
            // Composite function: generate the body from the logic block.
            buildCompositeBody(*lbIt->second, fn);
        } else {
            // Leaf function: no logic block. Mark the body unresolved so the
            // M3 adapter resolver can fill it (or degrade it).
            fn.unsupported.push_back(kLeafUnresolvedMarker);
        }

        result_.module.functions.push_back(std::move(fn));
    }

    // A logic block with no matching FnDecl — produced by a `flow`
    // declaration. The flow's name is the function identity; its signature
    // is synthesized: a flow is pipeline-mode, so the return type comes from
    // the terminal edge's type (`void` or a user type), and it takes no
    // explicit parameters at this naive-orchestration level.
    void buildOrphanComposite(const std::string& qualifiedName,
                               const FnLogicBlock& block) {
        TranspileFunction fn;
        fn.qualifiedName = qualifiedName;
        fn.accessModifier = "public"; // a flow is an external entry point
        fn.fidelity = Fidelity::Source;

        // Derive the return type from the pipeline's terminal edge.
        for (const auto& edge : block.pipelineEdges) {
            if (edge.isTerminal && !edge.terminalType.empty()) {
                fn.returnType.nameParts = {edge.terminalType};
                break;
            }
        }

        buildCompositeBody(block, fn);
        result_.module.functions.push_back(std::move(fn));
    }

    // --- Composite body generation ------------------------------------------

    void buildCompositeBody(const FnLogicBlock& block, TranspileFunction& fn) {
        if (block.isPipeline()) {
            buildPipelineBody(block, fn);
        } else {
            buildOperationBody(block, fn);
        }
    }

    // Operation-mode: each operation lowers to a statement in declaration
    // order. `f(args) -> x` → `VarDecl x = f(args)`; `f(args) -> (a, b)` →
    // a call ExprStmt (multi-binding has no single-target model node — the
    // call still runs, binding is recorded as unsupported); `f(args)` →
    // ExprStmt; `x = expr` → Assign / VarDecl depending on first-seen.
    void buildOperationBody(const FnLogicBlock& block, TranspileFunction& fn) {
        std::vector<std::string> boundNames; // names already declared in-body

        auto isBound = [&](const std::string& n) {
            for (const auto& b : boundNames)
                if (b == n) return true;
            return false;
        };

        for (const auto& opNode : block.operations) {
            if (opNode->kind != ASTKind::OperationDecl) continue;
            const auto& op = static_cast<const OperationDecl&>(*opNode);

            if (op.isAssignment()) {
                // `x = <expr>` operation.
                auto rhs = lowerExpr(op.expr.get(), fn);
                if (isBound(op.varName)) {
                    auto assign = std::make_unique<AssignStmt>();
                    auto target = std::make_unique<VarRefExpr>();
                    target->name = op.varName;
                    assign->target = std::move(target);
                    assign->value = std::move(rhs);
                    fn.body.push_back(std::move(assign));
                } else {
                    auto vd = std::make_unique<VarDeclStmt>();
                    vd->name = op.varName;
                    // No declared type at the .topo operation site. If the
                    // RHS is a scalar literal, infer its type so every
                    // emitter renders a concretely-typed local instead of an
                    // untyped (non-compilable) declaration. `.topo` has
                    // integer / string / bool literal tokens only — there is
                    // no float-literal token, so no f64 case here. A
                    // non-literal RHS keeps an empty type.
                    const ASTNode* rhsNode = op.expr.get();
                    if (rhsNode && rhsNode->kind == ASTKind::LiteralExpr) {
                        const auto& lit =
                            static_cast<const ::topo::LiteralExpr&>(*rhsNode);
                        switch (lit.literalKind) {
                        case TokenKind::IntegerLiteral:
                            vd->type.nameParts = {"i64"};
                            break;
                        case TokenKind::StringLiteral:
                            vd->type.nameParts = {"string"};
                            break;
                        case TokenKind::KW_true:
                        case TokenKind::KW_false:
                            vd->type.nameParts = {"bool"};
                            break;
                        default:
                            break;
                        }
                    }
                    vd->init = std::move(rhs);
                    fn.body.push_back(std::move(vd));
                    boundNames.push_back(op.varName);
                }
                continue;
            }

            // Function-call operation.
            auto call = std::make_unique<CallExpr>();
            call->callee = op.funcName;
            // The fn-block call grammar does not carry argument expressions
            // on OperationDecl (StageAnalysis tracks callee names only); the
            // naive orchestration emits an argument-free call. Argument
            // threading is a backend concern (the roadmap pins this path as
            // *naive orchestration*).

            if (op.returnBinding && op.returnBinding->isSingleValue) {
                // `f() -> x` → declare x = f(). The binding's declared type is
                // the callee's declared return type, so the emitters render a
                // concretely-typed local instead of an untyped declaration.
                auto vd = std::make_unique<VarDeclStmt>();
                vd->name = op.returnBinding->singleName;
                vd->type = calleeReturnType(op.funcName);
                vd->init = std::move(call);
                boundNames.push_back(op.returnBinding->singleName);
                fn.body.push_back(std::move(vd));
            } else if (op.returnBinding && !op.returnBinding->isSingleValue) {
                // Multi-value destructuring `f() -> (a, b)` has no
                // language-agnostic single statement; emit the call and
                // record the unmodeled binding traceably.
                auto es = std::make_unique<ExprStmt>();
                es->expr = std::move(call);
                fn.body.push_back(std::move(es));
                fn.unsupported.push_back(
                    "multi-value return binding on operation '" + op.funcName +
                    "' not represented in the naive orchestration body");
                if (fn.fidelity == Fidelity::Source) fn.fidelity = Fidelity::Inferred;
            } else {
                auto es = std::make_unique<ExprStmt>();
                es->expr = std::move(call);
                fn.body.push_back(std::move(es));
            }
        }
    }

    // Pipeline-mode: lower the DAG to a naive linear orchestration following
    // a topological order of the edges. Each non-terminal source node
    // becomes a `VarDecl <node> = <node>()`; the terminal edge becomes a
    // `return <lastNode>`.
    void buildPipelineBody(const FnLogicBlock& block, TranspileFunction& fn) {
        // Collect node order: declaration order of edge endpoints, terminal
        // last. This is a naive orchestration, not a real topo sort — the
        // roadmap explicitly scopes this path to naive DAG orchestration.
        std::vector<std::string> order;
        std::string terminalNode;
        auto seen = [&](const std::string& n) {
            for (const auto& o : order)
                if (o == n) return true;
            return false;
        };
        for (const auto& edge : block.pipelineEdges) {
            if (!edge.source.empty() && !seen(edge.source)) order.push_back(edge.source);
            if (!edge.isTerminal && !edge.target.empty() && !seen(edge.target)) {
                order.push_back(edge.target);
            }
            if (edge.isTerminal) terminalNode = edge.source;
        }

        for (const auto& node : order) {
            auto call = std::make_unique<CallExpr>();
            call->callee = node;
            auto vd = std::make_unique<VarDeclStmt>();
            vd->name = node + "_out";
            // Type the pipeline-node binding from the node function's declared
            // return type (same reason as the operation-mode bindings).
            vd->type = calleeReturnType(node);
            vd->init = std::move(call);
            fn.body.push_back(std::move(vd));
        }

        if (!terminalNode.empty()) {
            auto ret = std::make_unique<ReturnStmt>();
            auto ref = std::make_unique<VarRefExpr>();
            ref->name = terminalNode + "_out";
            ret->value = std::move(ref);
            fn.body.push_back(std::move(ret));
        }
    }

    // --- Expression lowering (AST ExprPtr → TranspileModel ExprPtr) ----------

    ExprPtr lowerExpr(const ASTNode* node, TranspileFunction& fn) {
        if (!node) return makeUnsupported("empty expression", fn);

        switch (node->kind) {
        case ASTKind::BinaryExpr: {
            const auto& be = static_cast<const BinaryExpr&>(*node);
            auto op = mapBinaryOp(be.op);
            if (!op) {
                return makeUnsupported("binary operator not representable", fn);
            }
            auto e = std::make_unique<BinaryOpExpr>();
            e->op = *op;
            e->lhs = lowerExpr(be.lhs.get(), fn);
            e->rhs = lowerExpr(be.rhs.get(), fn);
            return e;
        }
        case ASTKind::UnaryExpr: {
            const auto& ue = static_cast<const UnaryExpr&>(*node);
            auto op = mapUnaryOp(ue.op);
            if (!op) {
                return makeUnsupported("unary operator '" +
                                           std::string(tokenKindName(ue.op)) +
                                           "' not representable",
                                       fn);
            }
            auto e = std::make_unique<UnaryOpExpr>();
            e->op = *op;
            e->operand = lowerExpr(ue.operand.get(), fn);
            return e;
        }
        case ASTKind::LiteralExpr: {
            const auto& le = static_cast<const ::topo::LiteralExpr&>(*node);
            auto e = std::make_unique<transpile::LiteralExpr>();
            e->litKind = mapLiteralKind(le.literalKind, le.value);
            e->value = le.value;
            return e;
        }
        case ASTKind::IdentifierExpr: {
            const auto& ie = static_cast<const IdentifierExpr&>(*node);
            auto e = std::make_unique<VarRefExpr>();
            e->name = ie.name;
            return e;
        }
        case ASTKind::CallExpr: {
            const auto& ce = static_cast<const ::topo::CallExpr&>(*node);
            auto e = std::make_unique<transpile::CallExpr>();
            e->callee = ce.funcName;
            for (const auto& arg : ce.args) {
                e->args.push_back(lowerExpr(arg.get(), fn));
            }
            return e;
        }
        default:
            return makeUnsupported("expression form not representable", fn);
        }
    }

    ExprPtr makeUnsupported(const std::string& desc, TranspileFunction& fn) {
        auto e = std::make_unique<UnsupportedExpr>();
        e->description = desc;
        fn.unsupported.push_back(desc);
        if (fn.fidelity == Fidelity::Source) fn.fidelity = Fidelity::Inferred;
        return e;
    }
};

} // namespace

bool isUnresolvedLeaf(const TranspileFunction& fn) {
    for (const auto& u : fn.unsupported) {
        if (u == kLeafUnresolvedMarker) return true;
    }
    return false;
}

BuildResult TopoSourceBuilder::build(const TopoFile& topoFile) {
    BuilderImpl impl;
    return impl.run(topoFile);
}

} // namespace topo::transpile
