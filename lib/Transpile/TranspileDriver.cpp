#include "topo/Transpile/TranspileDriver.h"

#include "topo/Basic/Diagnostic.h"
#include "topo/Lang/LanguagePlugin.h"
#include "topo/Lang/EmitterFactory.h"
#include "topo/Lang/BuildDriverFactory.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Transpile/Emitter.h"
#include "topo/Transpile/TranspileModelJson.h"
#include "topo/Transpile/TranspileTraversal.h"
#include "topo/Transpile/TopoSourceBuilder.h"
#include "topo/Transpile/AdapterResolver.h"
#include "topo/Platform/Process.h"
#include "topo/Platform/ToolResolution.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <queue>
#include <set>
#include <sstream>

namespace topo::transpile {

// ---------------------------------------------------------------------------
// Post-transpile verification (pure aggregate over the module)
// ---------------------------------------------------------------------------

namespace {
void tallyFidelity(Fidelity f, FidelityBreakdown& b) {
    switch (f) {
    case Fidelity::Source:    ++b.source; break;
    case Fidelity::Recovered: ++b.recovered; break;
    case Fidelity::Inferred:  ++b.inferred; break;
    }
}

void mergeUnsupportedSummaries(std::vector<std::string>& nodes,
                               const std::vector<std::string>& summaries) {
    // Extractors may repeat an AST placeholder in the legacy function summary.
    // Match occurrences, not a set: two distinct nodes with the same description
    // are still two unsupported constructs.
    std::multiset<std::string> remaining(nodes.begin(), nodes.end());
    for (const auto& summary : summaries) {
        auto found = remaining.find(summary);
        if (found != remaining.end()) remaining.erase(found);
        else nodes.push_back(summary);
    }
}

void appendUnsupported(TranspileVerification& verification, const std::string& owner,
                       std::vector<std::string> nodes, const std::vector<std::string>& summaries) {
    mergeUnsupportedSummaries(nodes, summaries);
    if (nodes.empty()) return;
    verification.totalUnsupported += static_cast<int>(nodes.size());
    verification.perFunction.push_back(FunctionUnsupported{owner, std::move(nodes)});
}
} // namespace

TranspileVerification verifyModule(const TranspileModule& module) {
    TranspileVerification v;

    for (const auto& fn : module.functions) {
        std::vector<std::string> nodes;
        visitStatements(fn.body, [&](const Expr& expr) {
            if (expr.kind() == Expr::Kind::Unsupported)
                nodes.push_back(static_cast<const UnsupportedExpr&>(expr).description);
        });
        appendUnsupported(v, fn.qualifiedName, std::move(nodes), fn.unsupported);
        tallyFidelity(fn.fidelity, v.fidelity);
    }

    for (const auto& ty : module.types) {
        std::vector<std::string> nodes;
        auto collect = [&](const Expr& expr) {
            if (expr.kind() == Expr::Kind::Unsupported)
                nodes.push_back(static_cast<const UnsupportedExpr&>(expr).description);
        };
        tallyFidelity(ty.fidelity, v.fidelity);
        for (const auto& fld : ty.fields) {
            tallyFidelity(fld.fidelity, v.fidelity);
            visitExpr(fld.init.get(), collect);
        }
        visitStatements(ty.initializers, collect);
        appendUnsupported(v, ty.qualifiedName + "::<initializers>", std::move(nodes), ty.unsupported);
    }

    return v;
}

GateDecision applyVerificationGate(const TranspileVerification& v,
                                   std::optional<int> verifyMaxUnsupported) {
    GateDecision d;
    if (!verifyMaxUnsupported.has_value()) {
        return d; // no gate configured
    }
    int limit = *verifyMaxUnsupported;
    if (v.totalUnsupported > limit) {
        std::ostringstream msg;
        msg << "post-transpile verification failed: " << v.totalUnsupported
            << " unsupported construct(s) across " << v.perFunction.size()
            << " function(s) exceeds the configured limit of " << limit;
        d.failed = true;
        d.error = msg.str();
    }
    return d;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

TranspileResult TranspileDriver::run(const TranspileRequest& request) {
    // `.topo`-source mode (M4): route to the dedicated pipeline. The
    // host-source path below is left exactly as it was.
    if (request.fromTopoSource) {
        return runFromTopoSource(request);
    }

    TranspileResult result;

    // 1. Parse .topo file
    SymbolTable symbols;
    std::vector<std::string> parseErrors;
    if (!parseTopoFile(request.topoFile, symbols, parseErrors)) {
        result.warnings = std::move(parseErrors);
        return result;
    }

    // 2. Determine function list (all from SymbolTable if not specified)
    std::vector<std::string> functionNames = request.functions;
    if (functionNames.empty()) {
        for (const auto& [name, _] : symbols.functions()) {
            functionNames.push_back(name);
        }
    }

    if (functionNames.empty()) {
        result.warnings.push_back("no functions found in .topo declarations");
        return result;
    }

    // 3. Extract function bodies via language-specific subprocess
    TranspileModule module;
    std::vector<std::string> extractErrors;
    if (!extractFunctions(request.sourceLanguage, request.javaProjectPath, request.sourceFiles, functionNames, symbols, module, extractErrors)) {
        result.warnings = std::move(extractErrors);
        return result;
    }

    // Bind the extracted model to the declaration's effective boundaries.
    // Use resolved qualified identity where the extractor provides one.
    for (auto& type : module.types) {
        if (const auto* declaration = symbols.findClassSymbol(type.qualifiedName)) {
            type.isExternal = declaration->isExternal;
            type.externalOwner = declaration->externalOwner;
        }
    }
    for (auto& function : module.functions) {
        const auto& name = function.resolvedQualifiedName.empty()
            ? function.qualifiedName : function.resolvedQualifiedName;
        if (const auto* declaration = symbols.findFunction(name)) {
            function.isExternal = declaration->isExternal;
            function.owningType = declaration->owningType;
            function.externalOwner = declaration->externalOwner;
        } else if (const auto* owner = symbols.externalTypeForMember(name)) {
            function.isExternal = true;
            function.owningType = owner->qualifiedName;
            function.externalOwner = owner->externalOwner;
        }
    }

    // 3b. Apply pipeline filter if requested
    if (!request.pipelineName.empty()) {
        std::vector<std::string> filterErrors;
        if (!filterByPipeline(request.pipelineName, symbols, module, filterErrors)) {
            result.warnings = std::move(filterErrors);
            return result;
        }
        for (auto& w : filterErrors) {
            result.warnings.push_back(std::move(w));
        }
    }

    // 4. Emit output in the target language
    auto emitRes = emitOutput(request.targetLanguage, module);
    result.verification = verifyModule(module);
    result.unsupportedCount = result.verification.totalUnsupported;
    for (auto& e : emitRes.errors) {
        result.errors.push_back(e.location + ": " + e.reason);
    }
    if (emitRes.code.empty()) {
        if (result.errors.empty()) result.errors.push_back("emitter produced empty output");
        return result;
    }
    std::string output = std::move(emitRes.code);

    // 5. Write output file
    namespace fs = std::filesystem;
    fs::create_directories(request.outputDir);

    // Derive output filename from the .topo file stem
    std::string stem = request.topoFile.stem().string();
    std::string extension = ".cpp"; // default
    auto* extPlugin = lang::getPlugin(request.targetLanguage);
    if (extPlugin && extPlugin->emitterFactory()) {
        extension = extPlugin->emitterFactory()->fileExtension();
    }
    fs::path outputPath = request.outputDir / (stem + extension);

    {
        std::ofstream ofs(outputPath);
        if (!ofs) {
            result.warnings.push_back("failed to write output file: " + outputPath.string());
            return result;
        }
        ofs << output;
    }
    result.outputFiles.push_back(outputPath.string());

    // 6. Structured post-transpile verification (pure aggregate)
    result.verification = verifyModule(module);
    result.unsupportedCount = result.verification.totalUnsupported;

    // Collect warnings for unsupported constructs
    for (const auto& fu : result.verification.perFunction) {
        for (const auto& u : fu.constructs) {
            result.warnings.push_back(fu.qualifiedName + ": unsupported construct: " + u);
        }
    }

    result.success = result.errors.empty();

    // 7. Configurable gate: when the caller set a tolerance, the driver
    // itself enforces it (not just the CLI), so library/test consumers get
    // the same guarantee.
    GateDecision gate =
        applyVerificationGate(result.verification, request.verifyMaxUnsupported);
    if (gate.failed) {
        result.errors.push_back(gate.error);
        result.success = false;
    }

    return result;
}

// ---------------------------------------------------------------------------
// `.topo`-source pipeline (M4)
//
// Builds the TranspileModule directly from the parsed `.topo` AST:
//   - TopoSourceBuilder generates composite-function bodies from logic blocks
//     and marks leaf functions unresolved (M2);
//   - AdapterResolver fills leaf bodies from the assembled adapter registry,
//     or applies the traceable degradation when no adapter resolves (M3/M5).
// Then it reuses the same emitter dispatch + output-writing + verification
// gate as the host-source path. The host-source path is untouched.
// ---------------------------------------------------------------------------

TranspileResult TranspileDriver::runFromTopoSource(const TranspileRequest& request) {
    TranspileResult result;
    namespace fs = std::filesystem;

    // 1. Read + parse the `.topo` file into an AST (not just a SymbolTable —
    // TopoSourceBuilder needs the FnDecl / FnLogicBlock nodes).
    std::ifstream file(request.topoFile);
    if (!file) {
        result.warnings.push_back("cannot open file: " + request.topoFile.string());
        return result;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    DiagnosticEngine diag;
    Lexer lexer(source, request.topoFile.string(), diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    if (diag.hasErrors() || !ast) {
        for (const auto& d : diag.diagnostics()) {
            result.warnings.push_back(
                request.topoFile.string() + ":" + std::to_string(d.location.line) +
                ":" + std::to_string(d.location.column) + ": " + d.message);
        }
        if (result.warnings.empty()) {
            result.warnings.push_back("failed to parse " + request.topoFile.string());
        }
        return result;
    }

    // 2. Build the TranspileModule from the `.topo` AST (M2).
    TopoSourceBuilder builder;
    BuildResult build = builder.build(static_cast<const TopoFile&>(*ast));
    for (auto& w : build.warnings) result.warnings.push_back(std::move(w));
    if (!build.success) {
        for (auto& e : build.errors) result.warnings.push_back(std::move(e));
        if (build.errors.empty()) {
            result.warnings.push_back("`.topo`-source builder produced no module");
        }
        return result;
    }
    TranspileModule module = std::move(build.module);

    // 2b. Restrict to requested functions, if any (`--functions`).
    if (!request.functions.empty()) {
        std::set<std::string> keep(request.functions.begin(), request.functions.end());
        module.functions.erase(
            std::remove_if(module.functions.begin(), module.functions.end(),
                           [&](const TranspileFunction& fn) {
                               return keep.find(fn.qualifiedName) == keep.end();
                           }),
            module.functions.end());
    }

    if (module.functions.empty()) {
        result.warnings.push_back("no functions found in .topo declarations");
        return result;
    }

    // `--pipeline` filtering is a host-source-path feature (it walks the
    // SymbolTable call graph from the extractor). The `.topo`-source path
    // does not support it; surface a warning rather than silently ignoring.
    if (!request.pipelineName.empty()) {
        result.warnings.push_back(
            "--pipeline is ignored in .topo-source mode (--from topo)");
    }

    // 3. Fill leaf functions from the adapter registry (M3/M5). The builtin
    // adapter source is always assembled; the topo-app source is loaded from
    // `adapterManifestPath` when supplied; tpm sources from
    // `tpmAdapterManifests` (priority tpm > topo-app > builtin).
    AdapterRegistry registry = assembleAdapterRegistry(
        request.adapterManifestPath, request.tpmAdapterManifests,
        result.warnings);
    AdapterResolver resolver(registry);
    ResolveStats stats = resolver.resolve(module, request.targetLanguage);
    for (auto& w : stats.warnings) result.warnings.push_back(std::move(w));

    // 4. Emit output in the target language (existing emitter dispatch).
    auto emitRes = emitOutput(request.targetLanguage, module);
    result.verification = verifyModule(module);
    result.unsupportedCount = result.verification.totalUnsupported;
    for (auto& e : emitRes.errors) {
        result.errors.push_back(e.location + ": " + e.reason);
    }
    if (emitRes.code.empty()) {
        if (result.errors.empty()) result.errors.push_back("emitter produced empty output");
        return result;
    }

    // 5. Write output file (filename derives from the .topo stem, extension
    // from the target emitter factory — same as the host-source path).
    fs::create_directories(request.outputDir);
    std::string stem = request.topoFile.stem().string();
    std::string extension = ".cpp";
    auto* extPlugin = lang::getPlugin(request.targetLanguage);
    if (extPlugin && extPlugin->emitterFactory()) {
        extension = extPlugin->emitterFactory()->fileExtension();
    }
    fs::path outputPath = request.outputDir / (stem + extension);
    {
        std::ofstream ofs(outputPath);
        if (!ofs) {
            result.warnings.push_back("failed to write output file: " +
                                      outputPath.string());
            return result;
        }
        ofs << emitRes.code;
    }
    result.outputFiles.push_back(outputPath.string());

    // 5b. Graduation manifest: copy the source `.topo` into the output
    // directory and write a Topo.toml so the result is a drop-in project
    // (consumer can `topo build` from outputDir without rewriting build
    // config). Only emitted in `.topo`-source mode, where the output is
    // intended as a self-contained project skeleton.
    {
        fs::path topoCopy = request.outputDir / (stem + ".topo");
        std::error_code ec;
        if (topoCopy != request.topoFile) {
            fs::copy_file(
                request.topoFile, topoCopy,
                fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            result.warnings.push_back("failed to copy .topo into output dir: " +
                                      ec.message());
        } else if (topoCopy != request.topoFile) {
            result.outputFiles.push_back(topoCopy.string());
        }

        const char* langStr = "cpp";
        switch (request.targetLanguage) {
        case HostLanguage::Cpp:        langStr = "cpp"; break;
        case HostLanguage::Rust:       langStr = "rust"; break;
        case HostLanguage::Java:       langStr = "java"; break;
        case HostLanguage::Python:     langStr = "python"; break;
        case HostLanguage::TypeScript: langStr = "typescript"; break;
        case HostLanguage::Mixed:      langStr = "mixed"; break;
        }

        fs::path tomlPath = request.outputDir / "Topo.toml";
        std::ofstream ofs(tomlPath);
        if (!ofs) {
            result.warnings.push_back("failed to write Topo.toml: " +
                                      tomlPath.string());
        } else {
            ofs << "[topo]\n"
                << "root = \"" << stem << ".topo\"\n"
                << "\n"
                << "[build]\n"
                << "language = \"" << langStr << "\"\n"
                << "sources = [\"" << stem << extension << "\"]\n";
            result.outputFiles.push_back(tomlPath.string());
        }
    }

    // 6. Structured post-transpile verification (pure aggregate) + warnings.
    result.verification = verifyModule(module);
    result.unsupportedCount = result.verification.totalUnsupported;
    for (const auto& fu : result.verification.perFunction) {
        for (const auto& u : fu.constructs) {
            result.warnings.push_back(fu.qualifiedName +
                                      ": unsupported construct: " + u);
        }
    }

    result.success = result.errors.empty();

    // 7. Configurable verification gate (same as the host-source path).
    GateDecision gate =
        applyVerificationGate(result.verification, request.verifyMaxUnsupported);
    if (gate.failed) {
        result.errors.push_back(gate.error);
        result.success = false;
    }

    return result;
}

// ---------------------------------------------------------------------------
// .topo parsing (reuses existing compiler frontend)
// ---------------------------------------------------------------------------

bool TranspileDriver::parseTopoFile(const std::filesystem::path& topoFile,
                                    SymbolTable& symbols,
                                    std::vector<std::string>& errors) {
    std::ifstream file(topoFile);
    if (!file) {
        errors.push_back("cannot open file: " + topoFile.string());
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    DiagnosticEngine diag;
    Lexer lexer(source, topoFile.string(), diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();

    if (diag.hasErrors()) {
        for (const auto& d : diag.diagnostics()) {
            std::string msg = topoFile.string() + ":" + std::to_string(d.location.line) + ":" +
                              std::to_string(d.location.column) + ": " + d.message;
            errors.push_back(std::move(msg));
        }
        return false;
    }

    SemanticAnalyzer sema(diag);
    symbols = sema.analyze(static_cast<const TopoFile&>(*ast));

    if (diag.hasErrors()) {
        for (const auto& d : diag.diagnostics()) {
            std::string msg = topoFile.string() + ": semantic error: " + d.message;
            errors.push_back(std::move(msg));
        }
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Extractor subprocess
// ---------------------------------------------------------------------------

bool TranspileDriver::extractFunctions(HostLanguage lang,
                                       const std::filesystem::path& javaProjectPath,
                                       const std::vector<std::filesystem::path>& sources,
                                       const std::vector<std::string>& functionNames,
                                       const SymbolTable& symbols,
                                       TranspileModule& module,
                                       std::vector<std::string>& errors) {
    std::string toolName = extractorToolName(lang);

    // Build JSON request for the extractor
    nlohmann::json request = nlohmann::json::object();
    if (!javaProjectPath.empty()) {
        if (lang != HostLanguage::Java) {
            errors.push_back("Java project model requires Java source language");
            return false;
        }
        try {
            std::ifstream modelFile(javaProjectPath);
            if (!modelFile) throw std::runtime_error("cannot open project model");
            auto model = nlohmann::json::parse(modelFile);
            if (!model.is_object()) throw std::runtime_error("expected JSON object");
            request["sourceLevel"] = model.value("sourceLevel", std::string("25"));
            for (const char* key : {"classpath", "sourcepath", "files"}) {
                // Explicit sources fully replace the model's file selection,
                // including stale or unavailable entries in that selection.
                if (std::string(key) == "files" && !sources.empty()) continue;
                auto paths = model.value(key, nlohmann::json::array());
                if (!paths.is_array()) throw std::runtime_error(std::string(key) + " must be an array");
                request[key] = nlohmann::json::array();
                for (const auto& value : paths) {
                    auto path = std::filesystem::path(value.get<std::string>());
                    if (path.empty()) throw std::runtime_error(std::string(key) + " contains an empty path");
                    if (path.is_relative()) path = javaProjectPath.parent_path() / path;
                    path = std::filesystem::absolute(path).lexically_normal();
                    if (!std::filesystem::exists(path)) throw std::runtime_error("path does not exist: " + path.string());
                    request[key].push_back(path.string());
                }
            }
        } catch (const std::exception& e) {
            errors.push_back("invalid Java project model '" + javaProjectPath.string() + "': " + e.what());
            return false;
        }
    } else if (lang == HostLanguage::Java) {
        request["sourceLevel"] = "25";
    }

    nlohmann::json filesArr = nlohmann::json::array();
    for (const auto& src : sources) {
        filesArr.push_back(src.string());
    }
    if (!sources.empty() || !request.contains("files"))
        request["files"] = std::move(filesArr);
    if (request["files"].empty()) {
        errors.push_back("no source files supplied (use --sources or Java project model files)");
        return false;
    }

    nlohmann::json funcsArr = nlohmann::json::array();
    for (const auto& fn : functionNames) {
        funcsArr.push_back(fn);
    }
    request["functions"] = std::move(funcsArr);

    // Serialize SymbolTable function signatures so the extractor knows
    // the declared prototypes.
    nlohmann::json symJson = nlohmann::json::object();
    for (const auto& [name, sym] : symbols.functions()) {
        nlohmann::json entry;
        entry["qualifiedName"] = sym.qualifiedName;
        entry["simpleName"] = sym.simpleName;
        symJson[name] = std::move(entry);
    }
    request["symbolTable"] = std::move(symJson);

    std::string requestStr = request.dump();

    // Ensure a relocated extractor can load libclang/libLLVM from the resolved
    // (possibly BYO) toolchain: prepend its lib dir to the child's loader path.
    // No-op on the build host and when no toolchain is resolved.
    platform::ensureLLVMLoaderPathForChildren();

    // Spawn the extractor: pipe JSON on stdin, read TranspileModule JSON on stdout
    platform::PipedProcess proc;
    if (!proc.start(toolName, {})) {
        errors.push_back("extractor tool '" + toolName +
                         "' not found. "
                         "Ensure it is installed and available on PATH.\n"
                         "  For C++:        build and install topo-extract-cpp\n"
                         "  For Rust:       build and install topo-extract-rust\n"
                         "  For Java:       build and install topo-extract-java\n"
                         "  For TypeScript: build and install topo-extract-typescript\n"
                         "  For Python:     build and install topo-extract-python");
        return false;
    }

    // Write request JSON
    if (!proc.write(requestStr.data(), requestStr.size())) {
        errors.push_back("failed to write request to " + toolName);
        proc.stop();
        return false;
    }
    // Close the parent's write-end of stdin so the child's read-to-EOF
    // loop can unblock and begin producing output. Without this, both
    // sides deadlock: parent blocks on read, child blocks on stdin.
    proc.closeStdin();

    // Read response JSON from stdout
    std::string responseStr;
    char buf[4096];
    while (true) {
        size_t n = proc.read(buf, sizeof(buf));
        if (n == 0) break;
        responseStr.append(buf, n);
    }

    proc.stop();

    if (responseStr.empty()) {
        errors.push_back(toolName + " produced no output");
        return false;
    }

    // Parse response
    try {
        nlohmann::json responseJson = nlohmann::json::parse(responseStr);
        if (lang == HostLanguage::Java &&
            (!responseJson.contains("schemaVersion") ||
             !responseJson["schemaVersion"].is_number_integer() ||
             responseJson["schemaVersion"].get<int>() != 2 ||
             !responseJson.contains("analysisComplete") ||
             !responseJson["analysisComplete"].is_boolean() ||
             !responseJson.contains("diagnostics") ||
             !responseJson["diagnostics"].is_array())) {
            errors.push_back(toolName + " requires Java schemaVersion 2 with analysisComplete and diagnostics; rebuild the Java extractor");
            return false;
        }
        module = deserializeModule(responseJson);
        if (lang == HostLanguage::Java) {
            // Java wire names follow JDT's dotted convention; the shared
            // emitter namespace contract uses ::. Keep resolved provenance
            // untouched so diagnostics still match the original source.
            auto portableName = [](const std::string& name) {
                std::string out;
                for (char c : name) {
                    if (c == '.') out += "::";
                    else out += c;
                }
                return out;
            };
            for (auto& function : module.functions)
                function.qualifiedName = portableName(function.qualifiedName);
            for (auto& type : module.types)
                type.qualifiedName = portableName(type.qualifiedName);
        }
        bool analysisFailed = false;
        if (responseJson.contains("diagnostics")) {
            for (const auto& diagnostic : responseJson.at("diagnostics")) {
                if (diagnostic.value("severity", "error") != "error") continue;
                errors.push_back(diagnostic.value("file", "") + ":" +
                    std::to_string(diagnostic.value("line", 0)) + ": " +
                    diagnostic.value("message", "extractor analysis failed"));
                analysisFailed = true;
            }
        }
        if (responseJson.contains("analysisComplete") &&
            !responseJson.at("analysisComplete").get<bool>()) {
            if (!analysisFailed) errors.push_back(toolName + " reported incomplete analysis");
            analysisFailed = true;
        }
        if (analysisFailed) return false;

        // Per-extractor degradation signal — currently emitted by
        // topo-extract-java when JDT binding resolution OOMs and the
        // process falls back to binding-disabled parsing for the rest
        // of the run. Surfaced through `errors` so the user-visible
        // topo-check / topo-transpile report does not silently treat a
        // degraded pass as a high-fidelity verification; to avoid silent
        // degradation this is reported even on successful extraction;
        // callers may choose warn vs fail.
        if (responseJson.contains("runDegraded") &&
            responseJson["runDegraded"].is_boolean() &&
            responseJson["runDegraded"].get<bool>()) {
            std::string msg = "warning: " + toolName + " ran in degraded mode";
            if (responseJson.contains("degradationReason") &&
                responseJson["degradationReason"].is_string()) {
                msg += ": " + responseJson["degradationReason"].get<std::string>();
            }
            errors.push_back(msg);
        }
    } catch (const nlohmann::json::exception& e) {
        errors.push_back("failed to parse " + toolName + " response: " + e.what());
        return false;
    }

    return true;
}

std::string TranspileDriver::extractorToolName(HostLanguage lang) const {
    auto* plugin = lang::getPlugin(lang);
    if (!plugin || !plugin->buildDriverFactory()) {
        plugin = lang::getPlugin(HostLanguage::Cpp);
        if (!plugin || !plugin->buildDriverFactory()) return "topo-extract-cpp";
    }
    return plugin->buildDriverFactory()->extractorToolName();
}

// ---------------------------------------------------------------------------
// Emitter dispatch
// ---------------------------------------------------------------------------

EmitResult TranspileDriver::emitOutput(HostLanguage lang, const TranspileModule& module) {
    auto* plugin = lang::getPlugin(lang);
    if (!plugin || !plugin->emitterFactory()) {
        // Fallback for Mixed or unknown -- try Cpp
        plugin = lang::getPlugin(HostLanguage::Cpp);
        if (!plugin || !plugin->emitterFactory()) return {};
    }
    auto emitter = plugin->emitterFactory()->createEmitter();
    return emitter->emit(module);
}

// ---------------------------------------------------------------------------
// Pipeline filter
// ---------------------------------------------------------------------------

bool TranspileDriver::filterByPipeline(const std::string& pipelineName,
                                       const SymbolTable& symbols,
                                       TranspileModule& module,
                                       std::vector<std::string>& errors) {
    // Look up the pipeline in logicBlocks
    const LogicBlockEntry* pipeline = symbols.findLogicBlock(pipelineName);
    if (!pipeline) {
        // Try unqualified match: iterate all logicBlocks and match simpleName
        for (const auto& [qname, block] : symbols.logicBlocks()) {
            if (block.simpleName == pipelineName && block.isPipeline) {
                pipeline = &block;
                break;
            }
        }
    }
    if (!pipeline) {
        errors.push_back("pipeline '" + pipelineName + "' not found in .topo declarations");
        return false;
    }
    if (!pipeline->isPipeline) {
        errors.push_back("logic block '" + pipelineName + "' is not a pipeline");
        return false;
    }

    // Build the call graph from the SymbolTable's callSites for transitive closure
    std::unordered_map<std::string, std::vector<std::string>> callGraph;
    for (const auto& cs : symbols.callSites()) {
        callGraph[cs.caller].push_back(cs.callee);
    }

    // Seed: direct calledFunctions of the pipeline
    std::set<std::string> reachable;
    std::queue<std::string> worklist;
    for (const auto& fn : pipeline->calledFunctions) {
        if (reachable.insert(fn).second) {
            worklist.push(fn);
        }
    }

    // BFS transitive closure over call graph
    while (!worklist.empty()) {
        std::string current = worklist.front();
        worklist.pop();
        auto it = callGraph.find(current);
        if (it != callGraph.end()) {
            for (const auto& callee : it->second) {
                if (reachable.insert(callee).second) {
                    worklist.push(callee);
                }
            }
        }
    }

    if (reachable.empty()) {
        errors.push_back("pipeline '" + pipelineName + "' has no referenced functions");
        return true; // not a fatal error; module will just be empty
    }

    // Filter functions: keep only those whose qualifiedName is in the reachable set
    module.functions.erase(
        std::remove_if(module.functions.begin(), module.functions.end(),
                       [&](const TranspileFunction& fn) {
                           return reachable.find(fn.qualifiedName) == reachable.end();
                       }),
        module.functions.end());

    // Collect type names referenced by the remaining functions' parameters and return types
    std::set<std::string> usedTypes;
    auto collectTypeName = [&](const TypeNode& tn) {
        if (!tn.nameParts.empty()) {
            usedTypes.insert(tn.toString());
        }
        for (const auto& arg : tn.templateArgs) {
            if (!arg.nameParts.empty()) {
                usedTypes.insert(arg.toString());
            }
        }
    };

    for (const auto& fn : module.functions) {
        collectTypeName(fn.returnType);
        for (const auto& p : fn.params) {
            collectTypeName(p.type);
        }
    }

    // Filter types: keep only those whose qualifiedName is referenced
    module.types.erase(
        std::remove_if(module.types.begin(), module.types.end(),
                       [&](const TranspileType& ty) {
                           // Check both qualified name and simple name (last component)
                           if (usedTypes.count(ty.qualifiedName)) return false;
                           auto pos = ty.qualifiedName.rfind("::");
                           if (pos != std::string::npos) {
                               std::string simple = ty.qualifiedName.substr(pos + 2);
                               if (usedTypes.count(simple)) return false;
                           }
                           return true;
                       }),
        module.types.end());

    return true;
}

} // namespace topo::transpile
