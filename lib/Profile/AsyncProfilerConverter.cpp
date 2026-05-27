// async-profiler collapsed → trace sampling segment.
// See AsyncProfilerConverter.h for the format contract and the
// `[tid] name` thread-root extension this converter lifts out.

#include "topo/Profile/AsyncProfilerConverter.h"

#include <cctype>
#include <istream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace topo::profile {

namespace {

// Split a folded stack on ';' into frame strings. Empty frames (`a;;b`)
// are skipped — matches the stackcollapse-folded converter's tolerance.
std::vector<std::string> splitStack(const std::string& stackPart) {
    std::vector<std::string> frames;
    std::string cur;
    cur.reserve(stackPart.size());
    for (char c : stackPart) {
        if (c == ';') {
            if (!cur.empty()) frames.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) frames.push_back(std::move(cur));
    return frames;
}

void trim(std::string& s) {
    size_t b = 0;
    while (b < s.size() &&
           std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b &&
           std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    if (b > 0 || e < s.size()) s = s.substr(b, e - b);
}

bool parseNonNegInt(const std::string& s, std::int64_t& out) {
    if (s.empty()) return false;
    std::int64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        if (v > (INT64_MAX - 9) / 10) return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

// async-profiler `-t` emits the stack root as `[<tid>] <thread-name>`.
// If `frame` matches that exact shape, lift the numeric tid out and
// return true; otherwise return false (the frame is a normal stack frame
// and the event keeps tid 0). The thread-name itself is dropped from the
// stack — the stack should be call frames, and the tid carries the thread
// identity in the trace schema (mirrors the JFR converter, which
// also keeps thread identity out of `stack`).
bool liftThreadRoot(const std::string& frame, std::int64_t& tid) {
    if (frame.size() < 4 || frame[0] != '[') return false;
    size_t close = frame.find(']');
    if (close == std::string::npos || close == 1) return false;
    std::string num = frame.substr(1, close - 1);
    std::int64_t v = 0;
    if (!parseNonNegInt(num, v)) return false;
    // Require a space + non-empty thread name after `]` so a literal
    // frame that merely starts with `[N]` (rare, but possible in mangled
    // names) is not misread as a thread root.
    if (close + 1 >= frame.size() || frame[close + 1] != ' ') return false;
    if (frame.substr(close + 2).empty()) return false;
    tid = v;
    return true;
}

} // namespace

bool convertAsyncProfilerCollapsedStream(
    std::istream& in,
    nlohmann::json& outJson,
    std::string& err,
    const AsyncProfilerConverterOptions& opts) {
    nlohmann::json events = nlohmann::json::array();
    std::int64_t totalSamples = 0;
    std::set<std::vector<std::string>> seenStacks;
    std::set<std::int64_t> seenThreads;
    std::int64_t nextTs = opts.baseTsNs;

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        size_t sp = line.find_last_of(" \t");
        if (sp == std::string::npos) {
            std::ostringstream o;
            o << "line " << lineNo << ": missing sample count";
            err = o.str();
            return false;
        }
        std::string countStr = line.substr(sp + 1);
        std::string stackPart = line.substr(0, sp);
        while (!stackPart.empty() &&
               std::isspace(static_cast<unsigned char>(stackPart.back()))) {
            stackPart.pop_back();
        }

        std::int64_t count = 0;
        if (!parseNonNegInt(countStr, count)) {
            std::ostringstream o;
            o << "line " << lineNo << ": sample count must be a "
              << "non-negative integer (got '" << countStr << "')";
            err = o.str();
            return false;
        }
        std::vector<std::string> frames = splitStack(stackPart);
        if (frames.empty()) {
            std::ostringstream o;
            o << "line " << lineNo << ": stack is empty";
            err = o.str();
            return false;
        }
        if (count == 0) continue; // legal no-op line

        // Lift the async-profiler `[tid] name` thread root if present so
        // events are grouped by thread; otherwise tid defaults to 0.
        std::int64_t tid = 0;
        if (liftThreadRoot(frames.front(), tid)) {
            frames.erase(frames.begin());
            if (frames.empty()) {
                std::ostringstream o;
                o << "line " << lineNo
                  << ": stack is empty after stripping the [tid] thread root";
                err = o.str();
                return false;
            }
        }
        seenThreads.insert(tid);
        // total_samples counts expanded samples (one event per `count`),
        // matching the stackcollapse-folded converter; unique_stacks counts
        // distinct frame tuples via seenStacks.
        totalSamples += count;

        nlohmann::json stackJson = nlohmann::json::array();
        for (const auto& f : frames) stackJson.push_back(f);

        for (std::int64_t i = 0; i < count; ++i) {
            nlohmann::json evt = nlohmann::json::object();
            evt["ts_ns"] = nextTs;
            evt["tid"] = tid;
            evt["stack"] = stackJson;
            events.push_back(std::move(evt));
            nextTs += opts.stepNs;
        }
        seenStacks.insert(std::move(frames));
    }

    nlohmann::json sampling = nlohmann::json::object();
    sampling["events"] = std::move(events);
    nlohmann::json summary = nlohmann::json::object();
    summary["total_samples"] = totalSamples;
    summary["unique_stacks"] =
        static_cast<std::int64_t>(seenStacks.size());
    summary["unique_threads"] =
        static_cast<std::int64_t>(seenThreads.size());
    sampling["summary"] = std::move(summary);
    sampling["source_format"] = "async_profiler_collapsed";
    outJson["sampling"] = std::move(sampling);
    return true;
}

} // namespace topo::profile
