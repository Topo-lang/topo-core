#include "topo/Basic/Diagnostic.h"

namespace topo {

void DiagnosticEngine::report(DiagLevel level,
                              const SourceLocation& loc,
                              const std::string& message,
                              const std::string& code) {
    if (level == DiagLevel::Error && errorCount_ >= kMaxErrors) {
        return; // silently drop — limit already reached
    }
    diagnostics_.push_back({level, loc, message, code});
    if (level == DiagLevel::Error) {
        ++errorCount_;
        if (errorCount_ == kMaxErrors) {
            diagnostics_.push_back(
                {DiagLevel::Error,
                 loc,
                 "error limit reached (" + std::to_string(kMaxErrors) + " errors); further errors suppressed",
                 ""});
        }
    }
}

void DiagnosticEngine::error(const SourceLocation& loc, const std::string& message, const std::string& code) {
    report(DiagLevel::Error, loc, message, code);
}

void DiagnosticEngine::warning(const SourceLocation& loc, const std::string& message, const std::string& code) {
    report(DiagLevel::Warning, loc, message, code);
}

void DiagnosticEngine::print(std::ostream& os) const {
    for (const auto& diag : diagnostics_) {
        os << diag.location.file << ":" << diag.location.line << ":" << diag.location.column << ": ";
        switch (diag.level) {
        case DiagLevel::Error: os << "error: "; break;
        case DiagLevel::Warning: os << "warning: "; break;
        }
        os << diag.message << "\n";
    }
}

} // namespace topo
