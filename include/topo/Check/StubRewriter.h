#ifndef TOPO_CHECK_STUBREWRITER_H
#define TOPO_CHECK_STUBREWRITER_H

#include "topo/Check/StubGenerator.h"
#include "topo/Sema/SymbolTable.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace topo::check {

/// Result of a batch stub rewrite operation.
/// Tracks all files modified so they can be restored atomically.
struct StubRewriteResult {
    bool success = false;
    std::string error;

    /// Per-file original content for restoration.
    /// Key: file path, Value: original file content.
    std::unordered_map<std::string, std::string> originalContents;

    /// Functions that were successfully stubbed.
    std::vector<std::string> stubbedFunctions;

    /// Functions declared in .topo but not found in any source file.
    int skippedCount = 0;
};

/// Rewrites source files by replacing function bodies with minimal stubs.
///
/// Given a set of function names (typically all functions in stages below
/// a threshold), StubRewriter locates each function in the source files
/// and replaces its body with a default-returning stub. This enables
/// pruning-based verification: if tests for stage S still pass after
/// stubbing all stages < S, stage S is properly isolated.
///
/// StubRewriter operates on the host language source via a StubGenerator
/// (C++/Rust/Java), and maintains backup state for atomic rollback.
class StubRewriter {
public:
    /// @param stubGen  Language-specific stub generator (owned).
    /// @param sourceFiles  All host language source files in the project.
    StubRewriter(std::unique_ptr<StubGenerator> stubGen, std::vector<std::string> sourceFiles);

    /// Stub all functions in `funcNames` across the source files.
    /// Each function is located by simple name in the source files.
    /// On any failure, already-applied stubs are rolled back.
    StubRewriteResult stubFunctions(const std::unordered_set<std::string>& funcNames);

    /// Restore all files modified by a previous stubFunctions() call.
    /// Uses the original content saved in the result.
    bool restore(const StubRewriteResult& result);

private:
    /// Find the source file containing a given function name.
    std::string findSourceFile(const std::string& funcName) const;

    std::unique_ptr<StubGenerator> stubGen_;
    std::vector<std::string> sourceFiles_;
};

} // namespace topo::check

#endif // TOPO_CHECK_STUBREWRITER_H
