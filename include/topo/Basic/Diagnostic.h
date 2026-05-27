#ifndef TOPO_BASIC_DIAGNOSTIC_H
#define TOPO_BASIC_DIAGNOSTIC_H

#include "topo/Basic/SourceLocation.h"
#include <string>
#include <vector>
#include <ostream>

namespace topo {

enum class DiagLevel { Error, Warning };

struct Diagnostic {
    DiagLevel level;
    SourceLocation location;
    std::string message;
    std::string code;
};

class DiagnosticEngine {
public:
    void report(DiagLevel level, const SourceLocation& loc, const std::string& message, const std::string& code = "");

    void error(const SourceLocation& loc, const std::string& message, const std::string& code = "");
    void warning(const SourceLocation& loc, const std::string& message, const std::string& code = "");

    static constexpr int kMaxErrors = 100;
    bool reachedLimit() const { return errorCount_ >= kMaxErrors; }
    bool hasErrors() const { return errorCount_ > 0; }
    int errorCount() const { return errorCount_; }
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

    void print(std::ostream& os) const;

private:
    std::vector<Diagnostic> diagnostics_;
    int errorCount_ = 0;
};

} // namespace topo

#endif // TOPO_BASIC_DIAGNOSTIC_H
