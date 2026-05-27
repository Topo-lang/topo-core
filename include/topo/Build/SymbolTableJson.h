#ifndef TOPO_BUILD_SYMBOLTABLEJSON_H
#define TOPO_BUILD_SYMBOLTABLEJSON_H

/// JSON serialization/deserialization for SymbolTable and VisibilityEntry types.
/// Used by IncrementalCache for caching .topo frontend results.

#include "topo/Sema/VisibilityCollector.h"
#include "topo/Sema/SymbolTable.h"

#include <nlohmann/json.hpp>

namespace topo {

// --- Enum serialization ---
void to_json(nlohmann::json& j, Visibility v);
void from_json(const nlohmann::json& j, Visibility& v);

void to_json(nlohmann::json& j, OwnershipKind k);
void from_json(const nlohmann::json& j, OwnershipKind& k);

void to_json(nlohmann::json& j, TypeNode::Modifier m);
void from_json(const nlohmann::json& j, TypeNode::Modifier& m);

void to_json(nlohmann::json& j, CallSiteInfo::Style s);
void from_json(const nlohmann::json& j, CallSiteInfo::Style& s);

void to_json(nlohmann::json& j, TemplateParamDecl::Kind k);
void from_json(const nlohmann::json& j, TemplateParamDecl::Kind& k);

void to_json(nlohmann::json& j, AccessPattern p);
void from_json(const nlohmann::json& j, AccessPattern& p);

// --- Type nodes ---
void to_json(nlohmann::json& j, const TypeNode& t);
void from_json(const nlohmann::json& j, TypeNode& t);

// --- Parameter types ---
void to_json(nlohmann::json& j, const Parameter& p);
void from_json(const nlohmann::json& j, Parameter& p);

void to_json(nlohmann::json& j, const ReturnParam& p);
void from_json(const nlohmann::json& j, ReturnParam& p);

void to_json(nlohmann::json& j, const PipelineEdge& e);
void from_json(const nlohmann::json& j, PipelineEdge& e);

void to_json(nlohmann::json& j, const TemplateParamDecl& d);
void from_json(const nlohmann::json& j, TemplateParamDecl& d);

// --- Symbol types ---
void to_json(nlohmann::json& j, const FunctionSymbol& s);
void from_json(const nlohmann::json& j, FunctionSymbol& s);

void to_json(nlohmann::json& j, const MemberVarSymbol& s);
void from_json(const nlohmann::json& j, MemberVarSymbol& s);

void to_json(nlohmann::json& j, const ClassSymbol& s);
void from_json(const nlohmann::json& j, ClassSymbol& s);

void to_json(nlohmann::json& j, const ConstraintMember& m);
void from_json(const nlohmann::json& j, ConstraintMember& m);

void to_json(nlohmann::json& j, const ConstraintSymbol& s);
void from_json(const nlohmann::json& j, ConstraintSymbol& s);

void to_json(nlohmann::json& j, const TypeAliasEntry& e);
void from_json(const nlohmann::json& j, TypeAliasEntry& e);

void to_json(nlohmann::json& j, const ImportEntry& e);
void from_json(const nlohmann::json& j, ImportEntry& e);

void to_json(nlohmann::json& j, const PipelineAnalysis& a);
void from_json(const nlohmann::json& j, PipelineAnalysis& a);

void to_json(nlohmann::json& j, const LogicBlockEntry& e);
void from_json(const nlohmann::json& j, LogicBlockEntry& e);

void to_json(nlohmann::json& j, const CallSiteInfo& c);
void from_json(const nlohmann::json& j, CallSiteInfo& c);

void to_json(nlohmann::json& j, const InstantiateEntry& e);
void from_json(const nlohmann::json& j, InstantiateEntry& e);

void to_json(nlohmann::json& j, const AdaptMapping& m);
void from_json(const nlohmann::json& j, AdaptMapping& m);

void to_json(nlohmann::json& j, const AdaptEntry& e);
void from_json(const nlohmann::json& j, AdaptEntry& e);

// --- Visibility entries ---
void to_json(nlohmann::json& j, const ParamConstInfo& p);
void from_json(const nlohmann::json& j, ParamConstInfo& p);

void to_json(nlohmann::json& j, const VisibilityEntry& e);
void from_json(const nlohmann::json& j, VisibilityEntry& e);

// --- SymbolTable (top-level) ---
nlohmann::json serializeSymbolTable(const SymbolTable& symbols);
bool deserializeSymbolTable(const nlohmann::json& j, SymbolTable& symbols);

} // namespace topo

#endif // TOPO_BUILD_SYMBOLTABLEJSON_H
