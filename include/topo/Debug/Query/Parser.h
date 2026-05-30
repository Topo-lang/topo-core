#ifndef TOPO_DEBUG_QUERY_PARSER_H
#define TOPO_DEBUG_QUERY_PARSER_H

// Query expression parser (recursive descent on the
// query-expression grammar).
//
// On parse error returns nullptr and fills `error` with a human-readable
// message including the column offset.

#include "topo/Debug/Query/Ast.h"

#include <string>

namespace topo::debug_query {

ExprPtr parseQuery(const std::string& source, std::string& error);

} // namespace topo::debug_query

#endif // TOPO_DEBUG_QUERY_PARSER_H
