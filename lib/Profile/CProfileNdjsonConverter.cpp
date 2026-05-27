// cProfile-NDJSON → trace sampling segment.
// See CProfileNdjsonConverter.h for the wire contract and the rationale
// for routing through an NDJSON intermediate emitted by the harness
// `topo_profile_python.cprofile_harness`.

#include "topo/Profile/CProfileNdjsonConverter.h"

#include <cctype>
#include <istream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace topo::profile {

namespace {

// Trim leading/trailing ASCII whitespace in place. Same shape as the
// helpers in SamplingConverter / JfrNdjsonConverter; duplicated to keep
// each converter as a self-contained translation unit.
void trim(std::string& s) {
    size_t b = 0;
    while (b < s.size() &&
           std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b &&
           std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    if (b > 0 || e < s.size()) s = s.substr(b, e - b);
}

// Flatten a cProfile-shaped stack frame object to a `<module>.<function>:<line>`
// string. The `:<line>` suffix is dropped when line <= 0 so built-in /
// synthesized entries (which the harness records with line 0) read cleanly.
//
// Returns false (and leaves `out` untouched) if the frame is missing
// required fields, which the caller turns into a parse error with line
// number context.
bool flattenFrame(const nlohmann::json& frame, std::string& out) {
    if (!frame.is_object()) return false;
    if (!frame.contains("module") || !frame["module"].is_string()) return false;
    if (!frame.contains("function") || !frame["function"].is_string()) return false;
    const std::string& mod = frame["module"].get_ref<const std::string&>();
    const std::string& fn  = frame["function"].get_ref<const std::string&>();
    if (fn.empty()) return false;
    std::int64_t line = 0;
    if (frame.contains("line") && frame["line"].is_number_integer()) {
        line = frame["line"].get<std::int64_t>();
    }
    out.clear();
    out.reserve(mod.size() + 1 + fn.size() + (line > 0 ? 12 : 0));
    out.append(mod);
    out.push_back('.');
    out.append(fn);
    if (line > 0) {
        out.push_back(':');
        out.append(std::to_string(line));
    }
    return true;
}

} // namespace

bool convertCProfileNdjsonStream(std::istream& in,
                                 nlohmann::json& outJson,
                                 std::string& err) {
    nlohmann::json events = nlohmann::json::array();
    std::int64_t totalSamples = 0;
    std::set<std::vector<std::string>> seenStacks;
    std::set<std::int64_t> seenThreads;

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        // CRLF tolerance — mirrors the other NDJSON readers.
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
        // Strict event_type gate — this converter is dedicated to the
        // cProfile harness wire format, so any other event_type is almost
        // certainly a misconfigured pipeline. Reject loudly rather than
        // silently re-interpreting JFR events as Python calls.
        if (!evt.contains("event_type") || !evt["event_type"].is_string()) {
            std::ostringstream o;
            o << "line " << lineNo
              << ": missing or non-string 'event_type' "
                 "(cprofile_ndjson expects 'py_call')";
            err = o.str();
            return false;
        }
        {
            const std::string& et =
                evt["event_type"].get_ref<const std::string&>();
            if (et != "py_call") {
                std::ostringstream o;
                o << "line " << lineNo << ": unexpected event_type '" << et
                  << "' (cprofile_ndjson expects 'py_call')";
                err = o.str();
                return false;
            }
        }
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
        if (!evt.contains("duration_ns") ||
            !evt["duration_ns"].is_number_integer()) {
            std::ostringstream o;
            o << "line " << lineNo
              << ": missing or non-integer 'duration_ns'";
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
                  << "] must be an object with non-empty 'function' and "
                     "string 'module' (line optional, integer when present)";
                err = o.str();
                return false;
            }
            stack.push_back(std::move(frameStr));
        }

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
        outEvt["duration_ns"] = evt["duration_ns"].get<std::int64_t>();
        nlohmann::json stackJson = nlohmann::json::array();
        for (const auto& f : stack) stackJson.push_back(f);
        outEvt["stack"] = std::move(stackJson);
        events.push_back(std::move(outEvt));

        seenStacks.insert(std::move(stack));
        ++totalSamples;
    }

    if (totalSamples == 0) {
        // A cProfile run that produced zero edges is evidence the harness
        // failed to start the target or the target was empty. Fail loudly
        // — silently emitting an empty sampling segment hides the problem.
        err = "no py_call events found in cprofile NDJSON input "
              "(harness produced no recorded call edges)";
        return false;
    }

    nlohmann::json sampling = nlohmann::json::object();
    sampling["events"] = std::move(events);
    nlohmann::json summary = nlohmann::json::object();
    // Sampling-summary field names are unified across all source formats:
    // `total_samples` / `unique_stacks` / `unique_threads`. Consumers read
    // these keys generically regardless of `source_format`.
    summary["total_samples"] = totalSamples;
    summary["unique_stacks"] =
        static_cast<std::int64_t>(seenStacks.size());
    summary["unique_threads"] = static_cast<std::int64_t>(seenThreads.size());
    sampling["summary"] = std::move(summary);
    sampling["source_format"] = "cprofile_ndjson";
    outJson["sampling"] = std::move(sampling);
    return true;
}

} // namespace topo::profile
