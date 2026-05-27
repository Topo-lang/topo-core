#ifndef TOPO_PROFILE_SPEEDSCOPECONVERTER_H
#define TOPO_PROFILE_SPEEDSCOPECONVERTER_H

// speedscope JSON sampling converter (py-spy default
// dump format, Chromium Performance panel export, Firefox profiler).
//
// Wire format reference:
//   https://github.com/jlfwong/speedscope/wiki/Importing-from-perf-(linux)
//
// Input shape (minimum subset this converter accepts — `sampled` profile
// type only):
//
//     {
//       "$schema": "https://www.speedscope.app/file-format-schema.json",
//       "exporter": "py-spy",
//       "shared": {
//         "frames": [
//           {"name":"main","file":"app.py","line":10},
//           {"name":"compute","file":"kernel.py","line":42}
//         ]
//       },
//       "profiles": [
//         {
//           "type": "sampled",
//           "name": "main thread",
//           "unit": "milliseconds",
//           "startValue": 0, "endValue": 100,
//           "samples":  [[0,1], [0,1], [0]],
//           "weights":  [10, 10, 5]
//         }
//       ]
//     }
//
// Semantics this converter implements:
//   - `shared.frames` is a frame pool; `profiles[].samples[i]` is an array
//     of integer indices into that pool, ordered root → leaf (matches the
//     other two converters' frame order).
//   - `profiles[].weights[i]` is the duration of `samples[i]` in `profiles[].unit`
//     ("milliseconds" / "microseconds" / "nanoseconds" / "none"). The converter
//     accumulates `ts_ns = sum(weights[0..i]) * unit_to_ns`; `unit == "none"`
//     is treated as 1 ns per weight unit (preserves ordering even when the
//     producer doesn't carry real time).
//   - Each profile becomes one thread, with `tid` = profile index in the
//     `profiles` array (speedscope itself does not carry numeric thread
//     ids — only the human-readable `name`, which we drop to keep the
//     output schema identical to the other two paths).
//   - Multiple profiles' events are concatenated then sorted by `ts_ns`
//     so consumers see a monotone timeline regardless of profile order.
//
// Output (trace sampling sub-schema, identical shape to the
// folded / JFR-NDJSON converters):
//
//     {
//       "sampling": {
//         "events": [
//           {"ts_ns": <int>, "tid": <int>, "stack": ["main","compute"]},
//           ...
//         ],
//         "summary": {"total_samples": N, "unique_stacks": M,
//                     "unique_threads": T},
//         "source_format": "speedscope_sampled"
//       }
//     }
//
// `total_samples` is the sum of every profile's sample count. `unique_stacks`
// counts distinct frame-name tuples across all profiles. `unique_threads`
// equals the number of profiles parsed (each profile == one thread).
//
// Unsupported: speedscope's `evented` profile type. Mixing evented and
// sampled in a single document is allowed by the wire format, but until
// a real consumer needs evented we reject any document containing an
// evented profile with a dedicated diagnostic, so the failure mode is
// loud rather than silently dropping data. The CLI maps this to exit 2
// (unsupported-mode bucket).

#include <cstdint>
#include <iosfwd>
#include <nlohmann/json.hpp>
#include <string>

namespace topo::profile {

// Reason for parse failure. `EventedNotSupported` is broken out from generic
// parse errors so the CLI can return a distinct exit code (2) for the
// "feature gap" case, mirroring the spans-mode `hybrid` placeholder.
enum class SpeedscopeError {
    None,
    ParseError,        // malformed JSON / missing field / bad index
    EventedNotSupported
};

// Parse a speedscope JSON document from `in` and write the trace
// sampling sub-schema into `outJson["sampling"]`. Caller owns the
// surrounding trace object.
//
// On success returns true and sets `errKind = None`. On failure returns
// false, populates `err` with a human diagnostic, and sets `errKind` so
// the CLI can pick the right exit code.
bool convertSpeedscopeStream(std::istream& in,
                             nlohmann::json& outJson,
                             std::string& err,
                             SpeedscopeError& errKind);

} // namespace topo::profile

#endif // TOPO_PROFILE_SPEEDSCOPECONVERTER_H
