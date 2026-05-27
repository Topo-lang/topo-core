#ifndef TOPO_AST_ASTNODE_H
#define TOPO_AST_ASTNODE_H

#include "topo/Basic/SourceLocation.h"
#include "topo/Basic/TokenKinds.h"
#include "topo/Stdlib/Types.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace topo {

// Forward declarations
struct ASTNode;
using ASTNodePtr = std::unique_ptr<ASTNode>;

// --- Ownership qualifier ---

enum class OwnershipKind { None, Owned, Shared, Weak };

inline const char* ownershipKindName(OwnershipKind kind) {
    switch (kind) {
    case OwnershipKind::None: return "none";
    case OwnershipKind::Owned: return "owned";
    case OwnershipKind::Shared: return "shared";
    case OwnershipKind::Weak: return "weak";
    }
    return "unknown";
}

// --- Type representation ---

struct TypeNode {
    bool isConst = false;
    OwnershipKind ownership = OwnershipKind::None;
    std::vector<std::string> nameParts; // e.g. {"std", "cpp17", "intptr_t"}
    enum Modifier { None, Ref, Ptr } modifier = None;
    SourceLocation location; // source position of the type token

    std::vector<TypeNode> templateArgs; // Vector<T> -> templateArgs=[{T}]
    bool isTemplateParam = false;       // type itself is a template parameter name
    bool isVariadic = false;            // typename... Ts
    std::optional<int> nonTypeValue;    // non-type template argument N=4

    // Stdlib bridging types.
    //
    // When the parser sees a lowercase stdlib keyword (bool / i64 / f64 /
    // string / optional / slice) it sets `stdlibId` to the matching
    // TypeId and writes the canonical keyword into nameParts[0]. Both
    // representations stay in sync so existing TypeNode consumers (string
    // matching on `nameParts`) keep working unchanged, while Emitters and
    // catalogs can dispatch directly on `stdlibId` without re-parsing the
    // name. `stdlibId == TypeId::None` means "not a stdlib type".
    stdlib::TypeId stdlibId = stdlib::TypeId::None;
    bool isStdlib() const { return stdlibId != stdlib::TypeId::None; }

    // Named fields of a `record<name1: T1, ...>` or
    // `union<tag: TagT, v1: T1, ...>` composite. Populated when `stdlibId`
    // is `TypeId::Record` or `TypeId::Union`; empty for every other type.
    // Fields keep declaration order — the cross-language byte layout depends
    // on it (and for union the first field is the discriminant tag). The
    // field type is itself a TypeNode so nested
    // stdlib composites (`record<inner: optional<i64>>`) recurse naturally;
    // it is held inside a std::vector for the same incomplete-type
    // indirection that `templateArgs` relies on (a by-value TypeNode member
    // inside a struct nested in TypeNode would be an incomplete type).
    struct RecordField {
        std::string name;
        SourceLocation location;
        std::vector<TypeNode> typeBox; // exactly one element
        const TypeNode& type() const { return typeBox.front(); }
        TypeNode& type() { return typeBox.front(); }
    };
    std::vector<RecordField> recordFields;

    // Associated-type bindings on a trait-bound type (Rust
    // `Iterator<Item = u8>` or `Container<Item = T, Key = K>`). Only ever
    // populated when this TypeNode is used as a *trait bound* in
    // TemplateParamDecl::constraintType / extraBounds — the binding's left
    // side is the associated-type name (e.g. "Item") and the right side is
    // a recursive TypeNode. Reuses RecordField for the same incomplete-type
    // indirection reason that `recordFields` and `templateArgs` use a vector
    // box. Empty for every type that is not a parameterised trait bound,
    // so the wire stays byte-identical to pre-assoc-binding payloads.
    // Target hosts other than Rust have no equivalent concept and drop the
    // bindings with a comment (see per-emitter comment); Rust extracts and
    // emits them via the renderPath path in RustEmitter.
    std::vector<RecordField> assocBindings;

    // Higher-Ranked Trait Bound (HRTB) lifetimes (Rust
    // `for<'a, 'b> Fn(&'a T) -> &'b T`). Only ever populated when this
    // TypeNode is used as a *trait bound* in TemplateParamDecl::constraintType
    // / extraBounds and the bound carries `for<...>`-introduced lifetimes.
    // Names stored WITHOUT the leading apostrophe (`["a", "b"]`); the `'` is
    // added at emit time. Empty for every type that is not a HRTB trait
    // bound, so the wire stays byte-identical to pre-HRTB payloads.
    // Target hosts other than Rust silently drop the prefix (no comment —
    // HRTBs have no informational value for C++/Java/Python/TS); Rust
    // extracts and emits them via the renderPath path in RustEmitter as the
    // `for<...>` prefix to the trait path.
    std::vector<std::string> hrtbLifetimes;

    std::string toString() const {
        std::string result;
        if (isConst) result += "const ";
        if (ownership != OwnershipKind::None) {
            result += std::string(ownershipKindName(ownership)) + " ";
        }
        for (size_t i = 0; i < nameParts.size(); ++i) {
            if (i > 0) result += "::";
            result += nameParts[i];
        }
        if (!recordFields.empty()) {
            result += "<";
            for (size_t i = 0; i < recordFields.size(); ++i) {
                if (i > 0) result += ", ";
                result += recordFields[i].name + ": " + recordFields[i].type().toString();
            }
            result += ">";
        } else if (!templateArgs.empty() || !assocBindings.empty()) {
            result += "<";
            bool first = true;
            for (size_t i = 0; i < templateArgs.size(); ++i) {
                if (!first) result += ", ";
                result += templateArgs[i].toString();
                first = false;
            }
            for (size_t i = 0; i < assocBindings.size(); ++i) {
                if (!first) result += ", ";
                result += assocBindings[i].name + " = " + assocBindings[i].type().toString();
                first = false;
            }
            result += ">";
        }
        if (modifier == Ref)
            result += "&";
        else if (modifier == Ptr)
            result += "*";
        return result;
    }
};

// --- Parameter representation ---

struct Parameter {
    TypeNode type;
    std::string name;

    std::string toString() const { return type.toString() + " " + name; }
};

// --- Multi-return parameter ---

struct ReturnParam {
    TypeNode type;
    std::string name;
    SourceLocation loc;

    std::string toString() const { return type.toString() + " " + name; }
};

// --- Return binding (in operations) ---

struct ReturnBinding {
    struct Target {
        std::string name; // "_" = discard
        bool isDiscard = false;
    };
    bool isSingleValue = false;  // f() -> x
    std::string singleName;      // populated when isSingleValue is true
    std::vector<Target> targets; // populated when isSingleValue is false (destructuring)
    SourceLocation loc;
};

// --- Pipeline edge ---

struct PipelineEdge {
    std::string source;
    std::string target;       // empty = terminal edge
    std::string terminalType; // type name on a terminal edge
    bool isTerminal = false;
    SourceLocation loc;
};

// --- Template parameter declaration (shared by class and function templates) ---

struct TemplateParamDecl {
    enum Kind { TypeParam, NonTypeParam, TemplateTemplateParam, LifetimeParam };
    Kind kind = TypeParam;
    std::string name;        // T, N, Container; for LifetimeParam: lifetime
                             // name WITHOUT leading `'` (e.g. "a"). The
                             // apostrophe is added at emit time so the wire
                             // stays apostrophe-free.
    TypeNode constraintType; // non-type param type; type param's first bound.
                             // For LifetimeParam: outlives target encoded as
                             // a TypeNode with nameParts={"'b"} (the
                             // apostrophe IS kept on outlives targets so a
                             // type-param lifetime-bound entry and a
                             // lifetime-on-lifetime outlives entry share the
                             // same `'<name>` spelling on the wire).
    // Additional bounds for type params (Rust `T: A + B`, Java/TS
    // intersection `T extends A & B`). For TypeParam the full bound list is
    // [constraintType, ...extraBounds]; for NonTypeParam this stays empty
    // (constraintType carries the value type, not a bound list). For
    // TypeParam carrying a Rust lifetime bound (`T: 'a`), the lifetime
    // appears in this list as a TypeNode whose nameParts[0] starts with
    // `'`. Non-Rust emitters drop lifetime bound entries; the trait-bound
    // entries (no leading `'`) are unaffected.
    std::vector<TypeNode> extraBounds;
    bool isVariadic = false;
    std::vector<TemplateParamDecl> innerParams; // template template params
    std::optional<TypeNode> defaultType;
    // Default literal expression for NonTypeParam (C++ `template <int N = 10>`,
    // Rust `<const N: usize = 16>`). Independent of `defaultType` (which is
    // TypeParam-only). Stored as the source literal spelling: integer
    // literals (`"10"`, `"0x1F"`), bool literals (`"true"`/`"false"`), or
    // enum literal-spelling. Complex constant expressions (`N+1`,
    // `sizeof(T)`, template instantiations) are conservatively dropped at
    // extraction time rather than being captured partially. omit-when-empty
    // on the wire so a NonTypeParam without a default stays byte-identical
    // to pre-feature output.
    std::optional<std::string> defaultValue;
    SourceLocation location;
};

// --- Modifier system ---

struct ModifierData {
    enum Kind {
        // Compiler layer
        Stage, // stage<N> — args[0] = stage number string
        Const,
        Static,
        Explicit,  // explicit (constructor)
        Ownership, // args[0] = "owned"/"shared"/"weak"

        // Standard library layer
        Constraint,  // marks type as constraint
        Adapt,       // adapt block — args[0] = constraintName
        Lifetime,    // lifetime declaration
        Priority,    // args[0] = level name
        Comptime,    // comptime modifier
        External,    // external modifier (boundary function)
        TypeFn,      // typefn modifier
        Instantiate, // instantiate directive

        // Logic-unit / logic-flow surface markers. A `handler` desugars to
        // an FnDecl carrying Handler; a `flow` desugars to a pipeline
        // FnLogicBlock carrying Flow. The marker is inert to every checker
        // (they read the desugared FnDecl/FnLogicBlock) and exists so the
        // declaration round-trips: re-emission reproduces the original
        // surface keyword instead of `fn`.
        Handler,
        Flow,
    };
    Kind kind;
    SourceLocation location;
    std::vector<std::string> args;
};

// --- AST node kinds ---

enum class ASTKind {
    // --- New unified kinds ---
    TopoFile,
    Import,   // was FileImport
    DataDecl, // replaces TypeAliasDecl, StdImportDecl, MemberVarDecl,
              //          LifetimeDecl, InstantiateDecl
    NamespaceDecl,
    VisibilitySection,
    FnDecl,       // replaces FunctionDecl, OperatorDecl, ComptimeFn,
                  //          ConstructorDecl, DestructorDecl
    FnLogicBlock, // was FunctionLogicBlock
    OperationDecl,
    TypeDecl, // replaces ClassDecl, ConstraintDecl, AdaptDecl, TypeFn
    IfDecl,   // was ComptimeIf
    DebugDecl, // `debug T { view ... summary ... inactive_region ... render ... }`
    // Expressions
    BinaryExpr,
    UnaryExpr,
    LiteralExpr,
    IdentifierExpr,
    CallExpr,

    // --- Deprecated aliases (same underlying value) ---
    FileImport = Import,
    TypeAliasDecl = DataDecl,
    StdImportDecl = DataDecl,
    MemberVarDecl = DataDecl,
    LifetimeDecl = DataDecl,
    InstantiateDecl = DataDecl,
    FunctionDecl = FnDecl,
    OperatorDecl = FnDecl,
    ConstructorDecl = FnDecl,
    DestructorDecl = FnDecl,
    ComptimeFn = FnDecl,
    FunctionLogicBlock = FnLogicBlock,
    ClassDecl = TypeDecl,
    ConstraintDecl = TypeDecl,
    AdaptDecl = TypeDecl,
    // TypeFn conflicts with ModifierData::TypeFn, use TypeDecl directly
    ComptimeIf = IfDecl,
};

// --- Base node ---

struct ASTNode {
    ASTKind kind;
    SourceLocation location;
    std::vector<ModifierData> modifiers;

    ASTNode(ASTKind k, SourceLocation loc) : kind(k), location(loc) {}
    virtual ~ASTNode() = default;

    bool hasModifier(ModifierData::Kind k) const {
        for (const auto& m : modifiers)
            if (m.kind == k) return true;
        return false;
    }

    const ModifierData* findModifier(ModifierData::Kind k) const {
        for (const auto& m : modifiers)
            if (m.kind == k) return &m;
        return nullptr;
    }
};

// --- TopoFile: root node ---

struct TopoFile : ASTNode {
    std::vector<ASTNodePtr> declarations;

    explicit TopoFile(SourceLocation loc) : ASTNode(ASTKind::TopoFile, loc) {}
};

// --- Import: import other; / import core/types { Foo }; ---

struct Import : ASTNode {
    std::string path;                         // e.g. "other" or "core/types"
    std::vector<std::string> selectedSymbols; // empty = import all

    Import(SourceLocation loc, std::string p, std::vector<std::string> syms = {})
        : ASTNode(ASTKind::Import, loc), path(std::move(p)), selectedSymbols(std::move(syms)) {}
};

// Deprecated alias
using FileImport = Import;

// --- DataDecl: unified data binding ---
// Replaces: TypeAliasDecl, StdImportDecl, MemberVarDecl, LifetimeDecl, InstantiateDecl

struct DataDecl : ASTNode {
    std::string name; // alias name, member var name, lifetime name
    TypeNode type;    // target type (alias target, member var type, instantiate type)

    // StdImport: non-empty when this is a std::import
    std::string importPath;

    // Lifetime fields (when hasModifier(Lifetime))
    std::string startFunc;
    std::string endFunc;
    bool isOpenEnded = false;
    bool isSingleFunc = false;

    // MemberVar: isStatic determined by Static modifier

    DataDecl(SourceLocation loc) : ASTNode(ASTKind::DataDecl, loc) {}

    // Convenience queries
    bool isStdImport() const { return !importPath.empty(); }
    bool isLifetime() const { return hasModifier(ModifierData::Kind::Lifetime); }
    bool isInstantiate() const { return hasModifier(ModifierData::Kind::Instantiate); }
    bool isStatic() const { return hasModifier(ModifierData::Kind::Static); }
    bool isTypeAlias() const { return !name.empty() && importPath.empty() && startFunc.empty() && !isInstantiate(); }
    bool isMemberVar() const {
        return !name.empty() && !importPath.empty() == false && startFunc.empty() && !isInstantiate() && !isTypeAlias();
    }
};

// Deprecated aliases — these types are now all DataDecl
using TypeAliasDecl = DataDecl;
using StdImportDecl = DataDecl;
using MemberVarDecl = DataDecl;

// --- NamespaceDecl: namespace a::b { ... } (unchanged) ---

struct NamespaceDecl : ASTNode {
    std::vector<std::string> path; // {"a", "b"}
    std::vector<ASTNodePtr> sections;
    bool isInternal = false;

    NamespaceDecl(SourceLocation loc, std::vector<std::string> p)
        : ASTNode(ASTKind::NamespaceDecl, loc), path(std::move(p)) {}

    std::string pathString() const {
        std::string result;
        for (size_t i = 0; i < path.size(); ++i) {
            if (i > 0) result += "::";
            result += path[i];
        }
        return result;
    }
};

// --- VisibilitySection: public: / protected: / private: + members ---

enum class Visibility { Public, Protected, Private, Internal, Ignore };

inline const char* visibilityName(Visibility v) {
    switch (v) {
    case Visibility::Public: return "public";
    case Visibility::Protected: return "protected";
    case Visibility::Private: return "private";
    case Visibility::Internal: return "internal";
    case Visibility::Ignore: return "ignore";
    }
    return "unknown";
}

// --- Priority level for scheduling ---

enum class PriorityLevel { Critical, High, Normal, Low, Background };

// --- Data-aware optimization hints ---

struct CardinalityHint {
    int64_t min = -1; // -1 = unspecified
    int64_t max = -1;
};

enum class AccessPattern { None, Streaming, Random, Tiled, GatherScatter };

inline const char* priorityLevelName(PriorityLevel p) {
    switch (p) {
    case PriorityLevel::Critical: return "critical";
    case PriorityLevel::High: return "high";
    case PriorityLevel::Normal: return "normal";
    case PriorityLevel::Low: return "low";
    case PriorityLevel::Background: return "background";
    }
    return "unknown";
}

struct VisibilitySection : ASTNode {
    Visibility visibility;
    std::vector<ASTNodePtr> members;

    VisibilitySection(SourceLocation loc, Visibility v) : ASTNode(ASTKind::VisibilitySection, loc), visibility(v) {}
};

// --- Expression nodes ---

using ExprPtr = std::unique_ptr<ASTNode>;

struct BinaryExpr : ASTNode {
    TokenKind op;
    ExprPtr lhs;
    ExprPtr rhs;

    BinaryExpr(SourceLocation loc, TokenKind op, ExprPtr lhs, ExprPtr rhs)
        : ASTNode(ASTKind::BinaryExpr, loc), op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

struct UnaryExpr : ASTNode {
    TokenKind op;
    ExprPtr operand;

    UnaryExpr(SourceLocation loc, TokenKind op, ExprPtr operand)
        : ASTNode(ASTKind::UnaryExpr, loc), op(op), operand(std::move(operand)) {}
};

struct LiteralExpr : ASTNode {
    std::string value;
    TokenKind literalKind; // IntegerLiteral, KW_true, KW_false, StringLiteral

    LiteralExpr(SourceLocation loc, std::string val, TokenKind kind)
        : ASTNode(ASTKind::LiteralExpr, loc), value(std::move(val)), literalKind(kind) {}
};

struct IdentifierExpr : ASTNode {
    std::string name; // may be qualified (a::b)

    IdentifierExpr(SourceLocation loc, std::string name)
        : ASTNode(ASTKind::IdentifierExpr, loc), name(std::move(name)) {}
};

struct CallExpr : ASTNode {
    std::string funcName; // may be dotted
    std::vector<ExprPtr> args;

    CallExpr(SourceLocation loc, std::string name) : ASTNode(ASTKind::CallExpr, loc), funcName(std::move(name)) {}
};

// --- Overloadable operators ---

enum class OverloadableOp {
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    EqEq,
    NotEq,
    Less,
    Greater,
    LessEq,
    GreaterEq,
    Amp,
    Pipe,
    Caret,
    Tilde,
    ShiftLeft,
    ShiftRight,
    AmpAmp,
    PipePipe,
    Bang,
    Subscript,
    Call,
    FatArrow
};

inline const char* overloadableOpName(OverloadableOp op) {
    switch (op) {
    case OverloadableOp::Plus: return "+";
    case OverloadableOp::Minus: return "-";
    case OverloadableOp::Star: return "*";
    case OverloadableOp::Slash: return "/";
    case OverloadableOp::Percent: return "%";
    case OverloadableOp::EqEq: return "==";
    case OverloadableOp::NotEq: return "!=";
    case OverloadableOp::Less: return "<";
    case OverloadableOp::Greater: return ">";
    case OverloadableOp::LessEq: return "<=";
    case OverloadableOp::GreaterEq: return ">=";
    case OverloadableOp::Amp: return "&";
    case OverloadableOp::Pipe: return "|";
    case OverloadableOp::Caret: return "^";
    case OverloadableOp::Tilde: return "~";
    case OverloadableOp::ShiftLeft: return "<<";
    case OverloadableOp::ShiftRight: return ">>";
    case OverloadableOp::AmpAmp: return "&&";
    case OverloadableOp::PipePipe: return "||";
    case OverloadableOp::Bang: return "!";
    case OverloadableOp::Subscript: return "[]";
    case OverloadableOp::Call: return "()";
    case OverloadableOp::FatArrow: return "=>";
    }
    return "?";
}

// --- FnDecl: unified function declaration ---
// Replaces: FunctionDecl, OperatorDecl, ComptimeFn, ConstructorDecl, DestructorDecl

struct FnDecl : ASTNode {
    TypeNode returnType;
    std::string name; // may include dots: "telemetry.init"
    std::vector<Parameter> params;
    bool isConst = false;
    bool isStatic = false;
    bool isRustStyle = false; // trailing return type
    bool isMultiReturn = false;
    std::vector<ReturnParam> returnParams;
    // Selective-return clause: `-> (...) with returns(a, _)`.
    // If non-empty, callers may only observe the listed names — unnamed
    // positions are marked as `_` (wildcards / may-be-elided).  Empty
    // means "no clause present; every return field is observable".
    // A clause written with all `_` (e.g., `with returns(_, _)`) yields
    // an empty `declaredUsedReturns` but sets `hasUsedReturnsClause=true`.
    std::vector<std::string> declaredUsedReturns;
    bool hasUsedReturnsClause = false;
    std::vector<TemplateParamDecl> templateParams;
    std::optional<std::string> bindingTarget;          // e.g. "std::sort"
    PriorityLevel priority = PriorityLevel::Normal;    // scheduling priority
    std::optional<CardinalityHint> cardinality;        // data-aware: scale range
    AccessPattern accessPattern = AccessPattern::None; // data-aware: access mode
    int tiledSize = 0;                                 // for access(tiled, N)

    // Constructor/Destructor fields
    bool isConstructor = false;
    bool isDestructor = false;
    bool isExplicit = false; // for explicit constructors
    std::string className;   // owning class name (for ctor/dtor)

    // Operator overload
    std::optional<OverloadableOp> operatorOp;

    // Comptime function body (non-null = comptime fn)
    ExprPtr comptimeBody;

    FnDecl(SourceLocation loc) : ASTNode(ASTKind::FnDecl, loc) {}

    bool isOperator() const { return operatorOp.has_value(); }
    bool isComptime() const { return comptimeBody != nullptr; }

    std::string signature() const {
        std::string result;
        if (isConstructor) {
            if (isExplicit) result += "explicit ";
            result += className + "(";
        } else if (isDestructor) {
            result += "~" + className + "(";
        } else if (isOperator()) {
            result += "fn operator";
            result += overloadableOpName(*operatorOp);
            result += "(";
        } else if (isRustStyle) {
            result += name + "(";
        } else {
            result += returnType.toString() + " " + name + "(";
        }
        for (size_t i = 0; i < params.size(); ++i) {
            if (i > 0) result += ", ";
            result += params[i].toString();
        }
        result += ")";
        if (isConst) result += " const";
        if (isOperator()) {
            result += " -> " + returnType.toString();
        } else if (isRustStyle && !isConstructor && !isDestructor) {
            if (isMultiReturn) {
                result += " -> (";
                for (size_t i = 0; i < returnParams.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += returnParams[i].toString();
                }
                result += ")";
            } else {
                result += " -> " + returnType.toString();
            }
        }
        return result;
    }
};

// Deprecated aliases
using FunctionDecl = FnDecl;
using OperatorDeclNode = FnDecl;
using ConstructorDecl = FnDecl;
using DestructorDecl = FnDecl;
using ComptimeFn = FnDecl;

// --- FnLogicBlock: fn funcName { ... } (was FunctionLogicBlock) ---

struct FnLogicBlock : ASTNode {
    std::string name; // may include dots
    std::vector<ASTNodePtr> operations;
    std::vector<PipelineEdge> pipelineEdges;
    std::vector<TemplateParamDecl> templateParams;

    FnLogicBlock(SourceLocation loc, std::string n) : ASTNode(ASTKind::FnLogicBlock, loc), name(std::move(n)) {}

    bool isPipeline() const { return !pipelineEdges.empty(); }
};

// Deprecated alias
using FunctionLogicBlock = FnLogicBlock;

// --- OperationDecl: [stage<N>] funcCall() | varName = expr; (unchanged) ---

struct OperationDecl : ASTNode {
    int stage = -1;       // -1 means no explicit stage
    std::string funcName; // function call (existing path)
    std::string varName;  // assignment target (new)
    ExprPtr expr;         // assignment RHS (new)
    std::optional<ReturnBinding> returnBinding;

    OperationDecl(SourceLocation loc) : ASTNode(ASTKind::OperationDecl, loc) {}

    bool isAssignment() const { return !varName.empty(); }

    std::string description() const {
        std::string result;
        if (stage >= 0) {
            result += "stage<" + std::to_string(stage) + "> ";
        }
        if (isAssignment()) {
            result += varName + " = <expr>";
        } else {
            result += "'" + funcName + "()'";
            if (returnBinding) {
                result += " -> ";
                if (returnBinding->isSingleValue) {
                    result += returnBinding->singleName;
                } else {
                    result += "(";
                    for (size_t i = 0; i < returnBinding->targets.size(); ++i) {
                        if (i > 0) result += ", ";
                        result += returnBinding->targets[i].name;
                    }
                    result += ")";
                }
            }
        }
        return result;
    }
};

// --- Constraint member (used in TypeDecl with Constraint modifier) ---

struct ConstraintMember {
    TypeNode type;                 // return type or member type
    std::string name;              // function/member name
    std::vector<Parameter> params; // empty for member variables
    bool isFunction = true;
    SourceLocation location;
};

// --- Adapt mapping (used in TypeDecl with Adapt modifier) ---

struct AdaptMapping {
    std::string memberName; // constraint member being adapted
    std::string targetName; // actual function/member name in the type
    SourceLocation location;
};

// --- TypeMatch arm (used in TypeDecl with TypeFn modifier) ---

struct TypeMatchArm {
    TypeNode pattern; // input type pattern (empty nameParts = "_" wildcard)
    TypeNode result;  // output type
    SourceLocation location;
};

// --- TypeDecl: unified type declaration ---
// Replaces: ClassDecl, ConstraintDecl, AdaptDecl, TypeFn

struct TypeDecl : ASTNode {
    std::string name;
    std::vector<TemplateParamDecl> templateParams;

    // Class-like fields
    std::optional<TypeNode> baseClass; // single inheritance
    std::vector<ASTNodePtr> sections;  // VisibilitySection nodes
    bool isSpecialization = false;
    bool isPartialSpecialization = false;
    std::vector<TypeNode> specializationArgs;

    // Constraint-specific (when hasModifier(Constraint))
    std::optional<std::string> parentConstraint; // constraint inheritance
    std::vector<ConstraintMember> constraintMembers;

    // Adapt-specific (when hasModifier(Adapt))
    std::string adaptConstraintName;
    TypeNode adaptTargetType;
    std::vector<AdaptMapping> adaptMappings;

    // TypeFn-specific (when hasModifier(TypeFn))
    std::string matchTarget; // which param to match on
    std::vector<TypeMatchArm> matchArms;

    TypeDecl(SourceLocation loc, std::string n) : ASTNode(ASTKind::TypeDecl, loc), name(std::move(n)) {}

    bool isTemplate() const { return !templateParams.empty(); }
    bool isConstraint() const { return hasModifier(ModifierData::Kind::Constraint); }
    bool isAdapt() const { return hasModifier(ModifierData::Kind::Adapt); }
    bool isTypeFn() const { return hasModifier(ModifierData::Kind::TypeFn); }
};

// Deprecated alias
using ClassDecl = TypeDecl;

// --- IfDecl: [comptime] if (expr) { ... } [else { ... }] (was ComptimeIf) ---

struct IfDecl : ASTNode {
    ExprPtr condition;
    std::vector<ASTNodePtr> thenBody;
    std::vector<ASTNodePtr> elseBody; // may be empty

    IfDecl(SourceLocation loc) : ASTNode(ASTKind::IfDecl, loc) {}
};

// Deprecated alias
using ComptimeIf = IfDecl;

// --- InstantiateDecl: instantiate Type; (now DataDecl + Instantiate modifier) ---
// Kept as legacy struct for backward-compatible construction only
struct InstantiateDecl : DataDecl {
    InstantiateDecl(SourceLocation loc, TypeNode t) : DataDecl(loc) {
        type = std::move(t);
        modifiers.push_back({ModifierData::Kind::Instantiate, loc, {}});
    }
};

// --- LifetimeDecl: lifetime name = range; (now DataDecl + Lifetime modifier) ---
// Kept as legacy struct for backward-compatible construction only
struct LifetimeDecl : DataDecl {
    LifetimeDecl(SourceLocation loc, std::string n) : DataDecl(loc) {
        name = std::move(n);
        modifiers.push_back({ModifierData::Kind::Lifetime, loc, {}});
    }
};

// --- DebugDecl ---
//
// `debug T { ... }` declares debug metadata for an existing TypeDecl or
// DataDecl named `T`. The block emits *no* codegen — it only contributes
// entries to the `*.topo-dbg.json` artifact consumed by `topo debug`.
//
// Body syntax (BNF, non-terminals lowercase):
//   debug_decl       ::= 'debug' IDENT '{' debug_body '}'
//   debug_body       ::= ( view_decl | summary_decl | inactive_decl
//                        | render_decl_raw )*
//   view_decl        ::= 'view' IDENT 'from' slice_expr ';'
//   summary_decl     ::= 'summary' STRING ';'
//   inactive_decl    ::= 'inactive_region' slice_expr ( 'grayed' | 'hidden' )? ';'
//   render_decl_raw  ::= 'render' 'method' '=' IDENT '{' …balanced… '}'
//
// A `slice_expr` is either `IDENT` (bare) or `IDENT '[' INT '..' INT ']'`.
// SemanticAnalyzer rejects anything else with "view expression must be a
// named slice".

enum class DebugInactiveMode {
    Default, // mode keyword absent — core stays neutral; default assigned downstream
    Grayed,
    Hidden,
};

inline const char* debugInactiveModeName(DebugInactiveMode m) {
    switch (m) {
    case DebugInactiveMode::Default: return "default";
    case DebugInactiveMode::Grayed: return "grayed";
    case DebugInactiveMode::Hidden: return "hidden";
    }
    return "unknown";
}

// `field` or `field[start..end]`, both non-negative integer endpoints.
struct DebugSliceExpr {
    std::string container;         // identifier of the field on the target
    bool isSliced = false;         // true when `[start..end]` was present
    std::optional<int64_t> start;  // populated iff isSliced
    std::optional<int64_t> end;    // populated iff isSliced (exclusive)
    SourceLocation location;

    std::string toString() const {
        if (!isSliced) return container;
        return container + "[" + (start ? std::to_string(*start) : "?") + ".." +
               (end ? std::to_string(*end) : "?") + "]";
    }
};

struct DebugViewEntry {
    std::string name;          // bound view name
    DebugSliceExpr slice;      // source slice expression
    SourceLocation location;
};

struct DebugInactiveEntry {
    DebugSliceExpr region;
    DebugInactiveMode mode = DebugInactiveMode::Default;
    SourceLocation location;
};

// Render decls: the Parser still captures the source span verbatim so it can
// point a precise diagnostic at the block, but web-rendering semantics are
// NOT implemented. SemanticAnalyzer explicitly REJECTS any `render` block
// with "render block: rendering not implemented" (no-silent-degradation) and
// drops the captured payload — it is never emitted as an active feature.
struct DebugRenderRaw {
    std::string method;        // identifier after `method = `; "" when absent
    std::string rawBody;       // raw `{ ... }` body, brace-balanced, braces *included*
    SourceLocation location;
};

struct DebugDecl : ASTNode {
    std::string targetTypeName; // IDENT after `debug`
    std::vector<DebugViewEntry> views;
    std::optional<std::string> summaryTemplate; // raw string literal, `{...}` interpolation not supported
    std::vector<DebugInactiveEntry> inactiveRegions;
    std::vector<DebugRenderRaw> renderDecls;

    DebugDecl(SourceLocation loc, std::string target)
        : ASTNode(ASTKind::DebugDecl, loc), targetTypeName(std::move(target)) {}
};

} // namespace topo

#endif // TOPO_AST_ASTNODE_H
