#ifndef TOPO_CHECK_LANGUAGEANALYSISPROVIDER_H
#define TOPO_CHECK_LANGUAGEANALYSISPROVIDER_H

#include "topo/Build/PassConfig.h"
#include "topo/Check/CallEdgeExtractor.h"
#include "topo/Check/AuditTypes.h"
#include "topo/Check/CallSiteExtractor.h"
#include "topo/Check/CheckTypes.h"
#include "topo/Check/ImportExtractor.h"
#include "topo/Check/SymbolAccessExtractor.h"
#include "topo/Check/SymbolExtractor.h"
#include "topo/Sema/SymbolTable.h"
#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace topo::check {

struct AuditExecutionOptions {
    // Explicit reviewer grants, supplied outside project-controlled models.
    std::vector<std::string> inputRoots;
};

/// Abstract factory for language-specific analysis components.
/// Each topo-lang-* package provides a concrete implementation.
class LanguageAnalysisProvider {
public:
    virtual ~LanguageAnalysisProvider() = default;

    /// Require OS confinement before cache lookup or extraction. Unsupported
    /// providers fail explicitly; ordinary checks retain their normal policy.
    virtual bool configureAuditExecution(const AuditExecutionOptions&, std::string& diagnostic) {
        diagnostic = "provider has no isolated audit execution capability";
        return false;
    }

    /// Supply project context before creating extractors. Paths in a model
    /// are relative to the model file, not the calling process.
    virtual void configureAnalysis(const std::string& /*projectDir*/,
                                   const std::string& /*projectModelPath*/) {}

    /// Extraction failures are check failures, even when no facts were produced.
    virtual std::vector<CheckDiagnostic> analysisDiagnostics() const { return {}; }

    /// Semantic identities for an explicit audit. Legacy extraction has no
    /// implicit audit capability: unsupported facts must stay unverified.
    virtual AuditFacts auditFacts(const std::vector<std::string>& /*sourceFiles*/) const {
        return {};
    }

    /// Optional closed-input cache protocol. Identity must cover negative
    /// lookups and the executing toolchain, before any expensive extraction.
    /// Cache payloads contain facts and diagnostics, never policy or approval.
    virtual nlohmann::json auditCacheIdentity(const std::string& /*projectDir*/,
                                             const std::string& /*modelPath*/) const {
        return {{"supported", false}, {"reason", "provider has no closed-input audit cache identity"}};
    }
    virtual nlohmann::json exportAuditCache() const { return nullptr; }
    virtual bool importAuditCache(const nlohmann::json& /*payload*/) { return false; }
    virtual void configureNativeEvidence(const std::string& /*manifest*/,
                                          const std::map<std::string,std::string>& /*roots*/) {}

    virtual std::unique_ptr<SymbolExtractor> createSymbolExtractor() = 0;
    virtual std::unique_ptr<ImportExtractor> createImportExtractor() = 0;
    virtual std::unique_ptr<CallSiteExtractor> createCallSiteExtractor() = 0;

    /// Optional: produces caller → callee edges for StageIsolationCheck and VisibilityCheck.
    /// Default returns nullptr — CheckRunner emits "extractor unavailable" diagnostic.
    virtual std::unique_ptr<CallEdgeExtractor> createCallEdgeExtractor() { return nullptr; }

    /// Optional: produces global/shared symbol accesses for PurityCheck.
    /// Default returns nullptr — CheckRunner emits "extractor unavailable" diagnostic.
    virtual std::unique_ptr<SymbolAccessExtractor> createSymbolAccessExtractor() { return nullptr; }

    /// Collect host language source files from the project directory.
    virtual std::vector<std::string> collectSourceFiles(
        const std::string& projectDir,
        const std::vector<std::string>& includeDirs) const = 0;

    /// Separator used by this language's qualified-name extractor output
    /// (e.g. "MyClass::method" for C++/Rust vs "MyClass.method" for TS/Python).
    /// Consumers like ContainmentCheck use this to derive the simple-name
    /// fallback from a qualifiedName. Default `"::"` matches C++/Rust/Java
    /// extractors; TS/Python providers must override to `"."`.
    virtual std::string separator() const { return "::"; }

    /// Initialize LSP bridge for semantic analysis. Returns true on success.
    virtual bool initLSP(const std::string& /*projectDir*/, bool /*verbose*/) { return false; }

    /// Shut down LSP bridge.
    virtual void shutdownLSP() {}

    /// Check if LSP bridge is ready.
    virtual bool isLSPReady() const { return false; }

    /// L2 deep containment analysis. Returns nullopt if not supported for this language.
    virtual std::optional<CheckResult> runDeepContainment(
        const SymbolTable& /*symbols*/,
        const std::vector<std::string>& /*sourceFiles*/,
        const ContainmentConfig& /*config*/,
        const std::string& /*projectDir*/,
        bool /*verbose*/) { return std::nullopt; }
};

} // namespace topo::check

#endif // TOPO_CHECK_LANGUAGEANALYSISPROVIDER_H
