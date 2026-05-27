#ifndef TOPO_CHECK_STUBGENERATOR_H
#define TOPO_CHECK_STUBGENERATOR_H

#include <string>
#include <vector>

namespace topo::check {

/// Result of stubbing a function.
struct StubResult {
    bool success = false;
    std::string originalContent; // full original file content for restoration
    std::string error;
};

/// Abstract interface for generating function stubs in host language source.
/// Implementations replace function bodies with trivial stubs so that
/// code-stripping verification can test declaration consistency.
class StubGenerator {
public:
    virtual ~StubGenerator() = default;

    /// Replace the body of `funcName` in `filePath` with a trivial stub.
    /// The function is located by matching its name in the source text.
    /// Returns the original file content for restoration.
    virtual StubResult stubFunction(const std::string& filePath, const std::string& funcName) = 0;

    /// Restore the original content after stubbing.
    virtual bool restoreFile(const std::string& filePath, const StubResult& result) = 0;
};

} // namespace topo::check

#endif // TOPO_CHECK_STUBGENERATOR_H
