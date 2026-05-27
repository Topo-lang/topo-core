#include "topo/Profile/SysMonitoringNdjsonConverter.h"

#include <istream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace topo::profile {
namespace {

// Flatten one `frame` object to `<module>.<function>:<line>` (the `:line`
// suffix dropped when line <= 0). Identical convention to the cProfile /
// JFR sibling converters so the six sampling formats stay interchangeable.
bool flattenFrame(const nlohmann::json& frame, std::string& out) {
    if (!frame.is_object() || !frame.contains("function") ||
        !frame["function"].is_string()) {
        return false;
    }
    std::string mod = frame.value("module", "");
    // Strip a trailing `.py` so `app.py` → `app` (matches cProfile's
    // os.path.basename-then-stem intent without re-deriving it here).
    if (mod.size() > 3 && mod.compare(mod.size() - 3, 3, ".py") == 0) {
        mod.erase(mod.size() - 3);
    }
    const std::string fn = frame["function"].get<std::string>();
    long long line = 0;
    if (frame.contains("line") && frame["line"].is_number_integer()) {
        line = frame["line"].get<long long>();
    }
    std::ostringstream o;
    if (!mod.empty()) o << mod << '.';
    o << fn;
    if (line > 0) o << ':' << line;
    out = o.str();
    return true;
}

struct OpenFrame {
    std::int64_t tsNs = 0;
    std::string flat;
};

} // namespace

bool convertSysMonitoringNdjsonStream(std::istream& in,
                                      nlohmann::json& outJson,
                                      std::string& err) {
    nlohmann::json events = nlohmann::json::array();
    std::int64_t totalSamples = 0;
    std::set<std::vector<std::string>> uniqueStacks;
    std::set<std::int64_t> uniqueThreads;

    // Per-thread stack of open PY_START frames. A PY_RETURN pops the
    // innermost; the emitted sample's stack is the full open chain
    // (root→leaf) captured at START time.
    std::map<std::int64_t, std::vector<OpenFrame>> openByThread;

    auto emitSample = [&](std::int64_t tid, std::int64_t startNs,
                          std::int64_t endNs,
                          const std::vector<std::string>& chain) {
        nlohmann::json ev = nlohmann::json::object();
        ev["ts_ns"] = startNs;
        ev["tid"] = tid;
        ev["duration_ns"] = (endNs >= startNs) ? (endNs - startNs) : 0;
        nlohmann::json stack = nlohmann::json::array();
        for (const auto& f : chain) stack.push_back(f);
        ev["stack"] = std::move(stack);
        events.push_back(std::move(ev));
        ++totalSamples;
        uniqueStacks.insert(chain);
        uniqueThreads.insert(tid);
    };

    std::string line;
    std::size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;

        nlohmann::json evt;
        try {
            evt = nlohmann::json::parse(line);
        } catch (const std::exception& e) {
            std::ostringstream o;
            o << "line " << lineNo << ": invalid JSON (" << e.what() << ')';
            err = o.str();
            return false;
        }

        if (!evt.contains("event_type") || !evt["event_type"].is_string()) {
            std::ostringstream o;
            o << "line " << lineNo << ": missing or non-string 'event_type'";
            err = o.str();
            return false;
        }
        const std::string et = evt["event_type"].get<std::string>();
        if (et != "py_start" && et != "py_return") {
            std::ostringstream o;
            o << "line " << lineNo << ": unexpected event_type '" << et
              << "' (py_sys_monitoring expects 'py_start' or 'py_return')";
            err = o.str();
            return false;
        }
        if (!evt.contains("ts_ns") || !evt["ts_ns"].is_number_integer()) {
            std::ostringstream o;
            o << "line " << lineNo << ": missing or non-integer 'ts_ns'";
            err = o.str();
            return false;
        }
        if (!evt.contains("thread") || !evt["thread"].is_object() ||
            !evt["thread"].contains("id") ||
            !evt["thread"]["id"].is_number_integer()) {
            std::ostringstream o;
            o << "line " << lineNo << ": missing or non-integer 'thread.id'";
            err = o.str();
            return false;
        }
        if (!evt.contains("frame")) {
            std::ostringstream o;
            o << "line " << lineNo << ": missing 'frame'";
            err = o.str();
            return false;
        }
        std::string flat;
        if (!flattenFrame(evt["frame"], flat)) {
            std::ostringstream o;
            o << "line " << lineNo << ": malformed 'frame' (need string "
                 "'function', optional 'module'/'line')";
            err = o.str();
            return false;
        }

        const std::int64_t tid = evt["thread"]["id"].get<std::int64_t>();
        const std::int64_t tsNs = evt["ts_ns"].get<std::int64_t>();
        auto& stack = openByThread[tid];

        if (et == "py_start") {
            stack.push_back(OpenFrame{tsNs, flat});
            continue;
        }
        // py_return: pop the innermost open START on this thread. Emit a
        // sample for the closed frame with the full open chain at the
        // moment of START (root→leaf, inclusive of the closing frame).
        if (stack.empty()) {
            // Unmatched RETURN — defensively skip (a monitored callback
            // can miss its paired START if monitoring was enabled mid-call).
            continue;
        }
        std::vector<std::string> chain;
        chain.reserve(stack.size());
        for (const auto& f : stack) chain.push_back(f.flat);
        const std::int64_t startNs = stack.back().tsNs;
        stack.pop_back();
        emitSample(tid, startNs, tsNs, chain);
    }

    // Flush frames still open at EOF (long-lived entry points / a target
    // that exits without unwinding). Duration 0 so they are never dropped.
    for (auto& [tid, stack] : openByThread) {
        std::vector<std::string> chain;
        for (const auto& f : stack) chain.push_back(f.flat);
        while (!stack.empty()) {
            std::vector<std::string> c(chain.begin(),
                                       chain.begin() +
                                           static_cast<long>(stack.size()));
            const std::int64_t startNs = stack.back().tsNs;
            stack.pop_back();
            emitSample(tid, startNs, startNs, c);
        }
    }

    if (totalSamples == 0) {
        err = "no py_start/py_return events found (sys.monitoring run "
              "produced no PY_START/PY_RETURN pairs)";
        return false;
    }

    nlohmann::json sampling = nlohmann::json::object();
    sampling["events"] = std::move(events);
    nlohmann::json summary = nlohmann::json::object();
    summary["total_samples"] = totalSamples;
    summary["unique_stacks"] =
        static_cast<std::int64_t>(uniqueStacks.size());
    summary["unique_threads"] =
        static_cast<std::int64_t>(uniqueThreads.size());
    sampling["summary"] = std::move(summary);
    sampling["source_format"] = "py_sys_monitoring";
    outJson["sampling"] = std::move(sampling);
    return true;
}

} // namespace topo::profile
