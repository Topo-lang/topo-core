#ifndef TOPO_DEBUG_QUERY_AST_H
#define TOPO_DEBUG_QUERY_AST_H

// Minimal query AST.
//
//   query   ::= add_sub
//   add_sub ::= mul_div ( ('+' | '-') mul_div )*
//   mul_div ::= unary   ( ('*' | '/') unary   )*
//   unary   ::= '-' unary | postfix
//   postfix ::= primary ( '.' ident | '[' range ']' | '(' arg_list ')' )*
//   primary ::= ident | number | string
//   range   ::= add_sub '..' add_sub          // half-open, end exclusive
//   arg_list ::= add_sub (',' add_sub)*
//
// Arithmetic operators (`+ - * /` + unary `-`) let
// summary templates and ad-hoc queries combine reductions naturally
// (e.g. `(max(matrix) - min(matrix)) / count(matrix)`). Recursive descent
// parser builds these nodes; Evaluator walks them.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace topo::debug_query {

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

enum class ExprKind {
    Ident,      // bare identifier (variable / function name root)
    IntLit,
    FloatLit,
    StringLit,
    FieldAccess, // base '.' name
    Slice,       // base '[' start '..' end ']'
    Call,        // callee '(' args ')'
    BinaryOp,    // lhs <op> rhs   (op ∈ + - * /)
    UnaryOp,     // <op> operand   (op ∈ -)
};

enum class BinaryOpKind {
    Add,    // +
    Sub,    // -
    Mul,    // *
    Div,    // /  (always returns float to avoid 10/3==3 surprises)
};

enum class UnaryOpKind {
    Neg,    // unary -
};

struct Expr {
    ExprKind kind;
    // Position of the first token of this node (1-indexed character offset
    // in the source). Used by Evaluator to attach error locations.
    size_t pos = 0;

    // Ident / FieldAccess.name
    std::string name;
    // IntLit
    int64_t intValue = 0;
    // FloatLit
    double floatValue = 0.0;
    // StringLit
    std::string stringValue;
    // FieldAccess.base, Slice.base, Call.callee, UnaryOp.operand
    ExprPtr base;
    // Slice
    ExprPtr sliceStart;
    ExprPtr sliceEnd;
    // Call
    std::vector<ExprPtr> args;
    // BinaryOp.lhs is stored in `base` (reuse) — rhs in `binaryRhs`.
    ExprPtr binaryRhs;
    BinaryOpKind binaryOp = BinaryOpKind::Add;
    UnaryOpKind unaryOp = UnaryOpKind::Neg;
};

inline ExprPtr makeIdent(std::string n, size_t pos) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::Ident;
    e->name = std::move(n);
    e->pos = pos;
    return e;
}

inline ExprPtr makeInt(int64_t v, size_t pos) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::IntLit;
    e->intValue = v;
    e->pos = pos;
    return e;
}

inline ExprPtr makeFloat(double v, size_t pos) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::FloatLit;
    e->floatValue = v;
    e->pos = pos;
    return e;
}

inline ExprPtr makeString(std::string v, size_t pos) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::StringLit;
    e->stringValue = std::move(v);
    e->pos = pos;
    return e;
}

inline ExprPtr makeBinary(BinaryOpKind op, ExprPtr lhs, ExprPtr rhs, size_t pos) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::BinaryOp;
    e->binaryOp = op;
    e->base = std::move(lhs);
    e->binaryRhs = std::move(rhs);
    e->pos = pos;
    return e;
}

inline ExprPtr makeUnary(UnaryOpKind op, ExprPtr operand, size_t pos) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::UnaryOp;
    e->unaryOp = op;
    e->base = std::move(operand);
    e->pos = pos;
    return e;
}

} // namespace topo::debug_query

#endif // TOPO_DEBUG_QUERY_AST_H
