#ifndef TOPO_PROFILE_JFRNDJSONCONVERTER_H
#define TOPO_PROFILE_JFRNDJSONCONVERTER_H

// JFR-NDJSON sampling converter for the JVM backend.
//
// The JVM-native profile format is `.jfr` — a binary container produced by
// `jdk.jfr` (Java Flight Recorder). Parsing `.jfr` directly requires the
// `jdk.jfr` API, which only lives inside a running JVM. To keep CTest
// reproducible without spawning a JVM at test time, we accept a **NDJSON
// intermediate**: one JSON object per line, each shaped like a simplified
// JFR event. A real bridge (planned: `java -jar topo-profile-jvm-jfr.jar
// <jfr>`) will emit the same NDJSON shape into stdout, and this converter
// will consume it identically.
//
// Input line shape (one of jdk.ExecutionSample / jdk.ObjectAllocationInNewTLAB
// / etc., but all currently treated as sampling events):
//
//     {"event_type":"jdk.ExecutionSample",
//      "ts_ns":12345,
//      "thread":{"id":1,"name":"main"},
//      "stack":[{"class":"geom.Mesh","method":"compute","line":42},
//               {"class":"main.App","method":"run","line":12}],
//      "duration_ns":4200}    // optional
//
// Frame order: root → leaf (matches the jdk.jfr API default, opposite of
// stackcollapse-folded). The converter preserves this order; downstream
// stack consumers can match LLVM-side conventions root-first.
//
// Output (trace sampling sub-schema, identical shape to the
// stackcollapse-folded converter so consumers don't branch):
//
//     {
//       "sampling": {
//         "events": [
//           {"ts_ns": 12345, "tid": 1,
//            "stack": ["geom.Mesh.compute", "main.App.run"]},
//           ...
//         ],
//         "summary": {"total_samples": N, "unique_stacks": M,
//                     "unique_threads": T},
//         "source_format": "jdk_jfr_ndjson"
//       }
//     }
//
// Stack frames are flattened to `<class>.<method>` strings (the line field
// is preserved as a follow-up — current consumers, including the C1 folded
// path, expect plain `string` frames). `unique_stacks` counts distinct
// stack-string-tuples across all events; `unique_threads` counts distinct
// `thread.id` values seen.
//
// Bucketing by `event_type` (sample vs allocation vs lock contention) is
// **not** done here — every event becomes a sampling event. That split is
// a clear follow-up once consumers need to distinguish wall-clock samples
// from allocation samples.
//
// Per-Pass profile events. Lines whose `event_type` begins
// with `topo.pass.` are NOT sampling events: they are stackless custom
// JFR events injected by the JVM transform (AdaptivePass / ArenaPass /
// ParallelPass / PipelinePass via dev.topo.PassEvents) and forwarded by
// the jfr-bridge with a `"fields"` object. The converter routes them into
// a top-level `pass_events.<PassName>[]` map:
//
//     {
//       "sampling": { ... as above ... },
//       "pass_events": {
//         "AdaptivePass": [
//           {"ts_ns":..., "tid":..., "fields":{"method":...,"from":...,"to":...}},
//           ...
//         ], ...
//       }
//     }
//
// This is strictly additive — any other `event_type` (notably
// jdk.ExecutionSample) is handled exactly as before, and `pass_events` is
// only present when at least one pass event was seen (absent, not empty,
// for a sample-only recording).
//
// Parse errors are reported with their 1-based line number. Comment lines
// (lines whose first non-whitespace char is `#`) and blank lines are
// skipped, mirroring the folded reader's tolerance.

#include <cstdint>
#include <iosfwd>
#include <nlohmann/json.hpp>
#include <string>

namespace topo::profile {

// Parse NDJSON-shaped JFR events from `in` and write the trace
// sampling sub-schema into `outJson["sampling"]`. Caller owns the
// surrounding trace object.
//
// Returns true on success. On failure, returns false and populates `err`
// with a diagnostic (e.g. `"line 3: missing 'stack' array"`).
//
// An empty input (only comments / blank lines) is valid: events array is
// empty and `summary.total_samples == 0`.
bool convertJfrNdjsonStream(std::istream& in,
                            nlohmann::json& outJson,
                            std::string& err);

} // namespace topo::profile

#endif // TOPO_PROFILE_JFRNDJSONCONVERTER_H
