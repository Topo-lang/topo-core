#ifndef TOPO_DEBUG_SUMMARYRENDERER_H
#define TOPO_DEBUG_SUMMARYRENDERER_H

// Summary template renderer.
//
// A `summary_template` declared in `.topo` is a string with `{ expr }`
// placeholders. Each placeholder is a query expression (parsed by
// the same parseQuery() the query subcommand uses). The renderer is pure
// string-level: it carves the template into literal+placeholder segments,
// then walks them substituting each placeholder's resolved value.
//
// Escapes: `{{` → literal `{`, `}}` → literal `}`. A bare `{` or `}` that
// doesn't open a balanced placeholder is a parse error — keeps the syntax
// strict so misformatted templates fail loudly rather than silently leak
// brace-laden text into rendered output.
//
// Resolver injection is a callback rather than a fixed map so the consumer
// (CLI subcommand, /summary HTTP route, future LSP) can decide whether to
// dispatch one adapter spawn per placeholder or to batch multiple queries
// into a single multi-variable adapter call (future enhancement).

#include <functional>
#include <string>
#include <vector>

namespace topo::debug_summary {

struct Segment {
    bool isPlaceholder = false;
    // For literals: the raw text to emit. For placeholders: the trimmed
    // inner expression string (no surrounding braces, no leading/trailing
    // whitespace).
    std::string text;
    // Byte offset in the original template where this segment starts; used
    // for error messages.
    size_t pos = 0;
};

// Parse a template string into a sequence of literal + placeholder segments.
// Returns true on success; on failure, `err` is set with a position-bearing
// description and `out` is left empty.
bool parseTemplate(const std::string& tmpl,
                   std::vector<Segment>& out,
                   std::string& err);

// Convenience: collect every distinct placeholder expression in source order.
// `parsedSegments` is the output of parseTemplate. The order is the order of
// first occurrence in the template — useful so a consumer can dispatch one
// adapter call per distinct expression and reuse the result.
std::vector<std::string> distinctPlaceholders(const std::vector<Segment>& parsedSegments);

// Render the template using `resolver(expr)` to obtain the text for each
// placeholder. The resolver returns `false` on failure; `errOut` then carries
// the resolver's error string and the renderer aborts.
using PlaceholderResolver =
    std::function<bool(const std::string& expr, std::string& valueOut, std::string& errOut)>;

bool render(const std::vector<Segment>& segments,
            const PlaceholderResolver& resolver,
            std::string& out,
            std::string& err);

} // namespace topo::debug_summary

#endif // TOPO_DEBUG_SUMMARYRENDERER_H
