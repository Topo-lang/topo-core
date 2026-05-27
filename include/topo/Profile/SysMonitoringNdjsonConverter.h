#ifndef TOPO_PROFILE_SYSMONITORINGNDJSONCONVERTER_H
#define TOPO_PROFILE_SYSMONITORINGNDJSONCONVERTER_H

// `sys.monitoring` (PEP 669, Python 3.12+) sampling converter.
//
// `--mode=sys-monitoring` runs the target under the harness
// `topo_profile_python.sys_monitoring_harness`, which registers PEP 669
// PY_START / PY_RETURN callbacks and emits one NDJSON line per event. PEP
// 669 is a CPython-internal API, so (as with cProfile) we route through an
// NDJSON intermediate rather than linking CPython into C++.
//
// Input line shape (root frame per event; START/RETURN are paired here):
//
//     {"event_type":"py_start",
//      "ts_ns":12345,
//      "thread":{"id":1,"name":"MainThread"},
//      "frame":{"module":"app.py","function":"compute","line":42}}
//     {"event_type":"py_return",
//      "ts_ns":16545,
//      "thread":{"id":1,"name":"MainThread"},
//      "frame":{"module":"app.py","function":"compute","line":42}}
//
// The PY_START/PY_RETURN sequence is the literal acceptance artifact.
// The converter pairs
// them per (thread, call-depth) into call samples and folds them into the
// trace sampling sub-schema, identical in shape to the cProfile /
// JFR / folded / speedscope / cpuprofile converters so all six are
// interchangeable on the wire:
//
//     {
//       "sampling": {
//         "events": [
//           {"ts_ns": 12345, "tid": 1, "duration_ns": 4200,
//            "stack": ["app.compute:42"]},
//           ...
//         ],
//         "summary": {"total_samples": N, "unique_stacks": M,
//                     "unique_threads": T},
//         "source_format": "py_sys_monitoring"
//       }
//     }
//
// "Join with declarations" is satisfied the same way the
// other Python sampling formats do it: each frame is flattened to
// `<module>.<function>:<line>`, so a stage-boundary function declared in
// the project surfaces by name in the sample stacks for the downstream
// declaration-aware consumer.
//
// Pairing rule: maintain a per-thread stack of open PY_START frames. A
// PY_RETURN pops the matching innermost START and emits one sample with
// `duration_ns = return.ts_ns - start.ts_ns` and `stack` = the open frame
// chain root→leaf at START time. An unmatched RETURN (no open START on
// that thread) is skipped defensively; START frames still open at EOF are
// emitted with `duration_ns = 0` so entry points are never dropped.
//
// Validation (each violation → false + err):
//   - any line whose `event_type` is neither `py_start` nor `py_return`
//   - empty input (no events) — a sys.monitoring run must yield ≥1 event
//   - missing required fields (`ts_ns`, `thread.id`, `frame`)
//   - blank / `#`-comment lines are skipped (matches the sibling NDJSON
//     converters)

#include <iosfwd>
#include <nlohmann/json.hpp>
#include <string>

namespace topo::profile {

// Parse PY_START/PY_RETURN NDJSON from `in` and write the trace
// sampling sub-schema into `outJson["sampling"]`. Caller owns the
// surrounding trace object. Returns true on success; on failure returns
// false and populates `err` with a line-anchored diagnostic.
bool convertSysMonitoringNdjsonStream(std::istream& in,
                                      nlohmann::json& outJson,
                                      std::string& err);

} // namespace topo::profile

#endif // TOPO_PROFILE_SYSMONITORINGNDJSONCONVERTER_H
