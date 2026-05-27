#ifndef TOPO_TRANSPILE_TRANSPILEMODEL_H
#define TOPO_TRANSPILE_TRANSPILEMODEL_H

#include "topo/AST/ASTNode.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace topo::transpile {

// --- Fidelity: confidence level of each node ---
enum class Fidelity { Source, Recovered, Inferred };

// --- Expression types ---

enum class BinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Eq,
    NotEq,
    Less,
    Greater,
    LessEq,
    GreaterEq,
    And,
    Or,
    BitAnd,
    BitOr,
    BitXor,
    Shl,
    Shr
};

enum class UnaryOp { Negate, Not, BitNot, PreIncrement, PostIncrement, PreDecrement, PostDecrement };

enum class LiteralKind { Integer, Float, Boolean, String };

// --- Lambda/Closure capture ---

enum class CaptureMode { ByValue, ByReference };

struct CaptureEntry {
    std::string name;
    CaptureMode mode = CaptureMode::ByValue;
};

// Forward declarations
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;
struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Expr {
    Fidelity fidelity = Fidelity::Source;
    virtual ~Expr() = default;

    enum class Kind {
        BinaryOp,
        UnaryOp,
        Call,
        MemberAccess,
        Index,
        Literal,
        VarRef,
        Construct,
        Lambda,
        Throw,
        Unsupported,
        Ternary,
        CompoundAssign
    };
    virtual Kind kind() const = 0;
};

struct BinaryOpExpr : Expr {
    BinaryOp op;
    ExprPtr lhs;
    ExprPtr rhs;
    Kind kind() const override { return Kind::BinaryOp; }
};

struct UnaryOpExpr : Expr {
    UnaryOp op;
    ExprPtr operand;
    Kind kind() const override { return Kind::UnaryOp; }
};

struct CallExpr : Expr {
    std::string callee; // qualified name
    std::vector<ExprPtr> args;
    Kind kind() const override { return Kind::Call; }
};

struct MemberAccessExpr : Expr {
    ExprPtr object;
    std::string member;
    Kind kind() const override { return Kind::MemberAccess; }
};

struct IndexExpr : Expr {
    ExprPtr object;
    ExprPtr index;
    Kind kind() const override { return Kind::Index; }
};

struct LiteralExpr : Expr {
    LiteralKind litKind;
    std::string value;
    Kind kind() const override { return Kind::Literal; }
};

struct VarRefExpr : Expr {
    std::string name;
    Kind kind() const override { return Kind::VarRef; }
};

struct ConstructExpr : Expr {
    TypeNode type;
    std::vector<ExprPtr> args;
    Kind kind() const override { return Kind::Construct; }
};

struct LambdaExpr : Expr {
    std::vector<CaptureEntry> captures;
    std::vector<Parameter> params;
    TypeNode returnType; // may be empty (inferred)
    std::vector<StmtPtr> body;
    Kind kind() const override { return Kind::Lambda; }
};

struct ThrowExpr : Expr {
    ExprPtr operand; // the expression being thrown
    Kind kind() const override { return Kind::Throw; }
};

struct UnsupportedExpr : Expr {
    std::string description;
    Kind kind() const override { return Kind::Unsupported; }
};

struct TernaryExpr : Expr {
    ExprPtr condition;
    ExprPtr trueExpr;
    ExprPtr falseExpr;
    Kind kind() const override { return Kind::Ternary; }
};

struct CompoundAssignExpr : Expr {
    BinaryOp op; // Add, Sub, Mul, Div, Mod, BitAnd, BitOr, BitXor, Shl, Shr
    ExprPtr target;
    ExprPtr value;
    Kind kind() const override { return Kind::CompoundAssign; }
};

// --- Statement types ---

struct Stmt {
    Fidelity fidelity = Fidelity::Source;
    virtual ~Stmt() = default;

    enum class Kind { VarDecl, Assign, Return, If, For, While, ExprStmt, TryCatch, Break, Continue, Switch };
    virtual Kind kind() const = 0;
};

struct VarDeclStmt : Stmt {
    TypeNode type;
    std::string name;
    ExprPtr init; // optional
    Kind kind() const override { return Kind::VarDecl; }
};

struct AssignStmt : Stmt {
    ExprPtr target;
    ExprPtr value;
    Kind kind() const override { return Kind::Assign; }
};

struct ReturnStmt : Stmt {
    ExprPtr value; // optional (void return)
    Kind kind() const override { return Kind::Return; }
};

struct IfStmt : Stmt {
    ExprPtr condition;
    std::vector<StmtPtr> thenBody;
    std::vector<StmtPtr> elseBody; // may be empty
    Kind kind() const override { return Kind::If; }
};

struct ForStmt : Stmt {
    StmtPtr init; // typically VarDecl
    ExprPtr condition;
    ExprPtr increment;
    std::vector<StmtPtr> body;
    Kind kind() const override { return Kind::For; }
};

struct WhileStmt : Stmt {
    ExprPtr condition;
    std::vector<StmtPtr> body;
    Kind kind() const override { return Kind::While; }
};

struct ExprStmt : Stmt {
    ExprPtr expr;
    Kind kind() const override { return Kind::ExprStmt; }
};

struct CatchClause {
    TypeNode exceptionType;
    std::string varName;
    std::vector<StmtPtr> body;
};

struct TryCatchStmt : Stmt {
    std::vector<StmtPtr> tryBody;
    std::vector<CatchClause> catchClauses;
    std::vector<StmtPtr> finallyBody; // may be empty
    Kind kind() const override { return Kind::TryCatch; }
};

struct BreakStmt : Stmt {
    Kind kind() const override { return Kind::Break; }
};

struct ContinueStmt : Stmt {
    Kind kind() const override { return Kind::Continue; }
};

struct SwitchCase {
    ExprPtr value; // nullptr for default case
    std::vector<StmtPtr> body;
};

struct SwitchStmt : Stmt {
    ExprPtr subject;
    std::vector<SwitchCase> cases;
    Kind kind() const override { return Kind::Switch; }
};

// --- Top-level constructs ---

struct TranspileField {
    TypeNode type;
    std::string name;
    Fidelity fidelity = Fidelity::Source;
};

// Per-base class-vs-interface discriminator. Java needs this to place a base
// in `extends` (a class superclass, or an interface's parent interface) vs
// `implements` (a class's implemented interfaces). Languages without the
// distinction (C++, Python) ignore it.
enum class BaseClassKind { Class, Interface };

struct TranspileType {
    std::string qualifiedName;
    std::vector<TranspileField> fields;
    // Inheritance hierarchy: base classes / implemented interfaces, in source
    // order. The host emitter decides which entries are an `extends` target vs
    // an `implements`/trait list. Default empty ⇒ no inheritance (backward
    // compatible: old JSON without this key deserializes to empty, and an
    // empty list emits byte-identically to the pre-inheritance output).
    std::vector<TypeNode> baseClasses;
    // Parallel to baseClasses (same length when populated): class-vs-interface
    // tag for each base. EMPTY ⇒ no discriminator available; emitters fall back
    // to the legacy heuristic (Java: first base = extends, rest = implements),
    // preserving exact pre-discriminator behavior and byte-identical output.
    // When non-empty it MUST be the same length as baseClasses.
    std::vector<BaseClassKind> baseClassKinds;
    // Declaration-site generic type parameters (`struct S<T>` / class
    // templates). Reuses the frontend AST TemplateParamDecl. MVP carries
    // only TypeParam names; richer forms downgrade fidelity. Default empty
    // ⇒ JSON key omitted ⇒ byte-identical to pre-generics output.
    std::vector<TemplateParamDecl> templateParams;
    Fidelity fidelity = Fidelity::Source;
};

struct TranspileFunction {
    std::string qualifiedName;
    TypeNode returnType;
    std::vector<Parameter> params; // reuse from ASTNode.h
    std::vector<StmtPtr> body;
    std::vector<std::string> unsupported; // human-readable list
    Fidelity fidelity = Fidelity::Source;
    std::string accessModifier; // "public", "private", "protected", "" (empty = default/package-private)
    // Declared checked-exception types (Java `throws` clause). Empty for
    // languages without checked exceptions; defaulted empty so non-Java
    // emitters/lifters are unaffected.
    std::vector<TypeNode> throwsClause;
    // Declaration-site generic type parameters (`fn foo<T>` / function
    // templates). Same TemplateParamDecl reuse and empty-default
    // backward-compat convention as TranspileType::templateParams.
    std::vector<TemplateParamDecl> templateParams;
};

struct TranspileModule {
    std::vector<TranspileType> types;
    std::vector<TranspileFunction> functions;
};

// --- Post-transpile verification summary ---
//
// A structured, language-agnostic aggregate over a TranspileModule. This is
// intentionally NOT a re-run of CompletenessCheck against the emitted source:
// CompletenessCheck requires host symbols re-extracted from the emitted code
// via per-target-language extractors, which would mean re-parsing the output
// in every target language. That defeats the point of transpile and is out of
// scope. Instead we surface the information the lifter already recorded:
// unsupported constructs and per-node fidelity.

struct FunctionUnsupported {
    std::string qualifiedName;
    std::vector<std::string> constructs;
};

struct FidelityBreakdown {
    // Counts over functions/types/fields whose fidelity is recorded.
    int source = 0;    // Fidelity::Source
    int recovered = 0; // Fidelity::Recovered
    int inferred = 0;  // Fidelity::Inferred

    int total() const { return source + recovered + inferred; }
};

struct TranspileVerification {
    int totalUnsupported = 0;                          // sum over all functions
    std::vector<FunctionUnsupported> perFunction;       // only functions with >=1 unsupported
    FidelityBreakdown fidelity;                          // function + type + field fidelity

    bool clean() const { return totalUnsupported == 0; }
};

// Compute the verification summary for a module. Pure function: no I/O, no
// subprocess. Directly unit-testable.
TranspileVerification verifyModule(const TranspileModule& module);

// Decision of the configurable post-transpile gate.
struct GateDecision {
    bool failed = false;     // true iff the gate tripped (limit exceeded)
    std::string error;       // precise message when failed; empty otherwise
};

// Pure gate decision: returns failed=true with a precise error iff a limit is
// set and the verification's total unsupported count exceeds it. No I/O.
// Directly unit-testable; TranspileDriver::run() applies this result to
// TranspileResult::errors / ::success.
GateDecision applyVerificationGate(const TranspileVerification& v,
                                   std::optional<int> verifyMaxUnsupported);

} // namespace topo::transpile

#endif // TOPO_TRANSPILE_TRANSPILEMODEL_H
