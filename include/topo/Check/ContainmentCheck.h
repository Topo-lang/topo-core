#ifndef TOPO_CHECK_CONTAINMENTCHECK_H
#define TOPO_CHECK_CONTAINMENTCHECK_H

#include "topo/Build/PassConfig.h"
#include "topo/Check/CheckTypes.h"
#include "topo/Check/ContainmentTypes.h"
#include "topo/Sema/SymbolTable.h"
#include <vector>

namespace topo::check {

/// L1 containment check: verifies non-external functions don't access external APIs.
///
/// Pass 1 — Import check:
///   Files with restricted imports (file/network/process/dynload) must have at least
///   one external function declared in .topo. Otherwise violation.
///
/// Pass 2 — Call site check:
///   Non-external functions calling external API patterns are violations.
///   External functions calling anything -> skip (not checked).
///   Non-external functions calling external functions -> skip (boundary delegation).
///
/// Severity: Auto = Warning, Force = Error.
///
/// `separator` is the per-language qualified-name component separator used
/// when deriving a simple-name fallback from a caller's qualifiedName
/// (e.g. `"MyClass::render"` -> `"render"` with `"::"`, or
/// `"MyClass.render"` -> `"render"` with `"."`). Defaults to `"::"` to
/// preserve the historical behaviour for C++/Rust/Java; TS/Python callers
/// must pass `"."`.
void checkContainment(const SymbolTable& symbols,
                      const std::vector<HostImport>& imports,
                      const std::vector<DetectedCallSite>& callSites,
                      const ContainmentConfig& config,
                      CheckResult& result,
                      const std::string& separator = "::");

} // namespace topo::check

#endif // TOPO_CHECK_CONTAINMENTCHECK_H
