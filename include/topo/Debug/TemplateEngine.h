#ifndef TOPO_DEBUG_TEMPLATEENGINE_H
#define TOPO_DEBUG_TEMPLATEENGINE_H

// Minimal mustache-subset template engine.
//
// Deliberately tiny: the design rationale is that an
// LLM-authored `templates/*.html.tpl` must stay trivially readable and
// editable, so we intentionally do NOT pull in Handlebars/Nunjucks. We
// support exactly the subset the AI-collaboration flow needs:
//
//   {{var}}            HTML-escaped scalar substitution
//   {{{var}}}          raw (un-escaped) substitution — opt-out of escaping
//   {{#if key}}...{{/if}}     conditional block (truthy = present & non-empty
//                             & not false/0/[] )
//   {{#each list}}...{{/each}}  iterate an array; inside the block `{{.}}`
//                               is the current element and `{{key}}` resolves
//                               against the element when it is an object
//   {{! comment }}     dropped from output
//
// Dotted paths (`{{view.rank_current.sum}}`) walk nested JSON objects.
// Unknown keys render as empty string (mustache semantics) rather than
// throwing — a half-written template still produces visible HTML for the
// author to iterate on.
//
// The data model is an nlohmann::json value (object at the top level).
// The engine has zero LLVM dependency and lives under topo-core/lib/Debug.

#include <nlohmann/json.hpp>

#include <string>

namespace topo::debug {

struct TemplateRenderResult {
    bool ok = false;
    std::string output;  // rendered text on success
    std::string error;   // human-readable diagnostic on failure
};

// Render `tpl` against `data`. `data` should be a JSON object; scalars and
// arrays are accepted but top-level non-object data only resolves `{{.}}`.
//
// Errors are reserved for structural problems the author must fix:
// unbalanced `{{#if}}` / `{{#each}}` / `{{/...}}` blocks or a malformed
// `{{` with no closing `}}`. Missing data keys are NOT errors.
TemplateRenderResult renderTemplate(const std::string& tpl,
                                    const nlohmann::json& data);

// HTML-escape helper (exposed for unit tests and reuse by callers that
// build fragments outside the engine).
std::string htmlEscape(const std::string& s);

} // namespace topo::debug

#endif // TOPO_DEBUG_TEMPLATEENGINE_H
