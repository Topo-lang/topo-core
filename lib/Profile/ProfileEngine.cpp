// ProfileEngine — zero-LLVM core of `topo-prof`. See ProfileEngine.h.
//
// This is a byte-for-byte port of the analyze / profile / hints
// orchestration formerly inlined in topo-llvm/tools/topo-prof/main.cpp.
// The ONLY change in behaviour is structural: hard-coded std::cout/std::cerr
// were replaced by the caller-supplied `out`/`err` streams (the shim passes
// std::cout/std::cerr, so observable output is unchanged), and the LLVM
// `buildTTIMap` was lifted behind the TTIProvider seam. The stage→node
// grouping now reuses topo::analysis::groupNodesByStage, which is a
// structurally identical (same std::map<int,...> insertion grouping over the
// same analysis.stages container) zero-LLVM helper.

#include "topo/Profile/ProfileEngine.h"

#include "topo/Analysis/PipelineAnalysis.h"
#include "topo/Basic/Diagnostic.h"
#include "topo/Sema/ImportResolver.h"
#include "topo/Sema/SemanticAnalyzer.h"

#include <nlohmann/json.hpp>

#define TOML_HEADER_ONLY 1
#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace topo {
namespace profile {

// ============================================================
// Analyze subcommand
// ============================================================

int runAnalyze(const std::string& projectDir, const std::string& focus,
                const std::string& format, std::ostream& out, std::ostream& err) {
    fs::path tomlPath = fs::path(projectDir) / "Topo.toml";
    if (!fs::exists(tomlPath)) {
        err << "error: Topo.toml not found in " << projectDir << "\n";
        return 1;
    }

    auto result = toml::parse_file(tomlPath.string());
    if (!result) {
        err << "error: " << result.error() << "\n";
        return 1;
    }
    auto& tbl = result.table();

    auto topoRoot = tbl["topo"]["root"].value<std::string>();
    if (!topoRoot) {
        err << "error: [topo].root not found in Topo.toml\n";
        return 1;
    }

    fs::path baseDir = tomlPath.parent_path();
    std::string rootPath = (baseDir / *topoRoot).string();

    // Parse and analyze .topo files
    topo::DiagnosticEngine diag;
    topo::ImportResolver resolver(diag);
    auto modules = resolver.resolve({rootPath});

    if (diag.hasErrors()) {
        diag.print(err);
        return 1;
    }

    // Build symbol table
    std::unordered_map<std::string, topo::SymbolTable> symbolCache;
    topo::SymbolTable symbols;

    for (const auto& mod : modules) {
        topo::SymbolTable importedSymbols;
        for (const auto& directive : mod.imports) {
            auto it = symbolCache.find(directive.resolvedPath);
            if (it != symbolCache.end()) importedSymbols.mergeSelected(it->second, directive.selectedSymbols);
        }

        topo::DiagnosticEngine fileDiag;
        topo::SemanticAnalyzer sema(fileDiag);
        auto fileSymbols = sema.analyze(static_cast<const topo::TopoFile&>(*mod.ast), importedSymbols);

        if (fileDiag.hasErrors()) {
            fileDiag.print(err);
            return 1;
        }

        symbolCache[mod.path] = fileSymbols;
    }

    symbols = topo::SymbolTable{};
    for (const auto& mod : modules) {
        auto it = symbolCache.find(mod.path);
        if (it != symbolCache.end()) symbols.mergeFrom(it->second, true);
    }

    // Output analysis
    bool jsonOutput = (format == "json");
    nlohmann::json jsonRoot;
    nlohmann::json jsonPipelines = nlohmann::json::array();

    for (const auto& [name, logicBlock] : symbols.logicBlocks()) {
        if (!logicBlock.isPipeline || !logicBlock.pipelineAnalysis) continue;

        if (focus == "pipeline" || focus.empty()) {
            const auto& analysis = *logicBlock.pipelineAnalysis;

            if (!jsonOutput) {
                out << "Pipeline: " << logicBlock.qualifiedName << "\n";
            }

            nlohmann::json jsonPipeline;
            jsonPipeline["name"] = logicBlock.qualifiedName;

            // Group by stage (reuses zero-LLVM topo::analysis helper —
            // identical std::map<int,...> grouping over analysis.stages).
            auto stageGroups = topo::analysis::groupNodesByStage(analysis);

            nlohmann::json jsonStages = nlohmann::json::array();

            for (const auto& [stage, nodes] : stageGroups) {
                std::string nodesStr;
                for (size_t i = 0; i < nodes.size(); ++i) {
                    if (i > 0) nodesStr += ", ";
                    nodesStr += nodes[i];
                }

                bool canParallel = nodes.size() >= 2;
                std::string decision = canParallel ? "parallel" : "sequential";
                decision += " (" + std::to_string(nodes.size()) + " node" + (nodes.size() > 1 ? "s" : "") + ")";

                if (!jsonOutput) {
                    out << "  Stage " << stage << ": [" << nodesStr << "]"
                        << "  -> " << decision << "\n";
                }

                nlohmann::json jsonStage;
                jsonStage["stage"] = stage;
                jsonStage["nodes"] = nodes;
                jsonStage["parallel"] = canParallel;
                jsonStages.push_back(jsonStage);
            }

            jsonPipeline["stages"] = jsonStages;
            jsonPipelines.push_back(jsonPipeline);

            if (!jsonOutput) out << "\n";
        }
    }

    if (jsonOutput) {
        jsonRoot["pipelines"] = jsonPipelines;
        out << jsonRoot.dump(2) << "\n";
    }

    return 0;
}

// ============================================================
// Load runtime samples from JSON file
// ============================================================

static std::map<std::string, uint64_t> loadSamples(const std::string& samplesPath, std::ostream& err) {
    std::map<std::string, uint64_t> samples;

    std::ifstream file(samplesPath);
    if (!file) {
        err << "error: cannot open samples file: " << samplesPath << "\n";
        return samples;
    }

    nlohmann::json data;
    try {
        data = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        err << "error: invalid JSON in samples file: " << e.what() << "\n";
        return samples;
    }

    if (!data.is_object()) {
        err << "error: samples JSON must be an object (function → nanoseconds)\n";
        return samples;
    }

    for (auto& [key, val] : data.items()) {
        if (val.is_number_unsigned()) {
            samples[key] = val.get<uint64_t>();
        } else if (val.is_number_integer()) {
            samples[key] = static_cast<uint64_t>(val.get<int64_t>());
        } else if (val.is_number_float()) {
            samples[key] = static_cast<uint64_t>(val.get<double>());
        }
    }

    return samples;
}

// ============================================================
// Extended samples (structured format with optional fields)
// ============================================================

struct ExtendedSample {
    uint64_t runtimeNs = 0;
    std::optional<uint64_t> observedCardinalityP95;
    std::optional<double> l1MissRate;
};

static std::map<std::string, ExtendedSample> loadExtendedSamples(const std::string& samplesPath,
                                                                  std::ostream& err) {
    std::map<std::string, ExtendedSample> samples;

    std::ifstream file(samplesPath);
    if (!file) {
        err << "error: cannot open samples file: " << samplesPath << "\n";
        return samples;
    }

    nlohmann::json data;
    try {
        data = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        err << "error: invalid JSON in samples file: " << e.what() << "\n";
        return samples;
    }

    if (!data.is_object()) {
        err << "error: samples JSON must be an object\n";
        return samples;
    }

    for (auto& [key, val] : data.items()) {
        ExtendedSample sample;

        if (val.is_number()) {
            // Legacy flat format: { "func": nanoseconds }
            if (val.is_number_unsigned())
                sample.runtimeNs = val.get<uint64_t>();
            else if (val.is_number_integer())
                sample.runtimeNs = static_cast<uint64_t>(val.get<int64_t>());
            else
                sample.runtimeNs = static_cast<uint64_t>(val.get<double>());
        } else if (val.is_object()) {
            // Structured format
            if (val.contains("runtime_ns") && val["runtime_ns"].is_number()) {
                sample.runtimeNs = static_cast<uint64_t>(val["runtime_ns"].get<double>());
            }
            if (val.contains("observed_cardinality_p95") && val["observed_cardinality_p95"].is_number()) {
                sample.observedCardinalityP95 = static_cast<uint64_t>(val["observed_cardinality_p95"].get<double>());
            }
            if (val.contains("l1_miss_rate") && val["l1_miss_rate"].is_number()) {
                sample.l1MissRate = val["l1_miss_rate"].get<double>();
            }
        } else {
            continue; // Skip unrecognized value types
        }

        samples[key] = sample;
    }

    return samples;
}

// ============================================================
// Suggestion logic
// ============================================================

static std::string computeSuggestion(uint64_t ttiEstimate, uint64_t runtimeAvgNs) {
    // If runtime is very low (< 100ns) and there would be parallel overhead
    if (runtimeAvgNs < 100) {
        return "consider_sequential";
    }

    // Compute delta percentage
    if (ttiEstimate > 0) {
        double deltaPct = (static_cast<double>(runtimeAvgNs) - static_cast<double>(ttiEstimate)) /
                          static_cast<double>(ttiEstimate) * 100.0;
        if (deltaPct > 200.0) {
            return "investigate_overhead";
        }
    }

    return "keep_parallel";
}

// ============================================================
// Profile subcommand
// ============================================================

int runProfile(const std::string& samplesPath, const std::string& projectDir,
                const std::string& binaryPath, const std::string& format,
                const std::string& outputPath, TTIProvider* tti,
                std::ostream& out, std::ostream& err) {
    // Load runtime samples
    auto samples = loadSamples(samplesPath, err);
    if (samples.empty()) {
        err << "error: no valid samples loaded from " << samplesPath << "\n";
        return 1;
    }

    // Optionally load TTI estimates from project
    std::map<std::string, uint64_t> ttiMap;
    bool hasTTI = false;
    if (!projectDir.empty() && tti != nullptr) {
        ttiMap = tti->buildTTIMap(projectDir, err);
        hasTTI = !ttiMap.empty();
    }

    // Build report
    nlohmann::json report;
    if (!binaryPath.empty()) {
        report["binary"] = binaryPath;
    }
    report["status"] = "complete";

    nlohmann::json functions = nlohmann::json::array();

    for (const auto& [funcName, runtimeNs] : samples) {
        nlohmann::json entry;

        // Extract simple name (last component after ::)
        std::string simpleName = funcName;
        auto lastSep = funcName.rfind("::");
        if (lastSep != std::string::npos && lastSep + 2 < funcName.size()) {
            simpleName = funcName.substr(lastSep + 2);
        }

        entry["name"] = simpleName;
        entry["runtime_avg_ns"] = runtimeNs;

        if (hasTTI) {
            // Try to match by qualified name first, then by simple name
            uint64_t ttiEst = 0;
            bool found = false;
            auto it = ttiMap.find(funcName);
            if (it != ttiMap.end()) {
                ttiEst = it->second;
                found = true;
            } else {
                // Fallback: search by simple name suffix
                for (const auto& [ttiName, cost] : ttiMap) {
                    auto ttiLastSep = ttiName.rfind("::");
                    std::string ttiSimple = ttiName;
                    if (ttiLastSep != std::string::npos && ttiLastSep + 2 < ttiName.size()) {
                        ttiSimple = ttiName.substr(ttiLastSep + 2);
                    }
                    if (ttiSimple == simpleName) {
                        ttiEst = cost;
                        found = true;
                        break;
                    }
                }
            }

            if (found) {
                entry["tti_estimate"] = ttiEst;
                double deltaPct = 0.0;
                if (ttiEst > 0) {
                    deltaPct = (static_cast<double>(runtimeNs) - static_cast<double>(ttiEst)) /
                               static_cast<double>(ttiEst) * 100.0;
                }
                // Round to 1 decimal place
                deltaPct = std::round(deltaPct * 10.0) / 10.0;
                entry["delta_pct"] = deltaPct;
                entry["suggestion"] = computeSuggestion(ttiEst, runtimeNs);
            } else {
                entry["runtime_avg_ns"] = runtimeNs;
            }
        }

        functions.push_back(entry);
    }

    report["functions"] = functions;

    // Output
    bool jsonOutput = (format == "json");
    std::string jsonStr = report.dump(2);

    if (jsonOutput || outputPath.empty()) {
        // JSON mode always outputs JSON; text mode to stdout also outputs JSON
        // (the profile report is fundamentally structured data)
        if (!outputPath.empty()) {
            std::ofstream outFile(outputPath);
            outFile << jsonStr << "\n";
            err << "Report written to " << outputPath << "\n";
        } else {
            out << jsonStr << "\n";
        }
    } else {
        // Text mode with output file
        std::ofstream outFile(outputPath);
        outFile << jsonStr << "\n";
        err << "Report written to " << outputPath << "\n";
    }

    return 0;
}

// ============================================================
// Hints subcommand — compare declared data-aware hints vs runtime
// ============================================================

// Format a cardinality value with k/M suffix for human readability.
static std::string formatCardinality(int64_t value) {
    if (value <= 0) return "?";
    if (value >= 1000000 && value % 1000000 == 0) return std::to_string(value / 1000000) + "M";
    if (value >= 1000 && value % 1000 == 0) return std::to_string(value / 1000) + "k";
    return std::to_string(value);
}

// Convert AccessPattern enum to display string.
static const char* accessPatternStr(topo::AccessPattern ap) {
    switch (ap) {
    case topo::AccessPattern::Streaming: return "streaming";
    case topo::AccessPattern::Random: return "random";
    case topo::AccessPattern::Tiled: return "tiled";
    case topo::AccessPattern::GatherScatter: return "gather_scatter";
    default: return "none";
    }
}

struct HintsReportEntry {
    std::string function;
    std::string type;     // "cardinality" or "access-pattern"
    std::string severity; // "ok", "info", "warning"
    std::string message;
    std::string suggestion;

    // Cardinality-specific
    int64_t declaredMin = 0;
    int64_t declaredMax = 0;
    uint64_t observedP95 = 0;
    double deviationFactor = 0.0;

    // Access-pattern-specific
    std::string declaredPattern;
    double l1MissRate = 0.0;
};

int runHints(const std::string& samplesPath, const std::string& projectDir,
              const std::string& format, const std::string& outputPath,
              std::ostream& out, std::ostream& err) {
    // 1. Parse .topo files (reuse doAnalyze pattern)
    fs::path tomlPath = fs::path(projectDir) / "Topo.toml";
    if (!fs::exists(tomlPath)) {
        err << "error: Topo.toml not found in " << projectDir << "\n";
        return 1;
    }

    auto result = toml::parse_file(tomlPath.string());
    if (!result) {
        err << "error: " << result.error() << "\n";
        return 1;
    }
    auto& tbl = result.table();

    auto topoRoot = tbl["topo"]["root"].value<std::string>();
    if (!topoRoot) {
        err << "error: [topo].root not found in Topo.toml\n";
        return 1;
    }

    fs::path baseDir = tomlPath.parent_path();
    std::string rootPath = (baseDir / *topoRoot).string();

    topo::DiagnosticEngine diag;
    topo::ImportResolver resolver(diag);
    auto modules = resolver.resolve({rootPath});

    if (diag.hasErrors()) {
        diag.print(err);
        return 1;
    }

    std::unordered_map<std::string, topo::SymbolTable> symbolCache;
    topo::SymbolTable symbols;

    for (const auto& mod : modules) {
        topo::SymbolTable importedSymbols;
        for (const auto& directive : mod.imports) {
            auto it = symbolCache.find(directive.resolvedPath);
            if (it != symbolCache.end()) importedSymbols.mergeSelected(it->second, directive.selectedSymbols);
        }

        topo::DiagnosticEngine fileDiag;
        topo::SemanticAnalyzer sema(fileDiag);
        auto fileSymbols = sema.analyze(static_cast<const topo::TopoFile&>(*mod.ast), importedSymbols);

        if (fileDiag.hasErrors()) {
            fileDiag.print(err);
            return 1;
        }

        symbolCache[mod.path] = fileSymbols;
    }

    symbols = topo::SymbolTable{};
    for (const auto& mod : modules) {
        auto it = symbolCache.find(mod.path);
        if (it != symbolCache.end()) symbols.mergeFrom(it->second, true);
    }

    // 2. Load extended samples
    auto extSamples = loadExtendedSamples(samplesPath, err);
    if (extSamples.empty()) {
        err << "error: no valid samples loaded from " << samplesPath << "\n";
        return 1;
    }

    // 3. Compare hints vs runtime for each function
    std::vector<HintsReportEntry> entries;

    for (const auto& [funcName, funcSym] : symbols.functions()) {
        // Find matching sample by qualified name or simple name fallback
        const ExtendedSample* sample = nullptr;
        auto it = extSamples.find(funcSym.qualifiedName);
        if (it != extSamples.end()) {
            sample = &it->second;
        } else {
            // Fallback: search by simple name suffix
            for (const auto& [sampleName, s] : extSamples) {
                auto lastSep = sampleName.rfind("::");
                std::string simplePart = sampleName;
                if (lastSep != std::string::npos && lastSep + 2 < sampleName.size()) {
                    simplePart = sampleName.substr(lastSep + 2);
                }
                if (simplePart == funcSym.simpleName) {
                    sample = &s;
                    break;
                }
            }
        }

        if (!sample) continue;

        // Cardinality check
        if (funcSym.cardinality && sample->observedCardinalityP95) {
            const auto& card = *funcSym.cardinality;
            uint64_t p95 = *sample->observedCardinalityP95;

            HintsReportEntry entry;
            entry.function = funcSym.qualifiedName;
            entry.type = "cardinality";
            entry.declaredMin = card.min;
            entry.declaredMax = card.max;
            entry.observedP95 = p95;

            if (card.max > 0 && p95 > static_cast<uint64_t>(card.max) * 2) {
                entry.severity = "warning";
                entry.deviationFactor = static_cast<double>(p95) / static_cast<double>(card.max);
                entry.deviationFactor = std::round(entry.deviationFactor * 10.0) / 10.0;
                entry.message = "exceeds declared max by " +
                                std::to_string(entry.deviationFactor)
                                    .substr(0, std::to_string(entry.deviationFactor).find('.') + 2) +
                                "x";

                // Suggestion: expand range with 20% margin
                int64_t suggestedMax = static_cast<int64_t>(static_cast<double>(p95) * 1.2);
                entry.suggestion = "Update declaration to cardinality(" + formatCardinality(card.min) + ".." +
                                   formatCardinality(suggestedMax) + ")";
            } else if (card.min > 0 && p95 < static_cast<uint64_t>(card.min) / 2) {
                entry.severity = "info";
                entry.deviationFactor = static_cast<double>(card.min) / static_cast<double>(p95);
                entry.deviationFactor = std::round(entry.deviationFactor * 10.0) / 10.0;
                entry.message = "well below declared min";

                int64_t suggestedMin = static_cast<int64_t>(static_cast<double>(p95) * 0.8);
                entry.suggestion = "Update declaration to cardinality(" + formatCardinality(suggestedMin) + ".." +
                                   formatCardinality(card.max) + ")";
            } else {
                entry.severity = "ok";
                entry.message = "within declared range";
            }

            entries.push_back(entry);
        }

        // Access pattern check
        if (funcSym.accessPattern != topo::AccessPattern::None && sample->l1MissRate) {
            double missRate = *sample->l1MissRate;

            HintsReportEntry entry;
            entry.function = funcSym.qualifiedName;
            entry.type = "access-pattern";
            entry.declaredPattern = accessPatternStr(funcSym.accessPattern);
            entry.l1MissRate = missRate;

            if (funcSym.accessPattern == topo::AccessPattern::Streaming && missRate > 0.3) {
                entry.severity = "warning";
                entry.message = "high miss rate inconsistent with streaming";
                entry.suggestion = "Consider access(random) or investigate prefetch issues";
            } else if (funcSym.accessPattern == topo::AccessPattern::Random && missRate < 0.05) {
                entry.severity = "info";
                entry.message = "low miss rate suggests streaming pattern";
                entry.suggestion = "Consider access(streaming) to enable streaming optimizations";
            } else {
                entry.severity = "ok";
                entry.message = "consistent with declared pattern";
            }

            entries.push_back(entry);
        }
    }

    // 4. Output report
    bool jsonOutput = (format == "json");

    auto writeOutput = [&](std::ostream& dst) {
        if (jsonOutput) {
            nlohmann::json root;
            nlohmann::json jsonEntries = nlohmann::json::array();

            for (const auto& e : entries) {
                nlohmann::json je;
                je["function"] = e.function;
                je["type"] = e.type;
                je["severity"] = e.severity;
                je["message"] = e.message;

                if (e.type == "cardinality") {
                    je["declared"] = {{"min", e.declaredMin}, {"max", e.declaredMax}};
                    je["observed_p95"] = e.observedP95;
                    if (e.deviationFactor > 0.0) je["deviation_factor"] = e.deviationFactor;
                    if (!e.suggestion.empty()) je["suggestion"] = e.suggestion;
                } else {
                    je["declared_pattern"] = e.declaredPattern;
                    je["l1_miss_rate"] = e.l1MissRate;
                    if (!e.suggestion.empty()) je["suggestion"] = e.suggestion;
                }

                jsonEntries.push_back(je);
            }

            root["hints_report"] = jsonEntries;
            dst << root.dump(2) << "\n";
        } else {
            // Text format
            if (entries.empty()) {
                dst << "No hint/runtime data overlap found.\n";
                return;
            }

            for (const auto& e : entries) {
                if (e.type == "cardinality") {
                    dst << "[cardinality] " << e.function << "\n";
                    dst << "  Declared: " << formatCardinality(e.declaredMin) << ".."
                        << formatCardinality(e.declaredMax) << "\n";

                    // Format observed p95 with thousands separators
                    std::string p95Str = std::to_string(e.observedP95);
                    std::string formatted;
                    int count = 0;
                    for (int i = static_cast<int>(p95Str.size()) - 1; i >= 0; --i) {
                        if (count > 0 && count % 3 == 0) formatted = "," + formatted;
                        formatted = p95Str[static_cast<size_t>(i)] + formatted;
                        ++count;
                    }

                    dst << "  Runtime p95: " << formatted;

                    if (e.severity == "warning") {
                        dst << "   WARNING: " << e.message;
                    } else if (e.severity == "info") {
                        dst << "   INFO: " << e.message;
                    }
                    dst << "\n";

                    if (!e.suggestion.empty()) dst << "  Suggestion: " << e.suggestion << "\n";
                } else {
                    dst << "[access-pattern] " << e.function << "\n";
                    dst << "  Declared: access(" << e.declaredPattern << ")\n";
                    dst << "  L1 miss rate: " << static_cast<int>(e.l1MissRate * 100) << "%";
                    if (e.severity == "ok") {
                        dst << "   (" << e.message << ")";
                    } else if (e.severity == "warning") {
                        dst << "   WARNING: " << e.message;
                    } else if (e.severity == "info") {
                        dst << "   INFO: " << e.message;
                    }
                    dst << "\n";

                    if (!e.suggestion.empty()) dst << "  Suggestion: " << e.suggestion << "\n";
                }
                dst << "\n";
            }
        }
    };

    if (!outputPath.empty()) {
        std::ofstream outFile(outputPath);
        if (!outFile) {
            err << "error: cannot write to " << outputPath << "\n";
            return 1;
        }
        writeOutput(outFile);
        err << "Report written to " << outputPath << "\n";
    } else {
        writeOutput(out);
    }

    return 0;
}

} // namespace profile
} // namespace topo
