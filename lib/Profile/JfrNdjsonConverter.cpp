// JFR-NDJSON → trace sampling segment.
// See JfrNdjsonConverter.h for the wire contract and rationale for the
// NDJSON intermediate (instead of consuming `.jfr` binary directly).

#include "topo/Profile/JfrNdjsonConverter.h"

#include <cctype>
#include <istream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace topo::profile {

namespace {

// Trim leading/trailing ASCII whitespace in place. Matches the helper in
// SamplingConverter.cpp; duplicated rather than shared because the two
// converters are otherwise independent units and a 6-line helper isn't
// worth a Util header.
void trim(std::string& s) {
    size_t b = 0;
    while (b < s.size() &&
           std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b &&
           std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    if (b > 0 || e < s.size()) s = s.substr(b, e - b);
}

// Flatten a JFR stack frame object to a `<class>.<method>` string. JFR
// always carries class+method for `jdk.ExecutionSample`; line numbers are
// optional and currently dropped (we keep the public schema as plain
// strings to match the folded converter).
//
// Returns false (and leaves out empty) if the frame is missing required
// fields, which the caller turns into a parse error with line number.
bool flattenFrame(const nlohmann::json& frame, std::string& out) {
    if (!frame.is_object()) return false;
    if (!frame.contains("class") || !frame["class"].is_string()) return false;
    if (!frame.contains("method") || !frame["method"].is_string()) return false;
    const std::string& cls = frame["class"].get_ref<const std::string&>();
    const std::string& mth = frame["method"].get_ref<const std::string&>();
    if (cls.empty() || mth.empty()) return false;
    out.clear();
    out.reserve(cls.size() + 1 + mth.size());
    out.append(cls);
    out.push_back('.');
    out.append(mth);
    return true;
}

} // namespace

namespace {

// `topo.pass.<PassName>` events are per-Pass profile events
// injected by the JVM transform (AdaptivePass / ArenaPass / ParallelPass /
// PipelinePass via dev.topo.PassEvents) and forwarded stackless by the
// jfr-bridge under a "fields" object. They are NOT sampling events — route
// them into the top-level `pass_events.<PassName>[]` map instead, mirroring
// the LLVM-side pass_event wire (topo-profile main.cpp routePassEvent).
// Returns true if the line was a pass event and was routed (caller then
// skips the sampling path); false if it is an ordinary sampling event.
//
// This is intentionally additive: any event_type that does not start with
// `topo.pass.` (notably jdk.ExecutionSample) falls straight through to the
// existing sampling logic unchanged.
constexpr const char* kPassPrefix = "topo.pass.";

bool routeJfrPassEvent(const nlohmann::json& evt, nlohmann::json& passEvents) {
    if (!evt.contains("event_type") || !evt["event_type"].is_string()) {
        return false;
    }
    const std::string& et = evt["event_type"].get_ref<const std::string&>();
    const std::size_t plen = std::char_traits<char>::length(kPassPrefix);
    if (et.size() <= plen || et.compare(0, plen, kPassPrefix) != 0) {
        return false;
    }
    std::string pass = et.substr(plen); // e.g. "AdaptivePass"

    nlohmann::json ev = nlohmann::json::object();
    ev["ts_ns"] = evt.value("ts_ns", static_cast<std::int64_t>(0));
    if (evt.contains("thread") && evt["thread"].is_object()) {
        const auto& th = evt["thread"];
        if (th.contains("id") && th["id"].is_number_integer()) {
            ev["tid"] = th["id"].get<std::int64_t>();
        }
    }
    // Forward the user-declared event fields verbatim. The bridge already
    // typed scalars (numbers/bools stay JSON numbers/bools, everything
    // else is a string), so a straight copy preserves them.
    if (evt.contains("fields") && evt["fields"].is_object()) {
        ev["fields"] = evt["fields"];
    } else {
        ev["fields"] = nlohmann::json::object();
    }

    if (!passEvents.contains(pass)) passEvents[pass] = nlohmann::json::array();
    passEvents[pass].push_back(std::move(ev));
    return true;
}

} // namespace

bool convertJfrNdjsonStream(std::istream& in,
                            nlohmann::json& outJson,
                            std::string& err) {
    nlohmann::json events = nlohmann::json::array();
    nlohmann::json passEvents = nlohmann::json::object();
    std::int64_t totalSamples = 0;
    // Track stack-string-tuples to count unique stacks. Using set<vector>
    // keeps the comparison structural (frame-by-frame) without hashing.
    std::set<std::vector<std::string>> seenStacks;
    std::set<std::int64_t> seenThreads;

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        // Strip trailing \r for CRLF tolerance.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        nlohmann::json evt;
        try {
            evt = nlohmann::json::parse(line);
        } catch (const std::exception& e) {
            std::ostringstream o;
            o << "line " << lineNo << ": invalid JSON (" << e.what() << ')';
            err = o.str();
            return false;
        }
        if (!evt.is_object()) {
            std::ostringstream o;
            o << "line " << lineNo << ": expected JSON object, got "
              << evt.type_name();
            err = o.str();
            return false;
        }
        // route per-Pass profile events out of the sampling
        // path BEFORE the stack/ts_ns requirements (pass events are
        // stackless). Additive: non-pass events fall through unchanged.
        if (routeJfrPassEvent(evt, passEvents)) continue;
        if (!evt.contains("ts_ns") || !evt["ts_ns"].is_number_integer()) {
            std::ostringstream o;
            o << "line " << lineNo << ": missing or non-integer 'ts_ns'";
            err = o.str();
            return false;
        }
        if (!evt.contains("stack") || !evt["stack"].is_array()) {
            std::ostringstream o;
            o << "line " << lineNo << ": missing 'stack' array";
            err = o.str();
            return false;
        }
        const auto& stackArr = evt["stack"];
        if (stackArr.empty()) {
            std::ostringstream o;
            o << "line " << lineNo << ": 'stack' array must be non-empty";
            err = o.str();
            return false;
        }

        std::vector<std::string> stack;
        stack.reserve(stackArr.size());
        for (size_t fi = 0; fi < stackArr.size(); ++fi) {
            std::string frameStr;
            if (!flattenFrame(stackArr[fi], frameStr)) {
                std::ostringstream o;
                o << "line " << lineNo << ": stack[" << fi
                  << "] must be an object with non-empty 'class' and 'method'";
                err = o.str();
                return false;
            }
            stack.push_back(std::move(frameStr));
        }

        // Thread id — JFR shape is `{"id":N,"name":"..."}`, but the id is
        // the only field we use. If absent, default to 0 (matches the
        // folded converter for missing-tid cases).
        std::int64_t tid = 0;
        if (evt.contains("thread") && evt["thread"].is_object()) {
            const auto& th = evt["thread"];
            if (th.contains("id") && th["id"].is_number_integer()) {
                tid = th["id"].get<std::int64_t>();
            }
        }
        seenThreads.insert(tid);

        nlohmann::json outEvt = nlohmann::json::object();
        outEvt["ts_ns"] = evt["ts_ns"].get<std::int64_t>();
        outEvt["tid"] = tid;
        nlohmann::json stackJson = nlohmann::json::array();
        for (const auto& f : stack) stackJson.push_back(f);
        outEvt["stack"] = std::move(stackJson);
        events.push_back(std::move(outEvt));

        seenStacks.insert(std::move(stack));
        ++totalSamples;
    }

    nlohmann::json sampling = nlohmann::json::object();
    sampling["events"] = std::move(events);
    nlohmann::json summary = nlohmann::json::object();
    summary["total_samples"] = totalSamples;
    summary["unique_stacks"] = static_cast<std::int64_t>(seenStacks.size());
    summary["unique_threads"] = static_cast<std::int64_t>(seenThreads.size());
    sampling["summary"] = std::move(summary);
    sampling["source_format"] = "jdk_jfr_ndjson";
    outJson["sampling"] = std::move(sampling);
    // only surface `pass_events` when non-empty so a
    // sample-only recording stays byte-for-byte identical to pre-Plan-42
    // output (same "absent, not empty" convention as topo-profile's
    // span pass_events handling).
    if (!passEvents.empty()) {
        outJson["pass_events"] = std::move(passEvents);
    }
    return true;
}

} // namespace topo::profile
