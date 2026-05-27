// V8 `.cpuprofile` → trace sampling segment.
// See CpuProfileConverter.h for the wire contract and the rationale for
// dropping the `(root)` synthetic frame and basenaming URLs.

#include "topo/Profile/CpuProfileConverter.h"

#include "topo/Profile/FrameResolver.h"

#include <algorithm>
#include <cstdint>
#include <istream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace topo::profile {

namespace {

// Take the last path segment of a URL or path. V8 emits absolute paths
// and `file://` URLs interchangeably; downstream consumers want a stable
// short label, so we strip everything up to the last '/' or '\\'.
// Empty input → empty output (caller turns it into "<unknown>" if it
// cares).
std::string basenameOfUrl(const std::string& url) {
    if (url.empty()) return {};
    size_t slash = url.find_last_of("/\\");
    if (slash == std::string::npos) return url;
    return url.substr(slash + 1);
}

// Compose the per-frame string `<functionName>@<file>:<line>`. Anonymous
// functions (empty functionName) become `<anonymous>` so the result never
// starts with `@`. Missing url/line are tolerated — V8 emits them as ""
// and -1 for the synthetic `(root)` node, which we never call this on,
// but defensive handling avoids odd outputs if a producer omits the
// fields for a real frame.
std::string formatFrame(const std::string& functionName,
                        const std::string& url,
                        std::int64_t lineNumber) {
    std::string name =
        functionName.empty() ? std::string("<anonymous>") : functionName;
    std::string file = basenameOfUrl(url);
    if (file.empty()) file = "<unknown>";
    // V8 wire is 0-indexed; every other source-location surface in topo
    // is 1-indexed. `-1` (V8 sentinel for "no source") preserves as `0`
    // after the bump, signalling "synthetic / unknown".
    std::int64_t displayLine = lineNumber < 0 ? 0 : lineNumber + 1;
    std::ostringstream o;
    o << name << '@' << file << ':' << displayLine;
    return o.str();
}

} // namespace

bool convertCpuProfileStream(std::istream& in,
                             nlohmann::json& outJson,
                             std::string& err,
                             FrameResolver* resolver) {
    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const std::exception& e) {
        std::ostringstream o;
        o << "invalid cpuprofile JSON: " << e.what();
        err = o.str();
        return false;
    }
    if (!doc.is_object()) {
        err = "cpuprofile document must be a JSON object at the top level";
        return false;
    }
    if (!doc.contains("nodes") || !doc["nodes"].is_array()) {
        err = "missing 'nodes' array";
        return false;
    }
    if (!doc.contains("samples") || !doc["samples"].is_array()) {
        err = "missing 'samples' array";
        return false;
    }
    if (!doc.contains("timeDeltas") || !doc["timeDeltas"].is_array()) {
        err = "missing 'timeDeltas' array";
        return false;
    }
    const auto& nodesArr = doc["nodes"];
    const auto& samplesArr = doc["samples"];
    const auto& timeDeltasArr = doc["timeDeltas"];
    if (samplesArr.size() != timeDeltasArr.size()) {
        std::ostringstream o;
        o << "samples (" << samplesArr.size() << ") and timeDeltas ("
          << timeDeltasArr.size() << ") must have equal length";
        err = o.str();
        return false;
    }

    // `startTime` is absolute microseconds; default to 0 if absent so a
    // producer that omits the field still gets a monotone timeline.
    std::int64_t startTimeUs = 0;
    if (doc.contains("startTime") && doc["startTime"].is_number()) {
        // `startTime` is sometimes a double in Chromium exports; truncate.
        startTimeUs =
            static_cast<std::int64_t>(doc["startTime"].get<double>());
    }

    // First sweep: build id → node-index lookup and child → parent map.
    // V8 ids are typically dense small integers but the spec doesn't
    // require it, so we use an unordered_map keyed by id rather than
    // assuming id == index.
    std::unordered_map<std::int64_t, size_t> idToIdx;
    idToIdx.reserve(nodesArr.size());
    for (size_t i = 0; i < nodesArr.size(); ++i) {
        const auto& n = nodesArr[i];
        if (!n.is_object()) {
            std::ostringstream o;
            o << "nodes[" << i << "] must be an object";
            err = o.str();
            return false;
        }
        if (!n.contains("id") || !n["id"].is_number_integer()) {
            std::ostringstream o;
            o << "nodes[" << i << "] missing integer 'id'";
            err = o.str();
            return false;
        }
        if (!n.contains("callFrame") || !n["callFrame"].is_object()) {
            std::ostringstream o;
            o << "nodes[" << i << "] missing 'callFrame' object";
            err = o.str();
            return false;
        }
        std::int64_t id = n["id"].get<std::int64_t>();
        if (idToIdx.count(id) != 0) {
            std::ostringstream o;
            o << "nodes[" << i << "].id " << id
              << " duplicates an earlier node";
            err = o.str();
            return false;
        }
        idToIdx[id] = i;
    }

    // parentIdx[i] = node-array index of i's parent, or SIZE_MAX for the
    // root (and for orphans, which we surface as a parse error when a
    // sample references them via the walk).
    std::vector<size_t> parentIdx(nodesArr.size(), SIZE_MAX);
    for (size_t i = 0; i < nodesArr.size(); ++i) {
        const auto& n = nodesArr[i];
        if (!n.contains("children")) continue; // leaf, fine
        const auto& children = n["children"];
        if (!children.is_array()) {
            std::ostringstream o;
            o << "nodes[" << i << "].children must be an array";
            err = o.str();
            return false;
        }
        for (size_t ci = 0; ci < children.size(); ++ci) {
            const auto& c = children[ci];
            if (!c.is_number_integer()) {
                std::ostringstream o;
                o << "nodes[" << i << "].children[" << ci
                  << "] must be an integer node id";
                err = o.str();
                return false;
            }
            std::int64_t childId = c.get<std::int64_t>();
            auto it = idToIdx.find(childId);
            if (it == idToIdx.end()) {
                std::ostringstream o;
                o << "nodes[" << i << "].children[" << ci
                  << "] references unknown node id " << childId;
                err = o.str();
                return false;
            }
            size_t childIdx = it->second;
            if (parentIdx[childIdx] != SIZE_MAX) {
                std::ostringstream o;
                o << "node id " << childId
                  << " is listed as child of multiple parents (tree "
                     "invariant violated)";
                err = o.str();
                return false;
            }
            parentIdx[childIdx] = i;
        }
    }

    // Identify the root: V8 always emits exactly one `(root)` node with
    // id 1 and functionName "(root)". We don't hard-code id == 1
    // because the spec permits any id; we look for the first node with
    // no parent and functionName "(root)". If that's ambiguous we still
    // accept the no-parent node — the function-name filter is for
    // diagnostics only.
    size_t rootIdx = SIZE_MAX;
    for (size_t i = 0; i < nodesArr.size(); ++i) {
        if (parentIdx[i] == SIZE_MAX) {
            rootIdx = i;
            break;
        }
    }
    // We don't *require* a root — a malformed profile with no parent-
    // less node will fail the per-sample walk below with a clear error.

    // Pre-compute frame strings per node (excluding root) so the per-
    // sample walk is a pointer-chase plus index lookup. When a resolver
    // is given, attempt sourcemap resolution per node and bump
    // `nodesResolved` on hit so summary stats reflect coverage. We
    // resolve per-node (not per-event) so a node hit on N samples
    // counts as one resolved frame regardless of multiplicity — the
    // figure is "how many call-frames did we recover" rather than
    // weighted by sample count.
    std::vector<std::string> frameStr(nodesArr.size());
    std::vector<bool> nodeResolved(nodesArr.size(), false);
    std::int64_t nodesEmittable = 0;
    std::int64_t nodesResolved = 0;
    for (size_t i = 0; i < nodesArr.size(); ++i) {
        if (i == rootIdx) continue;
        const auto& cf = nodesArr[i]["callFrame"];
        std::string fn;
        if (cf.contains("functionName") && cf["functionName"].is_string()) {
            fn = cf["functionName"].get<std::string>();
        }
        std::string url;
        if (cf.contains("url") && cf["url"].is_string()) {
            url = cf["url"].get<std::string>();
        }
        std::int64_t line = -1;
        if (cf.contains("lineNumber") && cf["lineNumber"].is_number_integer()) {
            line = cf["lineNumber"].get<std::int64_t>();
        }
        std::int64_t column = -1;
        if (cf.contains("columnNumber") && cf["columnNumber"].is_number_integer()) {
            column = cf["columnNumber"].get<std::int64_t>();
        }
        ++nodesEmittable;
        bool resolved = false;
        if (resolver != nullptr && !url.empty() && line >= 0) {
            // V8 wire is 0-indexed; SourceMapResolver expects 1-indexed.
            int wantLine = static_cast<int>(line + 1);
            int wantCol = column < 0 ? 1 : static_cast<int>(column + 1);
            auto hit = resolver->resolve(url, wantLine, wantCol);
            if (hit.has_value()) {
                std::string name = fn.empty()
                    ? (hit->name.has_value() && !hit->name->empty()
                           ? *hit->name
                           : std::string("<anonymous>"))
                    : fn;
                std::string tsFile = basenameOfUrl(hit->source_path);
                if (tsFile.empty()) tsFile = "<unknown>";
                std::ostringstream o;
                o << name << '@' << tsFile << ':' << hit->line_1indexed;
                frameStr[i] = o.str();
                nodeResolved[i] = true;
                resolved = true;
                ++nodesResolved;
            }
        }
        if (!resolved) {
            frameStr[i] = formatFrame(fn, url, line);
        }
    }

    // Second sweep: walk each sample to a stack. timeDeltas are
    // microseconds; ts_ns = (startTime + cumDeltaUs) * 1000.
    nlohmann::json events = nlohmann::json::array();
    std::set<std::vector<std::string>> seenStacks;
    std::int64_t totalSamples = 0;
    std::int64_t cumUs = 0;

    for (size_t si = 0; si < samplesArr.size(); ++si) {
        const auto& sId = samplesArr[si];
        const auto& tD = timeDeltasArr[si];
        if (!sId.is_number_integer()) {
            std::ostringstream o;
            o << "samples[" << si << "] must be an integer node id";
            err = o.str();
            return false;
        }
        if (!tD.is_number_integer()) {
            std::ostringstream o;
            o << "timeDeltas[" << si << "] must be an integer";
            err = o.str();
            return false;
        }
        std::int64_t nodeId = sId.get<std::int64_t>();
        std::int64_t delta = tD.get<std::int64_t>();
        cumUs += delta;

        auto it = idToIdx.find(nodeId);
        if (it == idToIdx.end()) {
            std::ostringstream o;
            o << "samples[" << si << "] references unknown node id "
              << nodeId;
            err = o.str();
            return false;
        }
        size_t leafIdx = it->second;

        // Walk parents leaf → root, excluding root itself. We collect
        // leaf-first and reverse to get root → leaf, matching the other
        // converters' frame ordering. Cycle guard: a malformed profile
        // could have a parent loop; cap walk depth at nodes.size().
        std::vector<std::string> stack;
        stack.reserve(8);
        size_t cur = leafIdx;
        size_t safety = 0;
        const size_t maxDepth = nodesArr.size() + 1;
        while (cur != SIZE_MAX && cur != rootIdx) {
            if (safety++ > maxDepth) {
                std::ostringstream o;
                o << "samples[" << si
                  << "]: parent walk exceeded " << maxDepth
                  << " hops (likely cycle in nodes[].children)";
                err = o.str();
                return false;
            }
            stack.push_back(frameStr[cur]);
            cur = parentIdx[cur];
        }
        // Reverse to root → leaf ordering.
        std::reverse(stack.begin(), stack.end());

        if (stack.empty()) {
            // Sample landed directly on (root) — V8 occasionally emits
            // these for idle ticks. Skip them so consumers don't see
            // empty-stack events (which break every flamegraph tool we
            // know of). Still bump cumUs above.
            continue;
        }

        nlohmann::json evt = nlohmann::json::object();
        // V8 µs → ns.
        evt["ts_ns"] = (startTimeUs + cumUs) * 1000;
        evt["tid"] = static_cast<std::int64_t>(0);
        nlohmann::json stackJson = nlohmann::json::array();
        for (const auto& f : stack) stackJson.push_back(f);
        evt["stack"] = std::move(stackJson);
        events.push_back(std::move(evt));
        seenStacks.insert(std::move(stack));
        ++totalSamples;
    }

    nlohmann::json sampling = nlohmann::json::object();
    sampling["events"] = std::move(events);
    nlohmann::json summary = nlohmann::json::object();
    summary["total_samples"] = totalSamples;
    summary["unique_stacks"] = static_cast<std::int64_t>(seenStacks.size());
    // V8's `.cpuprofile` is per-isolate; worker-thread merging is a
    // follow-up. Fixed `unique_threads = 1` matches `tid = 0` per event.
    summary["unique_threads"] = static_cast<std::int64_t>(1);
    // sourcemap resolution coverage. `frames_total` counts
    // emittable call-frames (excludes the synthetic (root) node);
    // `frames_resolved_via_sourcemap` is the subset that translated to
    // a `.ts` location. Both are 0 when no resolver was supplied.
    summary["frames_total"] = nodesEmittable;
    summary["frames_resolved_via_sourcemap"] = nodesResolved;
    sampling["summary"] = std::move(summary);
    // Discriminate the post-sourcemap output so downstream tooling sees
    // immediately whether frames have been translated. The original
    // `v8_cpuprofile` label is preserved when no frame resolved (either
    // because no resolver was given or because no map matched any URL),
    // so existing consumers continue to recognise unmapped output.
    sampling["source_format"] =
        nodesResolved > 0 ? "cpuprofile_with_sourcemap" : "v8_cpuprofile";
    outJson["sampling"] = std::move(sampling);
    return true;
}

} // namespace topo::profile
