#ifndef TOPO_DEBUG_SCHEMA_H
#define TOPO_DEBUG_SCHEMA_H

// `*.topo-dbg.json` schema constants.
//
// See `schema-v1.json` (alongside this header) for the authoritative
// JSON Schema. The schema is intentionally small:
//
//   {
//     "schema_version": "1",
//     "source": { "topo_files": [...], "host_files": [...] },
//     "symbols": [
//       {
//         "topo_name":       "Mesh",
//         "host_symbol":     "geom::Mesh",   // currently == topo_name
//         "kind":            "type" | "data" | "function",
//         "layout_inverse":  {},             // stub
//         "views":           [ { name, kind, container, start?, end? } ],
//         "summary_template": "...",        // optional
//         "render_decls":    [ ... ],       // passthrough; semantics owned downstream
//         "backend_ext":     {}             // backend extensions fill this
//       }
//     ]
//   }
//
// Backends inject `backend_ext.{llvm,jvm,python,v8}` later via the
// `BackendExtension` interface declared in `Emitter.h`.

namespace topo::debug_meta {

inline constexpr const char* kSchemaVersion = "1";

} // namespace topo::debug_meta

#endif // TOPO_DEBUG_SCHEMA_H
