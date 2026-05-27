#ifndef TOPO_DEBUG_QUERY_EVALUATOR_H
#define TOPO_DEBUG_QUERY_EVALUATOR_H

// Query evaluator.
//
// The evaluator walks an Expr AST against an `Environment` that maps bare
// identifiers to FrameView (variables captured by the Extract adapter at
// the current breakpoint). Built-in reductions are implemented internally;
// user functions are rejected ("user functions not implemented").

#include "topo/Debug/Query/Ast.h"
#include "topo/Debug/Query/FrameView.h"
#include "topo/Debug/Query/Value.h"

#include <map>
#include <string>

namespace topo::debug_meta {
class PassReportsRegistry;
}

namespace topo::debug_query {

struct Environment {
    // Variables visible at the current frame.
    std::map<std::string, FrameView> variables;

    // optional view onto `<output>.topo-passes/*.json`
    // sidecar reports. When non-null, the `pass_decision("PassName")`
    // builtin resolves against it. When null (default), the builtin errors
    // with "no pass-reports registry available". Default-init to nullptr so
    // every existing test/fixture stays green.
    const topo::debug_meta::PassReportsRegistry* passReports = nullptr;
};

struct EvalResult {
    bool ok = false;
    Value value;
    std::string error;
};

// Evaluate `expr` against `env`. Returns ok=true on success.
EvalResult evaluate(const Expr& expr, const Environment& env);

// Names of all built-in reductions. Used by docs and by error messages
// when a user function call is rejected.
const std::vector<std::string>& builtinReductionNames();

} // namespace topo::debug_query

#endif // TOPO_DEBUG_QUERY_EVALUATOR_H
