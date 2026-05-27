// speedscope JSON → trace sampling segment.
// See SpeedscopeConverter.h for the wire contract and the rationale for
// rejecting `evented` profiles loudly.

#include "topo/Profile/SpeedscopeConverter.h"

#include <algorithm>
#include <cstdint>
#include <istream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace topo::profile {

namespace {

// Convert speedscope `unit` to nanoseconds per weight unit.
// `unit == "none"` is treated as 1 (preserves ordering when the producer
// has no real time — e.g. flat counters). Unknown units are reported as
// a parse error by returning 0 and letting the caller surface the
// diagnostic.
std::int64_t unitToNs(const std::string& unit) {
    if (unit == "nanoseconds") return 1;
    if (unit == "microseconds") return 1'000;
    if (unit == "milliseconds") return 1'000'000;
    if (unit == "seconds") return 1'000'000'000;
    if (unit == "none") return 1;
    return 0; // sentinel: caller reports as unknown unit
}

} // namespace

bool convertSpeedscopeStream(std::istream& in,
                             nlohmann::json& outJson,
                             std::string& err,
                             SpeedscopeError& errKind) {
    errKind = SpeedscopeError::None;

    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const std::exception& e) {
        std::ostringstream o;
        o << "invalid speedscope JSON: " << e.what();
        err = o.str();
        errKind = SpeedscopeError::ParseError;
        return false;
    }
    if (!doc.is_object()) {
        err = "speedscope document must be a JSON object at the top level";
        errKind = SpeedscopeError::ParseError;
        return false;
    }

    // shared.frames — frame pool. We only extract `name`; the optional
    // `file`/`line` fields are dropped because every other converter emits
    // plain-string stack frames (keeping the output shape uniform across
    // source_formats is the whole point of routing through the trace schema).
    if (!doc.contains("shared") || !doc["shared"].is_object()) {
        err = "missing 'shared' object";
        errKind = SpeedscopeError::ParseError;
        return false;
    }
    const auto& shared = doc["shared"];
    if (!shared.contains("frames") || !shared["frames"].is_array()) {
        err = "missing 'shared.frames' array";
        errKind = SpeedscopeError::ParseError;
        return false;
    }
    const auto& framesArr = shared["frames"];
    std::vector<std::string> framePool;
    framePool.reserve(framesArr.size());
    for (size_t fi = 0; fi < framesArr.size(); ++fi) {
        const auto& fr = framesArr[fi];
        if (!fr.is_object() || !fr.contains("name") ||
            !fr["name"].is_string()) {
            std::ostringstream o;
            o << "shared.frames[" << fi
              << "] must be an object with string 'name'";
            err = o.str();
            errKind = SpeedscopeError::ParseError;
            return false;
        }
        framePool.push_back(fr["name"].get<std::string>());
    }

    if (!doc.contains("profiles") || !doc["profiles"].is_array()) {
        err = "missing 'profiles' array";
        errKind = SpeedscopeError::ParseError;
        return false;
    }
    const auto& profilesArr = doc["profiles"];
    if (profilesArr.empty()) {
        err = "'profiles' array must contain at least one profile";
        errKind = SpeedscopeError::ParseError;
        return false;
    }

    // First sweep: detect any evented profile and reject the whole
    // document. Mixing evented + sampled in a single doc is legal per
    // schema but pointless to half-support, so we fail loudly.
    for (size_t pi = 0; pi < profilesArr.size(); ++pi) {
        const auto& p = profilesArr[pi];
        if (!p.is_object() || !p.contains("type") || !p["type"].is_string()) {
            std::ostringstream o;
            o << "profiles[" << pi << "] must have a string 'type'";
            err = o.str();
            errKind = SpeedscopeError::ParseError;
            return false;
        }
        const std::string& pt = p["type"].get_ref<const std::string&>();
        if (pt == "evented") {
            std::ostringstream o;
            o << "speedscope evented profile not yet supported; use sampled "
                 "(profiles[" << pi << "].type == 'evented')";
            err = o.str();
            errKind = SpeedscopeError::EventedNotSupported;
            return false;
        }
        if (pt != "sampled") {
            std::ostringstream o;
            o << "profiles[" << pi << "].type must be 'sampled' (got '"
              << pt << "')";
            err = o.str();
            errKind = SpeedscopeError::ParseError;
            return false;
        }
    }

    // Second sweep: extract events. Each profile becomes one thread (tid =
    // profile index). We collect events first, then sort by ts_ns so the
    // output timeline is monotone even across multiple profiles.
    struct PendingEvent {
        std::int64_t tsNs;
        std::int64_t tid;
        std::vector<std::string> stack;
    };
    std::vector<PendingEvent> pending;
    std::int64_t totalSamples = 0;
    // Distinct stack-string-tuples for `unique_stacks`. Comparing by vector
    // gives a structural (frame-by-frame) match without hashing.
    std::set<std::vector<std::string>> seenStacks;

    for (size_t pi = 0; pi < profilesArr.size(); ++pi) {
        const auto& p = profilesArr[pi];
        if (!p.contains("samples") || !p["samples"].is_array()) {
            std::ostringstream o;
            o << "profiles[" << pi << "] missing 'samples' array";
            err = o.str();
            errKind = SpeedscopeError::ParseError;
            return false;
        }
        if (!p.contains("weights") || !p["weights"].is_array()) {
            std::ostringstream o;
            o << "profiles[" << pi << "] missing 'weights' array";
            err = o.str();
            errKind = SpeedscopeError::ParseError;
            return false;
        }
        const auto& samples = p["samples"];
        const auto& weights = p["weights"];
        if (samples.size() != weights.size()) {
            std::ostringstream o;
            o << "profiles[" << pi << "]: samples (" << samples.size()
              << ") and weights (" << weights.size()
              << ") must have equal length";
            err = o.str();
            errKind = SpeedscopeError::ParseError;
            return false;
        }
        std::string unit = "none";
        if (p.contains("unit") && p["unit"].is_string()) {
            unit = p["unit"].get<std::string>();
        }
        std::int64_t unitNs = unitToNs(unit);
        if (unitNs == 0) {
            std::ostringstream o;
            o << "profiles[" << pi << "].unit '" << unit
              << "' is not a recognized speedscope unit "
                 "(expected nanoseconds | microseconds | milliseconds | "
                 "seconds | none)";
            err = o.str();
            errKind = SpeedscopeError::ParseError;
            return false;
        }

        // Accumulate timestamp from weights. Per the schema, the i-th
        // sample's *end* time is sum(weights[0..=i]) — we report ts at the
        // end of the slice so consumers can replay the timeline in event
        // order without negative durations.
        std::int64_t cum = 0;
        for (size_t si = 0; si < samples.size(); ++si) {
            const auto& stk = samples[si];
            const auto& w = weights[si];
            if (!stk.is_array()) {
                std::ostringstream o;
                o << "profiles[" << pi << "].samples[" << si
                  << "] must be an array of frame indices";
                err = o.str();
                errKind = SpeedscopeError::ParseError;
                return false;
            }
            if (!w.is_number()) {
                std::ostringstream o;
                o << "profiles[" << pi << "].weights[" << si
                  << "] must be a number";
                err = o.str();
                errKind = SpeedscopeError::ParseError;
                return false;
            }
            // Weights are typically integers but the schema allows
            // fractional values; we round to the nearest integer when
            // multiplying by the unit (sub-ns resolution is meaningless
            // here). Negative weights are rejected — they would produce
            // a non-monotone timeline.
            double wVal = w.get<double>();
            if (wVal < 0.0) {
                std::ostringstream o;
                o << "profiles[" << pi << "].weights[" << si
                  << "] must be non-negative (got " << wVal << ')';
                err = o.str();
                errKind = SpeedscopeError::ParseError;
                return false;
            }
            std::int64_t wNs =
                static_cast<std::int64_t>(wVal * static_cast<double>(unitNs));
            cum += wNs;

            std::vector<std::string> frames;
            frames.reserve(stk.size());
            for (size_t fi = 0; fi < stk.size(); ++fi) {
                const auto& idxJ = stk[fi];
                if (!idxJ.is_number_integer()) {
                    std::ostringstream o;
                    o << "profiles[" << pi << "].samples[" << si << "]["
                      << fi << "] must be an integer frame index";
                    err = o.str();
                    errKind = SpeedscopeError::ParseError;
                    return false;
                }
                std::int64_t idx = idxJ.get<std::int64_t>();
                if (idx < 0 ||
                    static_cast<size_t>(idx) >= framePool.size()) {
                    std::ostringstream o;
                    o << "profiles[" << pi << "].samples[" << si << "]["
                      << fi << "] frame index " << idx
                      << " out of range (pool size " << framePool.size()
                      << ')';
                    err = o.str();
                    errKind = SpeedscopeError::ParseError;
                    return false;
                }
                frames.push_back(framePool[static_cast<size_t>(idx)]);
            }
            if (frames.empty()) {
                std::ostringstream o;
                o << "profiles[" << pi << "].samples[" << si
                  << "] must contain at least one frame";
                err = o.str();
                errKind = SpeedscopeError::ParseError;
                return false;
            }

            seenStacks.insert(frames);
            PendingEvent pe;
            pe.tsNs = cum;
            pe.tid = static_cast<std::int64_t>(pi);
            pe.stack = std::move(frames);
            pending.push_back(std::move(pe));
            ++totalSamples;
        }
    }

    // Stable sort by ts_ns so multi-profile timelines are monotone.
    // std::stable_sort preserves intra-profile order on ts_ns ties (which
    // happens when a profile uses `unit=none` + uniform weights).
    std::stable_sort(pending.begin(), pending.end(),
                     [](const PendingEvent& a, const PendingEvent& b) {
                         return a.tsNs < b.tsNs;
                     });

    nlohmann::json events = nlohmann::json::array();
    for (auto& pe : pending) {
        nlohmann::json evt = nlohmann::json::object();
        evt["ts_ns"] = pe.tsNs;
        evt["tid"] = pe.tid;
        nlohmann::json stackJson = nlohmann::json::array();
        for (const auto& f : pe.stack) stackJson.push_back(f);
        evt["stack"] = std::move(stackJson);
        events.push_back(std::move(evt));
    }

    nlohmann::json sampling = nlohmann::json::object();
    sampling["events"] = std::move(events);
    nlohmann::json summary = nlohmann::json::object();
    summary["total_samples"] = totalSamples;
    summary["unique_stacks"] = static_cast<std::int64_t>(seenStacks.size());
    summary["unique_threads"] = static_cast<std::int64_t>(profilesArr.size());
    sampling["summary"] = std::move(summary);
    sampling["source_format"] = "speedscope_sampled";
    outJson["sampling"] = std::move(sampling);
    return true;
}

} // namespace topo::profile
