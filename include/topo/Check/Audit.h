#ifndef TOPO_CHECK_AUDIT_H
#define TOPO_CHECK_AUDIT_H

#include "topo/Check/AuditTypes.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace topo::check {

struct AuditSelector {
    std::string kind; // class or file
    std::string name;
    std::string file;
};

struct AuditPolicy {
    int schemaVersion = 1;
    std::string dependencies = "opaque";
    bool strictDependencies = false;
    std::vector<AuditSelector> manual;
    std::vector<std::string> expand;
    std::vector<std::string> deny;
};

/// Snapshot files and evaluate exact semantic identities. This proves only
/// the supplied analysis model. It never promotes a model to a build record.
nlohmann::json makeAuditReport(const std::string& project,
                               const AuditFacts& facts,
                               const AuditPolicy& policy,
                               const std::vector<std::string>& externalTypes,
                               const std::vector<std::string>& sourceFiles,
                               const std::vector<std::string>& additionalInputs);

/// Validate saved bytes before accepting a review, and immediately before use.
bool verifyAuditSnapshot(const nlohmann::json& report, std::string& error);

/// Trust is loaded from outside the subject project. A report cannot grant it.
void applyAuditTrust(nlohmann::json& report, const nlohmann::json& trust);

/// Includes project identity, policy, model inputs and selected identities.
std::string auditReviewKey(const nlohmann::json& report);

} // namespace topo::check
#endif
