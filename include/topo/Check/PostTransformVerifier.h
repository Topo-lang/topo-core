#ifndef TOPO_CHECK_POSTTRANSFORMVERIFIER_H
#define TOPO_CHECK_POSTTRANSFORMVERIFIER_H

#include "topo/Check/CheckTypes.h"
#include "topo/Sema/SymbolTable.h"
#include "topo/Sema/VisibilityCollector.h"

#include <string>
#include <vector>

namespace topo::check {

/// Result of post-transform verification.
struct PostTransformResult {
    int visibilityVerified = 0;    // Access flags match .topo
    int visibilityMismatch = 0;    // Access flags DON'T match
    int obfuscationVerified = 0;   // Symbols correctly renamed
    int obfuscationMismatch = 0;
    std::vector<CheckDiagnostic> diagnostics;

    bool passed() const {
        return visibilityMismatch == 0 && obfuscationMismatch == 0;
    }

    int totalChecks() const {
        return visibilityVerified + visibilityMismatch +
               obfuscationVerified + obfuscationMismatch;
    }
};

/// Abstract interface for backend-specific post-transform verification.
/// Each backend (LLVM, JVM) implements this for its artifact format.
///
/// The verifier reads transformed artifacts (IR bitcode, .class files)
/// and confirms that the transformations preserved .topo invariants:
/// - Visibility: access modifiers match declarations
/// - Obfuscation: internal symbols were renamed, public preserved
class PostTransformVerifier {
public:
    virtual ~PostTransformVerifier() = default;

    /// Verify transformed artifacts against .topo metadata.
    /// @param symbols  The SymbolTable from .topo frontend
    /// @param visEntries  Visibility declarations
    /// @param artifactDir  Directory containing transformed output
    /// @return Verification result with diagnostics
    virtual PostTransformResult verify(
        const SymbolTable& symbols,
        const std::vector<VisibilityEntry>& visEntries,
        const std::string& artifactDir) = 0;
};

} // namespace topo::check

#endif // TOPO_CHECK_POSTTRANSFORMVERIFIER_H
