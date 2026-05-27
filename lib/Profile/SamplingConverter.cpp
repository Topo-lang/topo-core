// perf-script (stackcollapse folded) → trace
// sampling segment. See SamplingConverter.h for the format contract.

#include "topo/Profile/SamplingConverter.h"

#include <cctype>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

namespace topo::profile {

namespace {

// Split a folded line on ';' into frame strings. Empty frames (`a;;b`) are
// skipped — they're a pathological encoding that no downstream consumer
// cares about.
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

// Trim leading/trailing ASCII whitespace in place.
void trim(std::string& s) {
    size_t b = 0;
    while (b < s.size() &&
           std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b &&
           std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    if (b > 0 || e < s.size()) s = s.substr(b, e - b);
}

// Parse a non-negative decimal integer from `s` (entire string). Returns
// false if `s` is empty or contains anything other than digits.
bool parseNonNegInt(const std::string& s, std::int64_t& out) {
    if (s.empty()) return false;
    std::int64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        // Overflow guard — folded counts in practice are <10^9, but cheap to
        // check.
        if (v > (INT64_MAX - 9) / 10) return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

} // namespace

bool convertFoldedStream(std::istream& in,
                         nlohmann::json& outJson,
                         std::string& err,
                         const SamplingConverterOptions& opts) {
    nlohmann::json events = nlohmann::json::array();
    std::int64_t totalSamples = 0;
    std::int64_t uniqueStacks = 0;
    std::int64_t nextTs = opts.baseTsNs;

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        // Strip trailing \r for CRLF tolerance.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        // Split off the trailing whitespace-delimited count.
        size_t sp = line.find_last_of(" \t");
        if (sp == std::string::npos) {
            std::ostringstream o;
            o << "line " << lineNo << ": missing sample count";
            err = o.str();
            return false;
        }
        std::string countStr = line.substr(sp + 1);
        std::string stackPart = line.substr(0, sp);
        // Trim trailing whitespace on stack part (between stack and count
        // there may be multiple spaces).
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
        if (count == 0) continue; // zero-count lines are legal, just no-op
        ++uniqueStacks;
        totalSamples += count;

        // Pre-build the stack JSON once and copy per sample. Copying a small
        // JSON array of strings is cheaper than rebuilding it `count` times.
        nlohmann::json stackJson = nlohmann::json::array();
        for (const auto& f : frames) stackJson.push_back(f);

        for (std::int64_t i = 0; i < count; ++i) {
            nlohmann::json evt = nlohmann::json::object();
            evt["ts_ns"] = nextTs;
            evt["tid"] = opts.tid;
            evt["stack"] = stackJson;
            events.push_back(std::move(evt));
            nextTs += opts.stepNs;
        }
    }

    nlohmann::json sampling = nlohmann::json::object();
    sampling["events"] = std::move(events);
    nlohmann::json summary = nlohmann::json::object();
    summary["total_samples"] = totalSamples;
    summary["unique_stacks"] = uniqueStacks;
    sampling["summary"] = std::move(summary);
    sampling["source_format"] = "stackcollapse_folded";
    outJson["sampling"] = std::move(sampling);
    return true;
}

} // namespace topo::profile
