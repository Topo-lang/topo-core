#ifndef TOPO_PROFILE_CPROFILENDJSONCONVERTER_H
#define TOPO_PROFILE_CPROFILENDJSONCONVERTER_H

// cProfile-NDJSON sampling converter for the Python backend.
//
// cProfile is a Python stdlib deterministic profiler — it instruments every
// Python call rather than sampling at a fixed rate. Walking its output
// directly from C++ would require linking against CPython, so we route
// through an NDJSON intermediate emitted by the harness
// `topo_profile_python.cprofile_harness` (one event per recorded call edge,
// shape mirrors JfrNdjsonConverter so the two are interchangeable on the
// wire).
//
// Input line shape:
//
//     {"event_type":"py_call",
//      "ts_ns":12345,
//      "thread":{"id":1,"name":"MainThread"},
//      "stack":[{"module":"app.py","function":"compute","line":42},
//               {"module":"kernel.py","function":"inner","line":58}],
//      "duration_ns":4200}
//
// Frame order: root → leaf (matches the harness emit order and the JFR
// convention). The converter preserves this order so downstream stack
// consumers see consistent root-first stacks across all source formats.
//
// Output (trace sampling sub-schema, identical shape to the
// JFR-NDJSON / folded / speedscope / cpuprofile converters):
//
//     {
//       "sampling": {
//         "events": [
//           {"ts_ns": 12345, "tid": 1, "duration_ns": 4200,
//            "stack": ["app.compute:42", "kernel.inner:58"]},
//           ...
//         ],
//         "summary": {"total_samples": N, "unique_stacks": M,
//                     "unique_threads": T},
//         "source_format": "cprofile_ndjson"
//       }
//     }
//
// Stack frames are flattened to `<module>.<function>:<line>` strings (the
// `:<line>` suffix is omitted when `line <= 0`, which is the case for
// built-in / synthesized entries). Plain-string frames keep this converter
// interchangeable with the other four sampling-mode source formats.
//
// Summary field naming note: this converter writes the unified summary
// keys `total_samples` / `unique_stacks` / `unique_threads`, identical to
// the JFR / folded / speedscope siblings. Consumers read these keys
// generically regardless of `source_format`.
//
// Validation rules (each violation → exit 2 via err):
//   - any line with `event_type != "py_call"` is rejected (cProfile is the
//     only producer of this stream; mixing in other event types here is
//     evidence of a misconfigured pipeline)
//   - empty input (no events at all) is rejected — the harness must have
//     captured at least one user call for the trace to be useful
//   - missing required fields (`ts_ns`, `stack`, `duration_ns`) → rejected
//   - blank lines and `#` comment lines are skipped (matches the other
//     NDJSON converters)
//
// `unique_threads` counts distinct `thread.id` values; `unique_stacks`
// counts distinct stack-string-tuples across all events.

#include <cstdint>
#include <iosfwd>
#include <nlohmann/json.hpp>
#include <string>

namespace topo::profile {

// Parse cProfile-shaped NDJSON events from `in` and write the trace
// sampling sub-schema into `outJson["sampling"]`. Caller owns the
// surrounding trace object.
//
// Returns true on success. On failure, returns false and populates `err`
// with a diagnostic (e.g. `"line 3: unexpected event_type 'jdk.ExecutionSample'
// (cprofile_ndjson expects 'py_call')"`).
//
// Empty input (only comments / blank lines) is rejected with
// "no py_call events found" — unlike the JFR converter we treat this as
// failure because a cProfile run should always yield at least one edge.
bool convertCProfileNdjsonStream(std::istream& in,
                                 nlohmann::json& outJson,
                                 std::string& err);

} // namespace topo::profile

#endif // TOPO_PROFILE_CPROFILENDJSONCONVERTER_H
