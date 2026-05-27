#ifndef TOPO_PROFILE_ASYNCPROFILERCONVERTER_H
#define TOPO_PROFILE_ASYNCPROFILERCONVERTER_H

// async-profiler "collapsed" (folded) → trace sampling
// segment for the JVM backend (wall-clock / alloc / cpu profiling modes
// that JFR's sampling profiler does not cover well — off-CPU wall time and
// allocation-site profiling in particular).
//
// async-profiler's `collapsed` output format is the Brendan-Gregg folded
// stack with one extension: when recorded with `-t` (per-thread), each
// line is prefixed with the thread as the *first* frame, in the canonical
// async-profiler form
//
//     [tid] thread-name;frame1;frame2;...;leaf count
//
// (the `[tid] name` token is emitted verbatim by async-profiler as the
// stack root). Without `-t` the line is a plain folded stack:
//
//     frame1;frame2;...;leaf count
//
// This converter accepts BOTH shapes. When the first frame matches the
// `[<digits>] <name>` thread-root convention the numeric tid is lifted out
// into the per-event `tid` field and the remaining frames become the
// stack; otherwise the whole line is the stack and `tid` defaults to 0.
// Stacks are thereby grouped by thread (distinct `tid`) and by leaf
// function (the folded format already groups identical stacks into one
// count line, so leaf/function grouping is inherent).
//
// Output (trace sampling sub-schema — identical shape to the
// stackcollapse-folded and JFR-NDJSON converters so consumers never
// branch on source), with the async-profiler discriminator:
//
//     {
//       "sampling": {
//         "events": [
//           {"ts_ns": <synthetic>, "tid": <lifted-or-0>,
//            "stack": ["frame1", "frame2", "..."]},
//           ...
//         ],
//         "summary": {"total_samples": N, "unique_stacks": M,
//                     "unique_threads": T},
//         "source_format": "async_profiler_collapsed"
//       }
//     }
//
// async-profiler's `--type=wall|alloc|cpu` selects WHAT the agent samples
// (wall-clock / allocation bytes / on-CPU cycles); the collapsed text it
// emits is structurally identical for all three (only the leaf semantics
// differ), so a single converter covers every `--type`. The CLI records
// the requested type but the wire shape is uniform — exactly mirroring how
// the JFR converter treats every jdk.* event as one sampling shape.
//
// Like the sibling converters: `ts_ns` is synthesized monotonically (the
// folded format has no per-sample timestamps), comment (`#…`) and blank
// lines are skipped, and parse errors carry their 1-based line number.

#include <cstdint>
#include <iosfwd>
#include <nlohmann/json.hpp>
#include <string>

namespace topo::profile {

struct AsyncProfilerConverterOptions {
    // Synthetic timestamp base + step (collapsed format discards real
    // per-sample timestamps). Defaults: start at 0, advance 1 ms / sample.
    std::int64_t baseTsNs = 0;
    std::int64_t stepNs = 1'000'000; // 1 ms
};

// Parse async-profiler collapsed text from `in` and write the trace
// sampling sub-schema into `outJson["sampling"]`. The caller owns the
// surrounding trace object (the converter only touches out["sampling"]).
//
// Returns true on success. On failure, returns false and populates `err`
// with a diagnostic (e.g. `"line 3: missing sample count"`).
//
// A completely empty input (or only comments / blank lines) is valid:
// `events` is an empty array and `summary.total_samples == 0`.
bool convertAsyncProfilerCollapsedStream(
    std::istream& in,
    nlohmann::json& outJson,
    std::string& err,
    const AsyncProfilerConverterOptions& opts = {});

} // namespace topo::profile

#endif // TOPO_PROFILE_ASYNCPROFILERCONVERTER_H
