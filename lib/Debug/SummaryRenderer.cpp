// Pure-string summary template renderer.
//
// Two passes: tokenise the template into Segment[] (this file, no allocation
// of the resolver), then render Segment[] with a caller-provided resolver
// (CLI, /summary HTTP route, ...). Splitting parse + render lets the
// consumer cache the parsed form across multiple invocations against the
// same dbg.json and run distinct-placeholder dispatch upstream of render.

#include "topo/Debug/SummaryRenderer.h"

#include <sstream>
#include <unordered_set>

namespace topo::debug_summary {

namespace {
std::string trim(std::string s) {
    auto isWs = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    size_t i = 0;
    while (i < s.size() && isWs(s[i])) ++i;
    s.erase(0, i);
    while (!s.empty() && isWs(s.back())) s.pop_back();
    return s;
}
} // namespace

bool parseTemplate(const std::string& tmpl,
                   std::vector<Segment>& out,
                   std::string& err) {
    out.clear();
    err.clear();

    std::string lit;
    size_t litStart = 0;
    auto flushLit = [&]() {
        if (lit.empty()) return;
        Segment s;
        s.isPlaceholder = false;
        s.text = std::move(lit);
        s.pos = litStart;
        out.push_back(std::move(s));
        lit.clear();
    };

    size_t i = 0;
    const size_t n = tmpl.size();
    while (i < n) {
        char c = tmpl[i];
        if (c == '{') {
            // {{  → literal '{'
            if (i + 1 < n && tmpl[i + 1] == '{') {
                if (lit.empty()) litStart = i;
                lit.push_back('{');
                i += 2;
                continue;
            }
            // {expr}  → placeholder
            flushLit();
            size_t exprStart = i + 1;
            size_t j = exprStart;
            int depth = 1;
            while (j < n && depth > 0) {
                if (tmpl[j] == '{') {
                    err = "unexpected '{' inside placeholder at position " +
                          std::to_string(j) + " (use '{{' to escape a literal brace)";
                    out.clear();
                    return false;
                }
                if (tmpl[j] == '}') { --depth; if (depth == 0) break; }
                ++j;
            }
            if (depth != 0) {
                err = "unterminated '{' starting at position " + std::to_string(i);
                out.clear();
                return false;
            }
            Segment s;
            s.isPlaceholder = true;
            s.text = trim(tmpl.substr(exprStart, j - exprStart));
            s.pos = i;
            if (s.text.empty()) {
                err = "empty placeholder '{}' at position " + std::to_string(i);
                out.clear();
                return false;
            }
            out.push_back(std::move(s));
            i = j + 1;
        } else if (c == '}') {
            // }} → literal '}'
            if (i + 1 < n && tmpl[i + 1] == '}') {
                if (lit.empty()) litStart = i;
                lit.push_back('}');
                i += 2;
                continue;
            }
            err = "unexpected '}' at position " + std::to_string(i) +
                  " (use '}}' to escape a literal brace)";
            out.clear();
            return false;
        } else {
            if (lit.empty()) litStart = i;
            lit.push_back(c);
            ++i;
        }
    }
    flushLit();
    return true;
}

std::vector<std::string> distinctPlaceholders(const std::vector<Segment>& segments) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& s : segments) {
        if (!s.isPlaceholder) continue;
        if (seen.insert(s.text).second) out.push_back(s.text);
    }
    return out;
}

bool render(const std::vector<Segment>& segments,
            const PlaceholderResolver& resolver,
            std::string& out,
            std::string& err) {
    out.clear();
    err.clear();

    // Cache resolver results so a repeated `{expr}` only spawns the adapter
    // once. The cache lives only for this render call — the consumer can
    // additionally cache across calls if it wants by sharing a resolver
    // closure that memoises.
    std::vector<std::pair<std::string, std::string>> cache;
    auto cached = [&](const std::string& expr, std::string& valueOut) -> bool {
        for (const auto& kv : cache) {
            if (kv.first == expr) { valueOut = kv.second; return true; }
        }
        return false;
    };

    for (const auto& s : segments) {
        if (!s.isPlaceholder) {
            out += s.text;
            continue;
        }
        std::string value;
        if (!cached(s.text, value)) {
            std::string resolverErr;
            if (!resolver(s.text, value, resolverErr)) {
                std::ostringstream os;
                os << "placeholder '{" << s.text << "}' at position " << s.pos
                   << ": " << resolverErr;
                err = os.str();
                out.clear();
                return false;
            }
            cache.emplace_back(s.text, value);
        }
        out += value;
    }
    return true;
}

} // namespace topo::debug_summary
