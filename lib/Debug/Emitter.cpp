#include "topo/Debug/Emitter.h"
#include "topo/Debug/Schema.h"

#include <fstream>
#include <iostream>

namespace topo::debug_meta {

using nlohmann::json;

static json sliceExprToJson(const DebugSliceExpr& slice) {
    json out = json::object();
    out["kind"] = slice.isSliced ? "slice" : "field";
    out["container"] = slice.container;
    if (slice.isSliced) {
        if (slice.start) out["start"] = *slice.start;
        if (slice.end) out["end"] = *slice.end;
    }
    return out;
}

static json viewToJson(const DebugViewEntry& view) {
    json out = json::object();
    out["name"] = view.name;
    out["expr"] = sliceExprToJson(view.slice);
    return out;
}

static json inactiveToJson(const DebugInactiveEntry& inactive) {
    json out = json::object();
    out["expr"] = sliceExprToJson(inactive.region);
    out["mode"] = debugInactiveModeName(inactive.mode);
    return out;
}

static json renderToJson(const DebugRenderRaw& raw) {
    json out = json::object();
    out["method"] = raw.method;
    out["raw_body"] = raw.rawBody;
    return out;
}

static json symbolToJson(const DebugEntry& entry,
                        const std::map<std::string, BackendExtension*>& extensions) {
    json sym = json::object();
    sym["topo_name"] = entry.targetTypeName;
    // Default emit: host_symbol mirrors topo_name. Each backend overrides
    // this later by attaching `backend_ext.<lang>.host_symbol`.
    sym["host_symbol"] = entry.targetQualifiedName.empty() ? entry.targetTypeName
                                                           : entry.targetQualifiedName;
    sym["kind"] = debugTargetKindName(entry.targetKind);
    // Stub. Real layout_inverse is computed per-backend (data layout pass +
    // decompiler) and stored under backend_ext; the base field stays present
    // so downstream tools can default-read it.
    sym["layout_inverse"] = json::object();

    json views = json::array();
    for (const auto& v : entry.views) views.push_back(viewToJson(v));
    sym["views"] = std::move(views);

    if (entry.summaryTemplate) {
        sym["summary_template"] = *entry.summaryTemplate;
    }

    json renders = json::array();
    for (const auto& r : entry.renderDecls) renders.push_back(renderToJson(r));
    sym["render_decls"] = std::move(renders);

    json inactive = json::array();
    for (const auto& r : entry.inactiveRegions) inactive.push_back(inactiveToJson(r));
    sym["inactive_regions"] = std::move(inactive);

    json backendExt = json::object();
    for (const auto& [name, ext] : extensions) {
        if (!ext) continue;
        json e = ext->emitForSymbol(entry);
        if (!e.is_null() && !(e.is_object() && e.empty())) {
            backendExt[name] = std::move(e);
        }
    }
    sym["backend_ext"] = std::move(backendExt);

    return sym;
}

json buildJson(const SymbolTable& symbols,
               const SourceManifest& source,
               const std::map<std::string, BackendExtension*>& extensions) {
    json root = json::object();
    root["schema_version"] = kSchemaVersion;

    json src = json::object();
    src["topo_files"] = source.topoFiles;
    src["host_files"] = source.hostFiles;
    root["source"] = std::move(src);

    json symbolsJson = json::array();
    for (const auto& e : symbols.debugEntries()) {
        symbolsJson.push_back(symbolToJson(e, extensions));
    }
    root["symbols"] = std::move(symbolsJson);

    return root;
}

bool emit(const SymbolTable& symbols,
          const EmitOptions& options,
          const std::map<std::string, BackendExtension*>& extensions) {
    if (options.outPath.empty()) {
        std::cerr << "topo-debug-emitter: outPath is empty\n";
        return false;
    }

    json root = buildJson(symbols, options.source, extensions);

    // Pretty-print with sorted keys for golden-test stability. nlohmann::json
    // 's dump uses insertion order by default; here we re-serialize through
    // an ordered_json round-trip to enforce key ordering.
    nlohmann::ordered_json ordered = nlohmann::ordered_json::parse(root.dump());
    // Sort recursively.
    std::function<void(nlohmann::ordered_json&)> sortKeys = [&](nlohmann::ordered_json& j) {
        if (j.is_object()) {
            std::vector<std::pair<std::string, nlohmann::ordered_json>> pairs;
            pairs.reserve(j.size());
            for (auto it = j.begin(); it != j.end(); ++it) {
                pairs.emplace_back(it.key(), std::move(it.value()));
            }
            std::sort(pairs.begin(), pairs.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            j = nlohmann::ordered_json::object();
            for (auto& [k, v] : pairs) {
                sortKeys(v);
                j[k] = std::move(v);
            }
        } else if (j.is_array()) {
            for (auto& item : j) sortKeys(item);
        }
    };
    sortKeys(ordered);

    std::ofstream out(options.outPath);
    if (!out) {
        std::cerr << "topo-debug-emitter: cannot open output '" << options.outPath << "'\n";
        return false;
    }
    out << ordered.dump(2) << "\n";
    return true;
}

} // namespace topo::debug_meta
