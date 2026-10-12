#ifndef TOPO_CHECK_AUDITTYPES_H
#define TOPO_CHECK_AUDITTYPES_H

#include <string>
#include <map>
#include <vector>

namespace topo::check {

// Facts describe the selected compilation model, not an observed execution.
struct AuditIdentity {
    std::string id, name, kind, signature, owner, file;
    int line = 0;
    std::string origin;
    bool complete = false;
};

struct AuditDependency {
    std::string id, path, version, digest;
    bool project = false;
    std::string ownership, coordinate;
};

struct AuditReference {
    std::string caller, target, file;
    int line = 0;
    bool resolved = false;
    std::string dispatch;
    bool runtimeTargetsComplete = false;
    std::string dependency;
};

struct AuditNativeBinding {
    std::string caller, file;
    int line = 0;
    std::string operation, library, symbol, descriptor;
    bool resolved = false;
    std::string callback, lookup, unresolvedReason;
    bool staticBinding = false, runtimeMapping = false;
    std::string provenanceStatus;
    std::vector<std::string> evidenceRecords, diagnostics;
};

struct AuditFacts {
    int schemaVersion = 1;
    std::string provider, modelPath, buildVariant;
    bool supported = false;
    bool complete = false;
    std::vector<AuditIdentity> identities;
    std::vector<AuditDependency> dependencies;
    std::vector<AuditReference> references;
    std::vector<AuditNativeBinding> nativeBindings;
    std::vector<std::string> inputs;
    std::map<std::string, std::string> inputDigests;
    std::string analysisIsolation;
    std::vector<std::string> unresolved;
};

} // namespace topo::check
#endif
