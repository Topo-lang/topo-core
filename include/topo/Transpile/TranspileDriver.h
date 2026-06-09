#ifndef TOPO_TRANSPILE_TRANSPILEDRIVER_H
#define TOPO_TRANSPILE_TRANSPILEDRIVER_H

#include "topo/Basic/HostLanguage.h"
#include "topo/Sema/SymbolTable.h"
#include "topo/Transpile/AdapterResolver.h"
#include "topo/Transpile/Emitter.h"
#include "topo/Transpile/TranspileModel.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace topo::transpile {

struct TranspileRequest {
    std::filesystem::path topoFile;                 // .topo declaration file
    std::vector<std::filesystem::path> sourceFiles; // source files to extract from
    HostLanguage sourceLanguage;
    HostLanguage targetLanguage;
    std::filesystem::path outputDir;
    std::vector<std::string> functions;     // empty = all declared functions
    std::string pipelineName;               // empty = no pipeline filter

    // `.topo`-source mode. When true, the source side
    // is the `.topo` file itself: the driver builds the TranspileModule from
    // the parsed `.topo` AST via TopoSourceBuilder (composite-function bodies
    // generated from logic blocks) and fills leaf functions via the
    // AdapterResolver — instead of spawning a host-source extractor. The CLI
    // sets this when `--from topo` is given. In this mode `sourceFiles` is
    // not required and `sourceLanguage` is ignored.
    bool fromTopoSource = false;
    // Path to a topo-app adapter manifest, assembled as the topo-app adapter
    // source in `.topo`-source mode (the CLI `--adapters` flag). Empty = only
    // the always-on builtin adapter source is used.
    std::string adapterManifestPath;
    // tpm adapter sources: one per installed tpm package whose adapter
    // manifest the caller resolved. Assembled at priority tpm > topo-app >
    // builtin in `.topo`-source mode. The CLI populates this from
    // `--tpm-adapters <pkg>=<path>`. Empty = no tpm sources. topo-core does
    // NOT discover tpm packages itself (it never depends on topo-tpm); the
    // package→manifest mapping is supplied from outside.
    std::vector<TpmAdapterManifestRef> tpmAdapterManifests;

    // Post-transpile verification gate. nullopt = no gate (driver does not
    // fail on unsupported constructs; the CLI may still apply its own legacy
    // default exit policy). When set, the driver itself enforces it: if the
    // module's total unsupported-construct count exceeds this value, a precise
    // error is appended and TranspileResult::success is set to false.
    // 0 = strict (any unsupported construct fails).
    std::optional<int> verifyMaxUnsupported;
};

struct TranspileResult {
    bool success = false;
    std::vector<std::string> outputFiles;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;   // target-language capability errors from emitter
    int unsupportedCount = 0;
    // Structured aggregate over the lifted module (unsupported counts +
    // per-function list + fidelity breakdown). Populated whenever emission
    // succeeds; empty/zeroed otherwise.
    TranspileVerification verification;
};

class TranspileDriver {
public:
    TranspileResult run(const TranspileRequest& request);

private:
    // `.topo`-source pipeline (M4): build the TranspileModule from the parsed
    // `.topo` AST (TopoSourceBuilder) + fill leaves (AdapterResolver), instead
    // of spawning a host-source extractor. Routed to by run() when
    // request.fromTopoSource is set. The host-source path is left untouched.
    TranspileResult runFromTopoSource(const TranspileRequest& request);

    // Parse .topo and build SymbolTable
    bool parseTopoFile(const std::filesystem::path& topoFile, SymbolTable& symbols, std::vector<std::string>& errors);

    // Call extractor subprocess and get TranspileModule JSON
    bool extractFunctions(HostLanguage lang,
                          const std::vector<std::filesystem::path>& sources,
                          const std::vector<std::string>& functionNames,
                          const SymbolTable& symbols,
                          TranspileModule& module,
                          std::vector<std::string>& errors);

    // Resolve extractor tool name for a language
    std::string extractorToolName(HostLanguage lang) const;

    // Filter a TranspileModule to only include functions reachable from a named pipeline
    bool filterByPipeline(const std::string& pipelineName,
                          const SymbolTable& symbols,
                          TranspileModule& module,
                          std::vector<std::string>& errors);

    // Emit output using the appropriate language emitter
    EmitResult emitOutput(HostLanguage lang, const TranspileModule& module);
};

} // namespace topo::transpile

#endif // TOPO_TRANSPILE_TRANSPILEDRIVER_H
