// Minimal mustache-subset template engine implementation.
//
// Single-pass recursive parser. The grammar is intentionally small enough
// that a hand-written tokenizer beats pulling a dependency:
//
//   text       = run of literal chars
//   tag        = '{{' ('{' raw '}}}' | '!' comment '}}' | '#if' k '}}' ...
//                       | '#each' k '}}' ... | '/' k '}}' | path '}}')
//
// Blocks (`#if` / `#each`) recurse: parseNodes consumes until it hits the
// matching `{{/key}}` (or EOF/another close, which is reported as an error).

#include "topo/Debug/TemplateEngine.h"

#include <cctype>
#include <sstream>

namespace topo::debug {

using nlohmann::json;

std::string htmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}

namespace {

// Resolve a scalar JSON value to its display string. Objects/arrays are
// rendered as compact JSON so a misused `{{obj}}` is at least visible.
std::string scalarToString(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_null()) return "";
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) {
        std::ostringstream os;
        os << v.get<double>();
        return os.str();
    }
    return v.dump();
}

// Walk a dotted path against `ctx`. Returns a pointer to the resolved JSON
// node or nullptr when any segment is missing. `.` (a lone dot) means the
// current context itself (used inside {{#each}}).
const json* resolvePath(const json& ctx, const std::string& path) {
    if (path == ".") return &ctx;
    const json* cur = &ctx;
    size_t i = 0;
    while (i < path.size()) {
        size_t dot = path.find('.', i);
        std::string seg = (dot == std::string::npos)
                               ? path.substr(i)
                               : path.substr(i, dot - i);
        if (seg.empty()) return nullptr;
        if (!cur->is_object()) return nullptr;
        auto it = cur->find(seg);
        if (it == cur->end()) return nullptr;
        cur = &(*it);
        if (dot == std::string::npos) break;
        i = dot + 1;
    }
    return cur;
}

// Mustache-style truthiness for {{#if}}: absent / null / false / 0 / "" /
// [] / {} are falsy; everything else truthy.
bool truthy(const json* v) {
    if (!v || v->is_null()) return false;
    if (v->is_boolean()) return v->get<bool>();
    if (v->is_number()) return v->get<double>() != 0.0;
    if (v->is_string()) return !v->get<std::string>().empty();
    if (v->is_array() || v->is_object()) return !v->empty();
    return true;
}

struct Parser {
    const std::string& src;
    size_t pos = 0;
    std::string error;

    explicit Parser(const std::string& s) : src(s) {}

    // Render `src[pos..]` against `ctx` into `out` until either EOF or a
    // `{{/...}}` whose key is in `closers`. On return, `pos` points just
    // past the consumed close tag (or at EOF). `*matched` (if non-null) is
    // set to the close-tag key actually seen.
    bool render(const json& ctx, std::string& out, bool atTop,
                std::string* matched) {
        while (pos < src.size()) {
            size_t open = src.find("{{", pos);
            if (open == std::string::npos) {
                out.append(src, pos, src.size() - pos);
                pos = src.size();
                if (matched) matched->clear();
                return true;
            }
            out.append(src, pos, open - pos);
            pos = open + 2;

            bool rawTriple = false;
            if (pos < src.size() && src[pos] == '{') {
                rawTriple = true;
                ++pos;
            }
            // Find the closing delimiter.
            std::string closeDelim = rawTriple ? "}}}" : "}}";
            size_t close = src.find(closeDelim, pos);
            if (close == std::string::npos) {
                error = "unterminated '{{' tag (missing '" + closeDelim + "')";
                return false;
            }
            std::string tag = src.substr(pos, close - pos);
            pos = close + closeDelim.size();

            // Trim surrounding whitespace from the tag content.
            auto trim = [](std::string s) {
                size_t a = s.find_first_not_of(" \t\r\n");
                if (a == std::string::npos) return std::string{};
                size_t b = s.find_last_not_of(" \t\r\n");
                return s.substr(a, b - a + 1);
            };
            tag = trim(tag);

            if (rawTriple) {
                const json* v = resolvePath(ctx, tag);
                if (v) out += scalarToString(*v);
                continue;
            }
            if (tag.empty()) continue;
            if (tag[0] == '!') {
                continue;  // comment
            }
            if (tag[0] == '#') {
                // Block open: #if <key> or #each <key>.
                std::string rest = trim(tag.substr(1));
                std::string kind, key;
                size_t sp = rest.find_first_of(" \t");
                if (sp == std::string::npos) {
                    error = "block tag '{{#" + rest +
                            "}}' is missing its key";
                    return false;
                }
                kind = rest.substr(0, sp);
                key = trim(rest.substr(sp));
                if (kind == "if") {
                    if (!renderIf(ctx, key, out)) return false;
                } else if (kind == "each") {
                    if (!renderEach(ctx, key, out)) return false;
                } else {
                    error = "unknown block '{{#" + kind +
                            "}}' (only #if and #each are supported)";
                    return false;
                }
                continue;
            }
            if (tag[0] == '/') {
                std::string key = trim(tag.substr(1));
                if (matched) *matched = key;
                return true;  // hand control back to the enclosing block
            }
            // Plain interpolation, HTML-escaped.
            const json* v = resolvePath(ctx, tag);
            if (v) out += htmlEscape(scalarToString(*v));
        }
        if (matched) matched->clear();
        if (!atTop) {
            error = "unexpected end of template inside a block "
                    "(missing a '{{/...}}')";
            return false;
        }
        return true;
    }

    bool renderIf(const json& ctx, const std::string& key, std::string& out) {
        const json* v = resolvePath(ctx, key);
        std::string body;
        std::string matched;
        // Render the block body into a scratch buffer either way so we
        // consume the matching {{/if}} regardless of truthiness. The close
        // tag may be written as {{/key}} or {{/if}} (both accepted).
        if (!render(ctx, body, false, &matched)) return false;
        if (matched != key && matched != "if") {
            error = "mismatched block close: expected '{{/" + key +
                    "}}' or '{{/if}}', got '{{/" + matched + "}}'";
            return false;
        }
        if (truthy(v)) out += body;
        return true;
    }

    bool renderEach(const json& ctx, const std::string& key, std::string& out) {
        const json* v = resolvePath(ctx, key);
        // The block body is everything from here up to the matching
        // `{{/key}}` / `{{/each}}`. We scan with a depth counter so nested
        // `{{#if}}` / `{{#each}}` inside the body do not confuse the match.
        size_t bodyStart = pos;
        size_t scan = pos;
        int depth = 1;
        size_t closeTagStart = std::string::npos;
        while (scan < src.size()) {
            size_t open = src.find("{{", scan);
            if (open == std::string::npos) break;
            size_t close = src.find("}}", open + 2);
            if (close == std::string::npos) {
                error = "unterminated '{{' tag inside {{#each " + key + "}}";
                return false;
            }
            std::string inner = src.substr(open + 2, close - (open + 2));
            size_t a = inner.find_first_not_of(" \t\r\n");
            std::string t = (a == std::string::npos) ? "" : inner.substr(a);
            if (!t.empty() && t[0] == '#') {
                ++depth;
            } else if (!t.empty() && t[0] == '/') {
                --depth;
                if (depth == 0) {
                    closeTagStart = open;
                    pos = close + 2;  // advance past {{/...}}
                    break;
                }
            }
            scan = close + 2;
        }
        if (closeTagStart == std::string::npos) {
            error = "{{#each " + key +
                    "}} is missing its matching '{{/each}}' / '{{/" + key +
                    "}}'";
            return false;
        }
        std::string elemTpl =
            src.substr(bodyStart, closeTagStart - bodyStart);

        if (!v || !v->is_array()) return true;  // empty / non-array → nothing
        for (const auto& el : *v) {
            Parser sub(elemTpl);
            std::string piece;
            std::string m;
            if (!sub.render(el, piece, true, &m)) {
                error = sub.error;
                return false;
            }
            if (!m.empty()) {
                error = "stray block close inside {{#each " + key + "}} body";
                return false;
            }
            out += piece;
        }
        return true;
    }
};

} // namespace

TemplateRenderResult renderTemplate(const std::string& tpl, const json& data) {
    TemplateRenderResult r;
    Parser p(tpl);
    std::string out;
    std::string matched;
    if (!p.render(data, out, true, &matched)) {
        r.ok = false;
        r.error = p.error.empty() ? "template parse error" : p.error;
        return r;
    }
    if (!matched.empty()) {
        r.ok = false;
        r.error = "stray block close '{{/" + matched +
                  "}}' with no matching '{{#" + matched + "}}'";
        return r;
    }
    r.ok = true;
    r.output = std::move(out);
    return r;
}

} // namespace topo::debug
