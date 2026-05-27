#ifndef TOPO_TRANSPILE_TRANSPILEMODELJSON_H
#define TOPO_TRANSPILE_TRANSPILEMODELJSON_H

#include "topo/Transpile/TranspileModel.h"
#include "topo/Transpile/BackendLifter.h"
#include <nlohmann/json.hpp>

namespace topo::transpile {

// --- Enum serialization ---
void to_json(nlohmann::json& j, Fidelity f);
void from_json(const nlohmann::json& j, Fidelity& f);
void to_json(nlohmann::json& j, BinaryOp op);
void from_json(const nlohmann::json& j, BinaryOp& op);
void to_json(nlohmann::json& j, UnaryOp op);
void from_json(const nlohmann::json& j, UnaryOp& op);
void to_json(nlohmann::json& j, LiteralKind k);
void from_json(const nlohmann::json& j, LiteralKind& k);
void to_json(nlohmann::json& j, CaptureMode m);
void from_json(const nlohmann::json& j, CaptureMode& m);
void to_json(nlohmann::json& j, DecompileLevel l);
void from_json(const nlohmann::json& j, DecompileLevel& l);

// --- Expressions (polymorphic via kind discriminator) ---
nlohmann::json serializeExpr(const Expr& e);
ExprPtr deserializeExpr(const nlohmann::json& j);

// --- Statements (polymorphic via kind discriminator) ---
nlohmann::json serializeStmt(const Stmt& s);
StmtPtr deserializeStmt(const nlohmann::json& j);

// --- Top-level types ---
void to_json(nlohmann::json& j, const TranspileField& f);
void from_json(const nlohmann::json& j, TranspileField& f);
void to_json(nlohmann::json& j, const TranspileType& t);
void from_json(const nlohmann::json& j, TranspileType& t);
void to_json(nlohmann::json& j, const TranspileFunction& f);
void from_json(const nlohmann::json& j, TranspileFunction& f);

// --- Module (top-level) ---
nlohmann::json serializeModule(const TranspileModule& m);
TranspileModule deserializeModule(const nlohmann::json& j);

} // namespace topo::transpile

#endif // TOPO_TRANSPILE_TRANSPILEMODELJSON_H
