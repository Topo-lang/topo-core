#ifndef TOPO_BUILD_BACKENDPROTOCOL_H
#define TOPO_BUILD_BACKENDPROTOCOL_H

/// JSON-based communication protocol between topo-build (frontend) and
/// backend tools (topo-build-llvm-cpp, topo-build-llvm-rust, topo-build-llvm-mixed, topo-build-jvm-java).
///
/// topo-build serializes a request to a temp JSON file, then invokes
/// the backend tool as a subprocess with that file path as argv[1].

#include "topo/Backend/BackendTypes.h"
#include "topo/Basic/BuildTypes.h"
#include "topo/Basic/HostLanguage.h"
#include "topo/Sema/SymbolTable.h"
#include "topo/Sema/VisibilityCollector.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_set>
#include <vector>

namespace topo::build {

/// All data the backend tool needs to execute Steps 3-7.
struct BackendRequest {
    backend::BackendConfig config;
    std::string outputPath;
    SymbolTable symbolTable;
    std::vector<VisibilityEntry> visibilityEntries;
    std::string tempDir;
    HostLanguage language = HostLanguage::Cpp;

    // Generic fields (all backends)
    std::vector<std::string> sources;
    std::vector<std::string> includeDirs;
    std::vector<std::string> linkLibs;
    std::vector<std::string> linkDirs;
    bool verbose = false;
    bool keepTemps = false;
    bool noIncremental = false;

    // Backend-specific extensions (JSON object, each backend parses its own keys)
    nlohmann::json backendExtras = nlohmann::json::object();
};

/// Serialize a BackendRequest to JSON string.
std::string serializeBackendRequest(const BackendRequest& req);

/// Deserialize a BackendRequest from JSON string.
/// Returns false on parse error.
bool deserializeBackendRequest(const std::string& jsonStr, BackendRequest& req);

/// Determine the backend tool name for the given language.
/// Returns "topo-build-llvm-cpp", "topo-build-llvm-rust", "topo-build-llvm-mixed",
/// "topo-build-jvm-java", or "topo-build-python".
std::string backendToolName(HostLanguage language);

/// Per-backend `backendExtras` known-key registry.
///
/// Returns the set of `backendExtras` keys the named backend understands.
/// The JVM sub-schema (`HostLanguage::Java`) is the only one currently
/// enforced — `deserializeBackendRequest` rejects unknown JVM keys.
/// LLVM-cpp / LLVM-rust / LLVM-mixed / Python / TypeScript sub-schemas
/// list their accepted keys here for documentation and forward
/// reference, but the deserializer keeps the historical silent-tolerant
/// behaviour for them (unknown-key tolerance for non-JVM backends).
const std::unordered_set<std::string>& knownBackendExtrasKeys(HostLanguage language);

/// Whether unknown `backendExtras` keys are rejected for the given
/// backend at deserialize time. Only the JVM backend rejects today.
bool backendExtrasRejectsUnknown(HostLanguage language);

} // namespace topo::build

#endif // TOPO_BUILD_BACKENDPROTOCOL_H
