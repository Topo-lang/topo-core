#ifndef TOPO_DEBUG_PASSREPORTS_H
#define TOPO_DEBUG_PASSREPORTS_H

// Generic per-Pass report view for `topo debug query`.
//
// At build time the LLVM backend (or any other backend) writes one JSON file
// per judging Pass under `<output>.topo-passes/<PassName>.json`. Each file
// has a common `header { pass, category, fired, fired_count, decision,
// reason, elapsed_ns }` and a Pass-specific detail block (candidates /
// entries / stats).
//
// The *typed* per-Pass detail structs live exclusively
// in the LLVM backend (`topo-llvm/include/topo/Backend/PassReports.h`).
// topo-core never sees those types; the Compute-layer view here is generic
// header + opaque `nlohmann::json detail`. Consumers that need to access a
// detail field destructure the JSON object themselves.

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace topo::debug_meta {

// Header common to every Pass report. Keys mirror the wire JSON produced by
// `topo-llvm/lib/Backend/PassReportsSidecar.cpp` (snake_case on disk →
// camelCase here, the same convention `topo::backend::PassReportHeader` uses).
struct PassReportHeader {
    std::string passName;     // "DataLayoutPass"
    std::string category;     // "OPT" / "ENHANCE" / "VERIFY" / ...
    bool fired = false;
    int firedCount = 0;
    std::string decision;     // "auto_soa" / "disabled" / "no_candidates" / ...
    std::string reason;       // human-readable explanation
    int64_t elapsedNs = 0;
};

// One Pass's report: typed header + opaque detail JSON (candidates /
// entries / stats — schema is per-Pass and owned by the backend).
struct PassReport {
    PassReportHeader header;
    nlohmann::json detail = nlohmann::json::object();
};

} // namespace topo::debug_meta

#endif // TOPO_DEBUG_PASSREPORTS_H
