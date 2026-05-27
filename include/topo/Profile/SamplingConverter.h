#ifndef TOPO_PROFILE_SAMPLINGCONVERTER_H
#define TOPO_PROFILE_SAMPLINGCONVERTER_H

// perf-script sampling converter (stackcollapse folded
// format).
//
// Input format ("folded stack" as produced by Brendan Gregg's
// `stackcollapse-perf.pl`):
//
//     frame1;frame2;frame3 17
//     main;compute;kernel 5
//
// Each line is a unique stack (root → leaf, semicolon-delimited) followed
// by an integer sample count. The converter expands each count into that
// many sample events under the trace sampling sub-schema:
//
//     {
//       "sampling": {
//         "events": [
//           {"ts_ns": <synthetic>, "tid": 0,
//            "stack": ["frame1", "frame2", "frame3"]},
//           ...
//         ],
//         "summary": {"total_samples": N, "unique_stacks": M}
//       }
//     }
//
// Known simplification: stackcollapse folds away timestamps and thread IDs
// — the format only retains stack/count tuples. The converter therefore
// synthesizes a monotonically increasing `ts_ns` (`base + i * stepNs`,
// default 1 ms step) and emits a single `tid=0`. Sampling-mode
// time-distribution analyses that need true per-sample timestamps must wait
// for the `perf script` raw text reader (a separate converter, follow-up
// to this one). The folded reader is the minimal viable input path that
// keeps CTest reproducible: no `perf record` / `xctrace` invocation at
// test time, just a checked-in text fixture.
//
// Comment lines (`#…`) and blank lines are skipped. Lines whose final
// whitespace-separated token is not a non-negative integer are reported
// as parse errors with their 1-based line number.

#include <cstdint>
#include <iosfwd>
#include <nlohmann/json.hpp>
#include <string>

namespace topo::profile {

struct SamplingConverterOptions {
    // Synthetic timestamp base + step. Defaults: start at 0, advance 1 ms
    // per sample. Override only when a downstream test cares about the
    // exact wall-clock layout.
    std::int64_t baseTsNs = 0;
    std::int64_t stepNs = 1'000'000; // 1 ms
    // Single synthetic thread id — folded format discards real tids.
    std::int64_t tid = 0;
};

// Parse stackcollapse-folded text from `in` and write the trace
// sampling sub-schema into `outJson["sampling"]`. The caller owns the
// surrounding trace object (the converter never reads or modifies any
// other top-level fields).
//
// Returns true on success. On failure, returns false and populates
// `err` with a diagnostic (e.g. `"line 3: missing sample count"`).
//
// A completely empty input (or one with only comments / blank lines) is
// valid: `events` is an empty array and `summary.total_samples == 0`.
bool convertFoldedStream(std::istream& in,
                         nlohmann::json& outJson,
                         std::string& err,
                         const SamplingConverterOptions& opts = {});

} // namespace topo::profile

#endif // TOPO_PROFILE_SAMPLINGCONVERTER_H
