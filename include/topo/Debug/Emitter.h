#ifndef TOPO_DEBUG_EMITTER_H
#define TOPO_DEBUG_EMITTER_H

#include "topo/Sema/SymbolTable.h"
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

// `*.topo-dbg.json` emitter.
//
// The artifact has a shared base section (schema_version,
// source, symbols[]) and per-backend extension segments under `backend_ext`.
// The base section is always emitted; backend extensions are injected
// through the `BackendExtension` interface defined here.
//
// The emitter is intentionally pure-data: feed it a `SymbolTable` and an
// output path, get back a stable JSON file. Keys are sorted; no timestamps
// or non-deterministic fields are emitted, so the output is suitable for
// golden tests.

namespace topo::debug_meta {

// Source manifest. Typically populates `topoFiles`
// only — `hostFiles` is reserved for future spec extensions when host source
// participates in debug metadata generation.
struct SourceManifest {
    std::vector<std::string> topoFiles;
    std::vector<std::string> hostFiles;
};

struct EmitOptions {
    std::filesystem::path outPath;
    SourceManifest source;
};

// Plans 41-44 each subclass this to inject per-backend extension data.
// `emitForSymbol` is called once per `symbols[]` entry; the returned JSON
// object is merged under `backend_ext.<key>` keyed by the map key passed
// to `emit()`. Returning `nlohmann::json::object()` (the default) means
// "no extension for this symbol".
class BackendExtension {
public:
    virtual ~BackendExtension() = default;
    virtual nlohmann::json emitForSymbol(const DebugEntry& entry) = 0;
};

// Emit the debug metadata JSON. Returns true on success; on failure prints
// to stderr and returns false. The `extensions` map keys (e.g. "llvm",
// "jvm") become the field names under each symbol's `backend_ext` object.
bool emit(const SymbolTable& symbols,
          const EmitOptions& options,
          const std::map<std::string, BackendExtension*>& extensions = {});

// Convenience overload for tests / consumers that want the JSON in memory.
nlohmann::json buildJson(const SymbolTable& symbols,
                         const SourceManifest& source,
                         const std::map<std::string, BackendExtension*>& extensions = {});

} // namespace topo::debug_meta

#endif // TOPO_DEBUG_EMITTER_H
