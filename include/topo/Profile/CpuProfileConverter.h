#ifndef TOPO_PROFILE_CPUPROFILECONVERTER_H
#define TOPO_PROFILE_CPUPROFILECONVERTER_H

// V8 `.cpuprofile` sampling converter (Chrome DevTools
// profile format, also what `node --cpu-prof` emits by default).
//
// Wire format reference:
//   V8 profile shape — `nodes[]` tree (children pointers), `samples[]` of
//   leaf node ids, `timeDeltas[]` in microseconds with `startTime` as
//   absolute base. Documented at
//   https://chromedevtools.github.io/devtools-protocol/tot/Profiler/#type-Profile
//
// Input shape (minimum subset this converter accepts):
//
//     {
//       "nodes": [
//         {"id": 1,
//          "callFrame": {"functionName":"(root)","scriptId":"0","url":"",
//                        "lineNumber":-1,"columnNumber":-1},
//          "hitCount": 0, "children": [2]},
//         {"id": 2,
//          "callFrame": {"functionName":"main","scriptId":"42",
//                        "url":"file:///app.js","lineNumber":9,
//                        "columnNumber":1},
//          "hitCount": 5, "children": [3]},
//         {"id": 3,
//          "callFrame": {"functionName":"compute","scriptId":"42",
//                        "url":"file:///app.js","lineNumber":41,
//                        "columnNumber":3},
//          "hitCount": 15, "children": []}
//       ],
//       "startTime": 1000,
//       "endTime": 100000,
//       "samples": [2, 3, 3, 3, 2, 3, ...],
//       "timeDeltas": [100, 100, 100, 100, ...]
//     }
//
// Semantics this converter implements:
//   - `nodes[]` is a tree linked through `children[]` ids. We build an
//     id→node lookup and a child→parent map in one pass (each child
//     reference in any node's `children[]` writes the parent index for
//     that child). For each sample (a leaf node id) we walk parent
//     pointers up to the synthetic `(root)` node — root itself is
//     dropped — and the resulting list is reversed to give root→leaf
//     order, matching every other sampling converter.
//   - Each stack frame serializes to `<functionName>@<file>:<line>`.
//     `<file>` is `basename(url)` so the output is stable across
//     absolute vs `file://` paths; `<line>` is V8's `lineNumber` + 1
//     (V8 stores 0-indexed lines on the wire but every other source-
//     location surface we emit uses 1-indexed). Empty `functionName`
//     becomes `<anonymous>` so the frame string is never `@file:line`.
//   - Timestamps: V8 reports times in **microseconds**. The i-th
//     sample's `ts_ns = (startTime + sum(timeDeltas[0..i])) * 1000`.
//   - Threads: V8's `.cpuprofile` is per-isolate (single thread).
//     `tid = 0` for every event; worker-thread merging is a follow-up.
//
// Output (trace sampling sub-schema, identical shape to the
// folded / JFR / speedscope converters):
//
//     {
//       "sampling": {
//         "events": [
//           {"ts_ns": <int>, "tid": 0,
//            "stack": ["main@app.js:10", "compute@app.js:42"]},
//           ...
//         ],
//         "summary": {"total_samples": N, "unique_stacks": M,
//                     "unique_threads": 1},
//         "source_format": "v8_cpuprofile"
//       }
//     }
//
// Source-map translation (`*.js` → original `*.ts` via tsc's `.js.map`)
// is opt-in: pass a non-null `topo::v8::debug::SourceMapResolver*` to
// convertCpuProfileStream and the converter will translate every frame
// whose URL maps to an entry in the resolver. Frames that fail to
// resolve fall back to the original V8-reported `.js` location. When at
// least one frame resolves successfully, `source_format` is reported as
// `cpuprofile_with_sourcemap` instead of `v8_cpuprofile`, and the
// summary block carries `frames_resolved_via_sourcemap` /
// `frames_total` counts for coverage diagnosis.
//
// Parse errors carry the offending field path (`samples[3]`,
// `nodes[7].children[1]`, etc.) so producers can locate the issue without
// re-reading the source.

#include <cstdint>
#include <iosfwd>
#include <nlohmann/json.hpp>
#include <string>

namespace topo::profile {

class FrameResolver;

// Parse a V8 `.cpuprofile` JSON document from `in` and write the trace
// sampling sub-schema into `outJson["sampling"]`. Caller owns the
// surrounding trace object.
//
// If `resolver` is non-null, each frame's URL is looked up; on a hit the
// emitted frame string uses the original source file basename and line
// instead of the V8-reported `.js` location. Frames that fail to resolve
// fall back to the original location. Resolution counts are exposed via
// `sampling.summary.frames_resolved_via_sourcemap` /
// `sampling.summary.frames_total`.
//
// Returns true on success. On failure, returns false and populates `err`
// with a diagnostic.
bool convertCpuProfileStream(std::istream& in,
                             nlohmann::json& outJson,
                             std::string& err,
                             FrameResolver* resolver = nullptr);

} // namespace topo::profile

#endif // TOPO_PROFILE_CPUPROFILECONVERTER_H
