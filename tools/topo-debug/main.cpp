// topo-debug — declaration-driven debugger front-end.
//
// Subcommands:
//   query <expr> --break <site> ...    one-shot evaluation against an
//                                      Extract adapter (mock or real lldb).
//                                      With --replay <file.snap>, evaluates
//                                      against a previously captured snapshot
//                                      instead of spawning an adapter.
//   snapshot <topo_name> --break ...   capture all distinct container vars
//                                      referenced by the symbol's views in a
//                                      single adapter spawn, serialise them
//                                      to a TOPO_SNAP_V1 file.
//   serve [--port N] [--host H] ...    HTTP server that serves the
//                                      embedded debug SPA + a /query
//                                      endpoint backed by the same adapter
//
// The CLI is adapter-agnostic — it speaks the wire protocol
// over the adapter's stdio. A fixture-driven mock adapter
// (`topo-debug-mock-adapter`) is available alongside the real lldb adapter
// `topo-debug-cpp`/`topo-debug-rust` (single source). Declaration-driven
// view resolution: identifier references in the query
// that match a declared view get expanded to the underlying container +
// literal slice using the *.topo-dbg.json produced by `topo build`.
//
// `serve` is a thin HTTP front-end (default 127.0.0.1)
// that serves a static SPA reading the debug-meta + a JSON /query endpoint
// that calls the same dispatchOneQuery() helper as the CLI subcommand. The
// SPA is a vanilla-JS bundle compiled into the binary; no build tooling, no
// node_modules. Designed as a starting point that an LLM can fork into a
// per-project visualisation without touching C++.
//
// Deferred to follow-up phases: repl / watch / snapshot / attach, real
// adapters for Python/JVM/TS, user function calls, render-decl semantics.
//
// Exit codes:
//   0  success
//   1  parse error / IO / file not found / CLI usage error
//   2  adapter executable not found
//   3  query evaluation error (unknown var, type mismatch, ...)

#include "topo/Debug/Ipc/FrameReader.h"
#include "topo/Debug/PassReportsLoader.h"
#include "topo/Debug/Query/Ast.h"
#include "topo/Debug/Query/Evaluator.h"
#include "topo/Debug/Query/FrameView.h"
#include "topo/Debug/Query/Parser.h"
#include "topo/Debug/Query/Value.h"
#include "topo/Debug/Server/Assets.h"
#include "topo/Debug/Server/HttpServer.h"
#include "topo/Debug/Server/RenderFnLoader.h"
#include "topo/Debug/Server/WebSocket.h"
#include "topo/Debug/SummaryRenderer.h"
#include "topo/Debug/TemplateEngine.h"
#include "topo/Platform/Process.h"

#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

using namespace topo::debug_ipc;
using namespace topo::debug_query;
using nlohmann::json;

namespace {

void printUsage(std::FILE* out) {
    std::fprintf(out,
                 "Usage:\n"
                 "  topo-debug query <expr> --break <site>\n"
                 "                  (--mock-fixture <name> | --target <binary>\n"
                 "                   | --replay <file.snap>)\n"
                 "                  [--adapter <path>] [--debug-meta <path>]\n"
                 "                  [--format text|jsonl]\n"
                 "\n"
                 "  topo-debug query <expr>                (static / sidecar-only)\n"
                 "                  (--debug-meta <path> | --target <binary>)\n"
                 "                  [--format text|jsonl]\n"
                 "                  Use for queries that read only Pass sidecars\n"
                 "                  (pass_decision/pass_fired/pass_reason/pass_count).\n"
                 "                  No adapter spawn, no --break required; sidecar\n"
                 "                  protocol is host-agnostic across LLVM/JVM/V8.\n"
                 "\n"
                 "  topo-debug snapshot <topo_name> --break <site>\n"
                 "                  --debug-meta <path> --target <binary>\n"
                 "                  --adapter <path> --out <file>\n"
                 "\n"
                 "  topo-debug summary <topo_name> --break <site> --debug-meta <path>\n"
                 "                  (--mock-fixture <name> | --target <binary>)\n"
                 "                  [--adapter <path>]\n"
                 "\n"
                 "  topo-debug serve [--host 127.0.0.1] [--port 7300]\n"
                 "                  [--debug-meta <path>]\n"
                 "                  [--mock-fixture <name> | --target <binary> --adapter <path>\n"
                 "                   | --attach <pid> --adapter <path>]\n"
                 "                  [--render-fn-path [<kind>:]<dir>]...  (kind: cpp|java|python|node)\n"
                 "                  [--vendor-path <dir>] [--template-path <dir>]\n"
                 "                  [--ai-export <port>] [--once <expr> --break <site>] [--open]\n"
                 "\n"
                 "Adapter selection (exactly one required for query/summary;\n"
                 "optional for serve — when omitted, /query and /summary return 503):\n"
                 "  --mock-fixture <name>  drive the bundled mock adapter against a\n"
                 "                         named in-memory fixture\n"
                 "  --target <binary>      drive a real debugger adapter against an\n"
                 "                         actual host binary (lldb)\n"
                 "  --replay <file.snap>   (query-only) evaluate against a previously\n"
                 "                         captured TOPO_SNAP_V1 snapshot — no adapter\n"
                 "                         spawn, no live process needed\n"
                 "\n"
                 "Declaration-driven view resolution:\n"
                 "  --debug-meta <path>    explicit *.topo-dbg.json produced by\n"
                 "                         `topo build`. For `query`, when omitted with\n"
                 "                         --target, auto-discovers <target>.topo-dbg.json\n"
                 "                         next to the binary. For `serve`, the path is\n"
                 "                         required: the SPA fetches it via GET /dbg.json.\n"
                 "\n"
                 "Built-in reductions: sum, mean, min, max, count, shape, dtype, sample\n");
}

struct CliConfig {
    std::string subcommand;

    // ----- `query` subcommand fields -----
    std::string exprText;
    std::string site;
    std::string mockFixture;
    std::string target;
    std::string adapterPath;
    std::string format = "text";
    std::string debugMetaPath;
    bool debugMetaExplicit = false;

    // ----- `summary` subcommand fields -----
    // Symbol identifier (topo_name from .topo `debug <Name> { ... }` block).
    // The summary_template attached to that symbol is rendered against the
    // breakpoint's frame. Adapter selection / --break / --debug-meta reuse
    // the same flag handlers as `query`.
    std::string summaryTopoName;

    // ----- `snapshot` subcommand fields -----
    // Same `--break` / `--debug-meta` / `--target` / `--adapter` flag handlers
    // as `query`/`summary`. Symbol identifier (topo_name) selects which
    // debug-meta entry's view containers we capture. `--out` is the output
    // path for the TOPO_SNAP_V1 file.
    std::string snapshotTopoName;
    std::string snapshotOutPath;

    // ----- `query --replay` field -----
    // When set, query evaluates against a pre-captured snapshot instead of
    // spawning an adapter. Mutually exclusive with --mock-fixture/--target.
    std::string replayPath;

    // ----- `serve` subcommand fields -----
    std::string serveHost = "127.0.0.1";
    uint16_t servePort = 7300;
    // /query backing config (empty → /query returns 503 with hint)
    std::string serveMockFixture;
    std::string serveTarget;
    std::string serveAdapter;
    // Directories that hold user-authored
    // libtopo_render_<method>.{so,dylib} shared libraries. Each --render-fn-path
    // <dir> argument appends one entry. When empty, POST /render returns 503.
    std::vector<std::string> serveRenderFnPaths;
    // Directory served at GET /vendor/<file>. Holds
    // the SPA's third-party JS bundles (Chart.js etc.). Defaults to the
    // configure-time vendor build dir baked in via TOPO_DEBUG_VENDOR_DIR;
    // explicit --vendor-path overrides.
    std::string serveVendorPath;
    // Raw `--render-fn-path` specs ([<kind>:]<dir>). Parsed
    // into (backend, dir) pairs at serve time so the loader can register
    // each under the right host backend.
    std::vector<std::string> serveRenderFnSpecs;
    // Directory served at GET /templates/<name>. Holds
    // user-editable `*.html.tpl` mustache-subset templates. Defaults to the
    // configure-time templates build dir; explicit --template-path wins.
    std::string serveTemplatePath;
    // Read-only AI-collaboration export port. 0 = disabled
    // (default). When set, a second loopback-only HTTP server exposes
    // GET /ai/current-frame, /ai/symbols, /ai/recent-queries.
    uint16_t serveAiPort = 0;
    bool serveAiEnabled = false;
    // Serve UX flags.
    std::string serveOnceExpr;   // eval once, push to browser, exit
    std::string serveOnceSite;   // site for --once (reuses --break)
    bool serveOpen = false;      // launch a browser at startup
    std::string serveAttachPid;  // attach to a running process (vs spawn)
};

// View registry built from *.topo-dbg.json.
struct ViewExpansion {
    std::string container;
    bool isSliced = false;
    int64_t start = 0;
    int64_t end = 0;
};
using ViewRegistry = std::map<std::string, ViewExpansion>;

// Render-decl registry built from *.topo-dbg.json.
//
// Each entry describes one `render method=<name> { input: a, b; ... }` block
// found inside any `debug <Symbol> { ... }`. The serve loop consults this
// registry both as a method-name allow-list (only methods declared in the
// .topo source are dlopen-able) and as the source of truth for the input
// variable list — the user does not repeat it in the HTTP request.
struct RenderDeclEntry {
    std::string symbolTopoName;   // owning `debug <Symbol>` block
    std::vector<std::string> inputs;  // variables to feed to the render-fn
    std::string format;           // optional `format: <fmt>;` value
    std::string templatePath;     // optional `template: <path>;` value
};
// Keyed by method name; one entry per declaration. (The .topo grammar
// allows one method per render-decl, so collisions across different symbols
// would shadow each other — we keep the LAST one to match how renderer
// dispatch works when the user types `method=chart_demo` in the SPA.)
using RenderDeclRegistry = std::map<std::string, RenderDeclEntry>;

// Walk the query AST and collect every identifier that is
// referenced as a *value* (not as a call callee). The real adapter needs the
// variable name to pass to lldb's `FindVariable`; we don't want users to repeat
// it in a separate flag. The single-distinct-variable restriction applies here —
// multi-variable queries are resolved by resolveQueryVariables below.
void collectVariableIdents(const topo::debug_query::Expr& e, std::set<std::string>& out) {
    using topo::debug_query::ExprKind;
    switch (e.kind) {
        case ExprKind::Ident:
            out.insert(e.name);
            return;
        case ExprKind::FieldAccess:
        case ExprKind::Slice:
            if (e.base) collectVariableIdents(*e.base, out);
            if (e.sliceStart) collectVariableIdents(*e.sliceStart, out);
            if (e.sliceEnd) collectVariableIdents(*e.sliceEnd, out);
            return;
        case ExprKind::Call:
            // Skip `base` — that's the callee identifier (always a builtin reduction).
            for (const auto& arg : e.args) {
                if (arg) collectVariableIdents(*arg, out);
            }
            return;
        case ExprKind::BinaryOp:
            if (e.base) collectVariableIdents(*e.base, out);
            if (e.binaryRhs) collectVariableIdents(*e.binaryRhs, out);
            return;
        case ExprKind::UnaryOp:
            if (e.base) collectVariableIdents(*e.base, out);
            return;
        case ExprKind::IntLit:
        case ExprKind::FloatLit:
        case ExprKind::StringLit:
            return;
    }
}

// Parse a *.topo-dbg.json file into a view-name registry.
bool loadViewRegistry(const std::string& path,
                      ViewRegistry& out,
                      std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "cannot open debug metadata file '" + path + "'";
        return false;
    }
    json doc;
    try {
        in >> doc;
    } catch (const std::exception& e) {
        err = std::string("debug metadata parse error in '") + path + "': " + e.what();
        return false;
    }
    if (!doc.is_object() || !doc.contains("symbols") || !doc["symbols"].is_array()) {
        err = "debug metadata '" + path + "' missing required 'symbols' array";
        return false;
    }
    for (const auto& sym : doc["symbols"]) {
        if (!sym.is_object() || !sym.contains("views") || !sym["views"].is_array()) continue;
        for (const auto& v : sym["views"]) {
            if (!v.is_object() || !v.contains("name") || !v.contains("expr")) continue;
            std::string name = v["name"].get<std::string>();
            const auto& expr = v["expr"];
            if (!expr.is_object() || !expr.contains("container")) continue;
            ViewExpansion exp;
            exp.container = expr["container"].get<std::string>();
            std::string kind = expr.value("kind", std::string{"field"});
            if (kind == "slice") {
                if (!expr.contains("start") || !expr.contains("end")) {
                    continue;
                }
                exp.isSliced = true;
                exp.start = expr["start"].get<int64_t>();
                exp.end = expr["end"].get<int64_t>();
            }
            out[name] = std::move(exp);
        }
    }
    return true;
}

// Parse a render_decl's raw_body string (the text
// that appears between the `{ ... }` braces in the .topo source) into a
// key→value map. Grammar matches the SPA's `parseRenderArgs` JS helper so
// declarations behave the same whether they are dispatched server-side
// (this function) or client-side (the SPA fallback for unknown methods):
//
//     body  ::= pair (';' pair)* ';'?
//     pair  ::= key ':' value
//
// Returns true on success. Whitespace around keys/values/separators is
// trimmed; empty pairs are skipped. On the first ill-formed pair the
// function returns false with `err` populated; the caller treats a parse
// failure as a hard error because the registry would otherwise be lossy.
bool parseRenderRawBody(const std::string& body,
                        std::map<std::string, std::string>& out,
                        std::string& err) {
    auto trim = [](std::string s) {
        size_t i = 0;
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        size_t j = s.size();
        while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1]))) --j;
        return s.substr(i, j - i);
    };
    // The .topo parser captures everything between (and including) the
    // outer `{` and `}` that delimit the render-decl body — see
    // topo-core/lib/Parser/Parser.cpp's depth-counted token capture. Strip
    // one pair of outer braces before splitting on `;`, otherwise the
    // trailing `}` becomes a junk fragment without a `:`.
    std::string trimmedBody = trim(body);
    if (trimmedBody.size() >= 2 && trimmedBody.front() == '{' &&
        trimmedBody.back() == '}') {
        trimmedBody = trim(trimmedBody.substr(1, trimmedBody.size() - 2));
    }
    size_t pos = 0;
    const std::string& bodyRef = trimmedBody;
    while (pos <= bodyRef.size()) {
        size_t semi = bodyRef.find(';', pos);
        std::string part = bodyRef.substr(pos,
            semi == std::string::npos ? std::string::npos : semi - pos);
        std::string trimmed = trim(part);
        if (!trimmed.empty()) {
            size_t colon = trimmed.find(':');
            if (colon == std::string::npos) {
                err = "render_decl body fragment missing ':' separator: \"" +
                      trimmed + "\"";
                return false;
            }
            std::string key = trim(trimmed.substr(0, colon));
            std::string value = trim(trimmed.substr(colon + 1));
            if (key.empty()) {
                err = "render_decl body has an empty key in fragment: \"" +
                      trimmed + "\"";
                return false;
            }
            out[key] = value;
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return true;
}

// Split a comma-separated `input: a, b, c` value into individual variable
// names, stripping whitespace. Empty values map to an empty vector.
std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t comma = s.find(',', pos);
        std::string part = s.substr(pos,
            comma == std::string::npos ? std::string::npos : comma - pos);
        size_t i = 0;
        while (i < part.size() && std::isspace(static_cast<unsigned char>(part[i]))) ++i;
        size_t j = part.size();
        while (j > i && std::isspace(static_cast<unsigned char>(part[j - 1]))) --j;
        std::string trimmed = part.substr(i, j - i);
        if (!trimmed.empty()) out.push_back(trimmed);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

// Build the render-decl registry from *.topo-dbg.json.
//
// On a missing file or a malformed declaration the function returns false
// with `err` set. On success `out` contains one entry per declared method;
// declarations without an `input:` key are accepted but render-fns invoked
// against them will not receive any variables (empty JSON object).
bool loadRenderDeclRegistry(const std::string& path,
                            RenderDeclRegistry& out,
                            std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "cannot open debug metadata file '" + path + "'";
        return false;
    }
    json doc;
    try { in >> doc; }
    catch (const std::exception& e) {
        err = std::string("debug metadata parse error in '") + path + "': " +
              e.what();
        return false;
    }
    if (!doc.is_object() || !doc.contains("symbols") || !doc["symbols"].is_array()) {
        err = "debug metadata '" + path + "' missing required 'symbols' array";
        return false;
    }
    for (const auto& sym : doc["symbols"]) {
        if (!sym.is_object()) continue;
        std::string topoName = sym.value("topo_name", std::string{});
        if (!sym.contains("render_decls") || !sym["render_decls"].is_array()) continue;
        for (const auto& r : sym["render_decls"]) {
            if (!r.is_object() || !r.contains("method")) continue;
            RenderDeclEntry e;
            e.symbolTopoName = topoName;
            std::string method = r["method"].get<std::string>();
            std::string rawBody = r.value("raw_body", std::string{});
            std::map<std::string, std::string> kv;
            std::string parseErr;
            if (!parseRenderRawBody(rawBody, kv, parseErr)) {
                err = "render_decl '" + method + "' in symbol '" + topoName +
                      "': " + parseErr;
                return false;
            }
            auto itInput = kv.find("input");
            if (itInput != kv.end()) e.inputs = splitCsv(itInput->second);
            auto itFmt = kv.find("format");
            if (itFmt != kv.end()) e.format = itFmt->second;
            auto itTpl = kv.find("template");
            if (itTpl != kv.end()) e.templatePath = itTpl->second;
            out[method] = std::move(e);
        }
    }
    return true;
}

// Rewrite the query AST in place: every Ident whose name is
// a view in `registry` becomes either Ident(container) (field view) or
// Slice(Ident(container), IntLit(start), IntLit(end)) (sliced view).
void expandViews(topo::debug_query::ExprPtr& e, const ViewRegistry& registry) {
    using topo::debug_query::ExprKind;
    if (!e) return;
    switch (e->kind) {
        case ExprKind::Ident: {
            auto it = registry.find(e->name);
            if (it == registry.end()) return;
            const auto& exp = it->second;
            if (!exp.isSliced) {
                e->name = exp.container;
                return;
            }
            size_t pos = e->pos;
            auto base = topo::debug_query::makeIdent(exp.container, pos);
            auto startLit = topo::debug_query::makeInt(exp.start, pos);
            auto endLit = topo::debug_query::makeInt(exp.end, pos);
            auto slice = std::make_unique<topo::debug_query::Expr>();
            slice->kind = ExprKind::Slice;
            slice->pos = pos;
            slice->base = std::move(base);
            slice->sliceStart = std::move(startLit);
            slice->sliceEnd = std::move(endLit);
            e = std::move(slice);
            return;
        }
        case ExprKind::FieldAccess:
        case ExprKind::Slice:
            expandViews(e->base, registry);
            expandViews(e->sliceStart, registry);
            expandViews(e->sliceEnd, registry);
            return;
        case ExprKind::Call:
            for (auto& arg : e->args) expandViews(arg, registry);
            return;
        case ExprKind::BinaryOp:
            expandViews(e->base, registry);
            expandViews(e->binaryRhs, registry);
            return;
        case ExprKind::UnaryOp:
            expandViews(e->base, registry);
            return;
        case ExprKind::IntLit:
        case ExprKind::FloatLit:
        case ExprKind::StringLit:
            return;
    }
}

// Collect distinct variable names from the query AST and
// return them in deterministic order (alphabetical, since `std::set` already
// orders that way). The real adapter now accepts a comma-separated --var
// list, so the single-distinct-variable restriction is lifted here; the only
// remaining requirement is at-least-one, so we have a frame to anchor on.
std::vector<std::string> resolveQueryVariables(const topo::debug_query::Expr& root,
                                               std::string& err) {
    std::set<std::string> names;
    collectVariableIdents(root, names);
    if (names.empty()) {
        err = "query has no variable reference — `--target` mode needs at "
              "least one (e.g. `sum(matrix)`, not `42`)";
        return {};
    }
    return {names.begin(), names.end()};
}

bool parseCli(int argc, char** argv, CliConfig& cfg, std::string& err) {
    if (argc < 2) { err = "missing subcommand"; return false; }
    cfg.subcommand = argv[1];
    if (cfg.subcommand == "--help" || cfg.subcommand == "-h") return false;
    if (cfg.subcommand != "query" && cfg.subcommand != "summary" &&
        cfg.subcommand != "serve" && cfg.subcommand != "snapshot") {
        err = "supported subcommands: 'query', 'snapshot', 'summary', 'serve' (got '" +
              cfg.subcommand + "')";
        return false;
    }

    int startArg = 2;
    if (cfg.subcommand == "query") {
        if (argc < 3) { err = "query: missing expression"; return false; }
        cfg.exprText = argv[2];
        startArg = 3;
    } else if (cfg.subcommand == "summary") {
        if (argc < 3) { err = "summary: missing <topo_name>"; return false; }
        cfg.summaryTopoName = argv[2];
        startArg = 3;
    } else if (cfg.subcommand == "snapshot") {
        if (argc < 3) { err = "snapshot: missing <topo_name>"; return false; }
        cfg.snapshotTopoName = argv[2];
        startArg = 3;
    }

    for (int i = startArg; i < argc; ++i) {
        std::string a = argv[i];
        auto eat = [&](const std::string& flag, std::string& dest) -> bool {
            if (a == flag && i + 1 < argc) { dest = argv[++i]; return true; }
            if (a.rfind(flag + "=", 0) == 0) { dest = a.substr(flag.size() + 1); return true; }
            return false;
        };
        // Shared flags
        if (eat("--debug-meta", cfg.debugMetaPath)) { cfg.debugMetaExplicit = true; continue; }
        if (eat("--adapter", cfg.adapterPath)) {
            // For `serve`, the adapter feeds /query; reuse the same slot.
            if (cfg.subcommand == "serve") cfg.serveAdapter = cfg.adapterPath;
            continue;
        }
        if (eat("--mock-fixture", cfg.mockFixture)) {
            if (cfg.subcommand == "serve") cfg.serveMockFixture = cfg.mockFixture;
            continue;
        }
        if (eat("--target", cfg.target)) {
            if (cfg.subcommand == "serve") cfg.serveTarget = cfg.target;
            continue;
        }
        if (a == "--help" || a == "-h") return false;

        // query/summary/snapshot shared flag: --break <site> (frame anchor).
        // serve also takes --break to anchor `--once`.
        if (cfg.subcommand == "query" || cfg.subcommand == "summary" ||
            cfg.subcommand == "snapshot" || cfg.subcommand == "serve") {
            if (eat("--break", cfg.site)) continue;
        }

        // Query-only flags
        if (cfg.subcommand == "query") {
            if (eat("--format", cfg.format)) continue;
            if (eat("--replay", cfg.replayPath)) continue;
        }

        // Snapshot-only flags
        if (cfg.subcommand == "snapshot") {
            if (eat("--out", cfg.snapshotOutPath)) continue;
        }

        // Serve-only flags
        if (cfg.subcommand == "serve") {
            if (eat("--host", cfg.serveHost)) continue;
            std::string portStr;
            if (eat("--port", portStr)) {
                try {
                    int p = std::stoi(portStr);
                    // port 0 asks the OS to pick a free ephemeral
                    // port; the real port appears in the "listening on" line
                    // (read by E2E harnesses that need to avoid collisions).
                    if (p < 0 || p > 65535) throw std::out_of_range("range");
                    cfg.servePort = static_cast<uint16_t>(p);
                } catch (...) {
                    err = "--port must be an integer in 0..65535 (0 = OS-assigned)";
                    return false;
                }
                continue;
            }
            // --render-fn-path may be passed multiple
            // times; each entry is `[<kind>:]<dir>` (kind ∈ cpp/java/
            // python/node, bare = cpp). Appended in declaration order;
            // first match wins at lookup time.
            std::string rfPath;
            if (eat("--render-fn-path", rfPath)) {
                cfg.serveRenderFnSpecs.push_back(rfPath);
                // Keep the legacy cpp-only list populated for the existing
                // "render-fn search path:" banner / hasAnyPath callers.
                cfg.serveRenderFnPaths.push_back(rfPath);
                continue;
            }
            // single override for vendor static dir.
            if (eat("--vendor-path", cfg.serveVendorPath)) continue;
            // override for the templates directory.
            if (eat("--template-path", cfg.serveTemplatePath)) continue;
            // read-only AI export port (loopback only).
            std::string aiPortStr;
            if (eat("--ai-export", aiPortStr)) {
                try {
                    int p = std::stoi(aiPortStr);
                    if (p < 1 || p > 65535) throw std::out_of_range("range");
                    cfg.serveAiPort = static_cast<uint16_t>(p);
                    cfg.serveAiEnabled = true;
                } catch (...) {
                    err = "--ai-export must be an integer port in 1..65535";
                    return false;
                }
                continue;
            }
            // eval one expr, push to the browser, exit.
            if (eat("--once", cfg.serveOnceExpr)) continue;
            // attach to a running pid instead of spawning.
            if (eat("--attach", cfg.serveAttachPid)) continue;
            // launch a browser at startup.
            if (a == "--open") { cfg.serveOpen = true; continue; }
        }

        err = "unknown argument for `" + cfg.subcommand + "`: " + a;
        return false;
    }

    if (cfg.subcommand == "query") {
        if (cfg.format != "text" && cfg.format != "jsonl") {
            err = "--format must be 'text' or 'jsonl'";
            return false;
        }
        // Replay short-circuits adapter/target/break requirements. The
        // snapshot file already encodes the frame's vars + site + frame id;
        // mixing replay with adapter flags would be ambiguous.
        if (!cfg.replayPath.empty()) {
            if (!cfg.mockFixture.empty() || !cfg.target.empty() ||
                !cfg.adapterPath.empty() || !cfg.site.empty()) {
                err = "--replay is mutually exclusive with --mock-fixture, "
                      "--target, --adapter, --break (the snapshot already "
                      "encodes the frame)";
                return false;
            }
        } else if (cfg.site.empty()) {
            // static (sidecar-only) query mode.
            // Omitting --break opts into a path that reads only Pass sidecars
            // (pass_decision/pass_fired/pass_reason/pass_count) and never
            // spawns an adapter. The query is evaluated against an empty
            // Environment plus a PassReportsRegistry loaded from the sidecar
            // directory derived from --debug-meta or --target. The Evaluator
            // surfaces a clear per-call error if the query happens to touch
            // host variables (e.g. `sum(matrix)`), so the routing decision
            // is one-way: site absent → static, site present → adapter.
            if (!cfg.mockFixture.empty() || !cfg.adapterPath.empty()) {
                err = "static query mode (no --break) cannot accept "
                      "--mock-fixture or --adapter";
                return false;
            }
            if (cfg.debugMetaPath.empty() && cfg.target.empty()) {
                err = "static query mode (no --break) requires --debug-meta "
                      "<path> or --target <path> so we can locate the "
                      "sibling <output>.topo-passes/ sidecar directory";
                return false;
            }
        } else {
            if (cfg.mockFixture.empty() && cfg.target.empty()) {
                err = "either --mock-fixture, --target, or --replay is required";
                return false;
            }
            if (!cfg.mockFixture.empty() && !cfg.target.empty()) {
                err = "--mock-fixture and --target are mutually exclusive";
                return false;
            }
            if (!cfg.target.empty() && cfg.adapterPath.empty()) {
                err = "--adapter <path> is required with --target "
                      "(e.g. --adapter $(which topo-debug-cpp))";
                return false;
            }
        }
    } else if (cfg.subcommand == "snapshot") {
        if (cfg.site.empty()) {
            err = "--break is required for snapshot";
            return false;
        }
        if (cfg.debugMetaPath.empty()) {
            err = "--debug-meta <path> is required for snapshot "
                  "(view containers come from *.topo-dbg.json)";
            return false;
        }
        if (cfg.target.empty()) {
            err = "--target <binary> is required for snapshot";
            return false;
        }
        if (cfg.adapterPath.empty()) {
            err = "--adapter <path> is required for snapshot";
            return false;
        }
        if (cfg.snapshotOutPath.empty()) {
            err = "--out <file> is required for snapshot";
            return false;
        }
    } else if (cfg.subcommand == "summary") {
        if (cfg.site.empty()) {
            err = "--break is required for summary (the template renders against a frame)";
            return false;
        }
        if (cfg.debugMetaPath.empty()) {
            err = "--debug-meta <path> is required for summary "
                  "(template + view registry both live in *.topo-dbg.json)";
            return false;
        }
        if (cfg.mockFixture.empty() && cfg.target.empty()) {
            err = "either --mock-fixture or --target is required";
            return false;
        }
        if (!cfg.mockFixture.empty() && !cfg.target.empty()) {
            err = "--mock-fixture and --target are mutually exclusive";
            return false;
        }
        if (!cfg.target.empty() && cfg.adapterPath.empty()) {
            err = "--adapter <path> is required with --target";
            return false;
        }
    } else { // serve
        if (!cfg.serveMockFixture.empty() && !cfg.serveTarget.empty()) {
            err = "--mock-fixture and --target are mutually exclusive";
            return false;
        }
        if (!cfg.serveTarget.empty() && cfg.serveAdapter.empty()) {
            err = "--adapter <path> is required with --target";
            return false;
        }
        // --once needs a frame anchor and a query backing.
        if (!cfg.serveOnceExpr.empty()) {
            if (cfg.site.empty()) {
                err = "--once <expr> requires --break <site> to anchor the "
                      "one-shot evaluation";
                return false;
            }
            if (cfg.serveMockFixture.empty() && cfg.serveTarget.empty()) {
                err = "--once <expr> requires a query backing "
                      "(--mock-fixture <name> or --target <bin> --adapter <p>)";
                return false;
            }
            cfg.serveOnceSite = cfg.site;
        }
        // --attach <pid> selects a running process as the
        // debug target instead of spawning one. It is an alternative to
        // --target (the adapter attaches by pid); the two are mutually
        // exclusive.
        if (!cfg.serveAttachPid.empty()) {
            if (!cfg.serveTarget.empty()) {
                err = "--attach <pid> and --target <bin> are mutually "
                      "exclusive (attach uses a running process; target "
                      "spawns one)";
                return false;
            }
            // Validate the pid is a positive integer.
            bool digits = !cfg.serveAttachPid.empty();
            for (char c : cfg.serveAttachPid)
                if (c < '0' || c > '9') digits = false;
            if (!digits) {
                err = "--attach expects a numeric pid";
                return false;
            }
            if (cfg.serveAdapter.empty()) {
                err = "--attach <pid> requires --adapter <path> (the adapter "
                      "performs the attach)";
                return false;
            }
        }
    }
    return true;
}

std::string resolveAdapter(const std::string& override_, const std::string& argv0) {
    if (!override_.empty()) return override_;
    std::filesystem::path self(argv0);
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(self, ec);
    auto base = canon.parent_path();
    auto candidate = base / "topo-debug-mock-adapter";
    if (std::filesystem::exists(candidate)) return candidate.string();
    return "topo-debug-mock-adapter";
}

// One captured variable's bytes + layout, from a single frame. Multi-var
// adapters emit N of these in pair order (VarBytes immediately followed by
// the matching LayoutDescriptor); see the wire-protocol spec.
struct AdapterVarFrame {
    std::string name;
    std::string dtype;
    std::vector<int64_t> shape;
    std::vector<uint8_t> bytes;
    bool isShmRef = false;

    // v2 aggregate-leaf block. Populated when
    // the wire LayoutDescriptor JSON carries a top-level "struct" object;
    // empty otherwise.
    bool hasStruct = false;
    topo::debug_query::StructLayout structLayout;

    // stdlib-bridge default summary. When the adapter
    // sends a `var_summary` JSON frame (uuid / decimal128 / ndarray /
    // time_ns host value) instead of var_bytes+layout, `isSummary` is
    // set and `summaryText` holds the human-readable rendering. There
    // are no numeric `bytes` for such a var; a bare-identifier query
    // short-circuits to this text, a numeric op (sum(x)) still errors.
    bool isSummary = false;
    std::string summaryText;
};

struct AdapterFrame {
    int64_t frameId = 0;
    std::string site;
    std::vector<AdapterVarFrame> vars;
};

// Drain `expectedVars` (VarBytes, LayoutDescriptor) pairs from the adapter,
// preceded by a single `breakpoint_hit` JSON line. The adapter is expected
// to emit pairs in order so the pending VarBytes from the previous frame
// pairs up with the next LayoutDescriptor.
bool drainAdapter(ByteSource& src, size_t expectedVars, AdapterFrame& out,
                  std::string& err) {
    bool sawHit = false;
    // Hold the most recent VarBytes (and its ShmRef flag) until its matching
    // LayoutDescriptor arrives. The adapter's contract guarantees ordering.
    bool pendingBytes = false;
    std::vector<uint8_t> currentBytes;
    bool currentShmRef = false;

    while (out.vars.size() < expectedVars || !sawHit) {
        ReadResult r = readRecord(src, /*maxPayload=*/256ull * 1024 * 1024);
        if (r.status == ReadStatus::Eof) {
            err = "adapter EOF before all frames received "
                  "(have " + std::to_string(out.vars.size()) + "/" +
                  std::to_string(expectedVars) + " vars, breakpoint_hit=" +
                  (sawHit ? "yes" : "no") + ")";
            return false;
        }
        if (r.status != ReadStatus::Ok) {
            err = "adapter stream error: " + r.error;
            return false;
        }
        if (r.json) {
            const auto& j = *r.json;
            const std::string jkind = j.value("kind", std::string{});
            if (jkind == "breakpoint_hit") {
                sawHit = true;
                out.frameId = j.value("frame", 0);
                out.site = j.value("site", std::string{});
            } else if (jkind == "var_summary") {
                // a stdlib-bridge default summary stands
                // in for the var_bytes+layout pair of one variable. Count
                // it toward expectedVars so the drain loop terminates.
                AdapterVarFrame vf;
                vf.name = j.value("variable", std::string{});
                vf.isSummary = true;
                vf.summaryText = j.value("text", std::string{});
                vf.dtype = "summary";
                out.vars.push_back(std::move(vf));
            }
            continue;
        }
        if (r.frame) {
            const BinaryFrame& bf = *r.frame;
            if (bf.type == BinaryFrameType::VarBytes) {
                if (pendingBytes) {
                    err = "adapter sent VarBytes without an interleaved LayoutDescriptor";
                    return false;
                }
                pendingBytes = true;
                currentBytes = bf.payload;
                currentShmRef = bf.hasFlag(BinaryFrameFlag::ShmRef);
            } else if (bf.type == BinaryFrameType::LayoutDescriptor) {
                if (!pendingBytes) {
                    err = "adapter sent LayoutDescriptor without a preceding VarBytes";
                    return false;
                }
                try {
                    std::string body(bf.payload.begin(), bf.payload.end());
                    json lj = json::parse(body);
                    AdapterVarFrame vf;
                    vf.dtype = lj.value("dtype", std::string{});
                    vf.name = lj.value("variable", std::string{"matrix"});
                    if (lj.contains("shape") && lj["shape"].is_array()) {
                        for (const auto& d : lj["shape"]) {
                            vf.shape.push_back(d.get<int64_t>());
                        }
                    }
                    // optional struct block.
                    // Schema mirrors the adapter's emission in
                    // topo-lang-cpp/topo-debug/adapter.cpp tryBuildStructBlock():
                    //   {"name":..., "stride_bytes":..,
                    //    "fields":[{"name","offset","dtype"}, ...]}
                    if (lj.contains("struct") && lj["struct"].is_object()) {
                        const auto& sj = lj["struct"];
                        vf.structLayout.name = sj.value("name", std::string{});
                        vf.structLayout.strideBytes =
                            sj.value("stride_bytes", static_cast<uint64_t>(0));
                        if (sj.contains("fields") && sj["fields"].is_array()) {
                            for (const auto& fj : sj["fields"]) {
                                if (!fj.is_object()) continue;
                                topo::debug_query::StructField sf;
                                sf.name = fj.value("name", std::string{});
                                sf.offset = fj.value("offset", static_cast<uint64_t>(0));
                                sf.dtype = fj.value("dtype", std::string{});
                                vf.structLayout.fields.push_back(std::move(sf));
                            }
                        }
                        vf.hasStruct = true;
                    }
                    vf.bytes = std::move(currentBytes);
                    vf.isShmRef = currentShmRef;
                    out.vars.push_back(std::move(vf));
                    pendingBytes = false;
                    currentBytes.clear();
                    currentShmRef = false;
                } catch (const std::exception& e) {
                    err = std::string("layout_descriptor parse error: ") + e.what();
                    return false;
                }
            }
        }
    }
    if (pendingBytes) {
        err = "adapter ended with dangling VarBytes (no matching LayoutDescriptor)";
        return false;
    }
    return true;
}

// Configuration for one dispatch — what adapter to spawn, with what target.
struct QueryBacking {
    std::string adapterPath;    // resolved full path
    std::string mockFixture;    // "" → real target mode
    std::string target;         // host binary (real mode only)
    // attach to a running process by pid instead of
    // spawning `target`. Forwarded to the adapter as `--attach <pid>`;
    // mutually exclusive with `target` at the CLI layer.
    std::string attachPid;
    ViewRegistry views;         // loaded once at serve startup / per query call

    // optional sidecar registry from <output>.topo-passes/.
    // Loaded once per CLI invocation; passed by pointer into the Evaluator's
    // Environment. Empty/null when sidecar dir doesn't exist (build hadn't
    // run yet, or backend was non-LLVM and produced no sidecar).
    topo::debug_meta::PassReportsRegistry passReports;
    bool passReportsLoaded = false;
};

// Derive the sibling `<x>.topo-passes` directory from a `<x>.topo-dbg.json`
// path. Returns an empty path when `metaPath` does not end in the canonical
// suffix (defensive — the autodiscovery branch only invents paths that do).
std::filesystem::path passReportsDirForMeta(const std::string& metaPath) {
    const std::string suffix = ".topo-dbg.json";
    if (metaPath.size() < suffix.size()) return {};
    if (metaPath.compare(metaPath.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return {};
    }
    std::string stem = metaPath.substr(0, metaPath.size() - suffix.size());
    return std::filesystem::path(stem + ".topo-passes");
}

// Best-effort population of `backing.passReports` from the sibling sidecar
// directory. Never fatal — when the directory is missing or unreadable the
// registry stays empty and `pass_decision(...)` queries surface a clear
// per-call error instead of breaking the entire CLI invocation.
void loadPassReportsFromMeta(const std::string& metaPath, QueryBacking& backing) {
    auto dir = passReportsDirForMeta(metaPath);
    if (dir.empty()) return;
    std::string err;
    auto reg = topo::debug_meta::PassReportsRegistry::loadFromDirectory(dir, &err);
    if (reg) {
        backing.passReports = std::move(*reg);
        backing.passReportsLoaded = true;
    }
}

struct DispatchedQuery {
    bool ok = false;
    int cliExit = 0;            // 0/1/2/3 per existing semantics
    std::string error;
    int64_t frameId = 0;
    std::string typeName;
    json valueJson = nullptr;   // parsed JSON value (when ok)
    std::string valueText;      // human-readable result (when ok)
    std::string expandedExpr;   // post view-expansion expression dump (debug aid)
};

// Result of one adapter spawn: an Environment populated with whatever the
// adapter dumped, plus the frame id and site echoed back from the
// breakpoint_hit JSON line. Used by dispatchOneQuery (one expression) and
// dispatchOneSummary (many placeholders sharing the same env).
struct AdapterEnv {
    bool ok = false;
    int cliExit = 0;
    std::string error;
    int64_t frameId = 0;
    std::string site;
    Environment env;
    // name → human-readable default summary for vars the
    // adapter reported as stdlib-bridge-shaped (no numeric bytes; not in
    // `env`). A bare-identifier query short-circuits to this text.
    std::map<std::string, std::string> summaries;
};

// Spawn the adapter, drain `expectedVarCount` (VarBytes, LayoutDescriptor)
// pairs, and return a ready-to-evaluate Environment. `vars` is the
// comma-list of host symbol names — empty in mock mode (mock binds its
// fixture's variable internally; `expectedVarCount` then defaults to 1).
AdapterEnv buildEnvFromAdapter(const std::vector<std::string>& vars,
                               const std::string& site,
                               const QueryBacking& backing) {
    AdapterEnv r;

    if (!std::filesystem::exists(backing.adapterPath)) {
        r.error = "adapter not found at '" + backing.adapterPath + "'";
        r.cliExit = 2;
        return r;
    }

    topo::platform::PipedProcess adapter;
    std::vector<std::string> adapterArgs;
    size_t expectedVarCount = 0;
    if (!backing.target.empty()) {
        if (vars.empty()) {
            r.error = "real-target adapter requires at least one variable";
            r.cliExit = 1;
            return r;
        }
        std::string varsCsv;
        for (const auto& v : vars) {
            if (!varsCsv.empty()) varsCsv.push_back(',');
            varsCsv += v;
        }
        adapterArgs = {"--site", site, "--target", backing.target, "--var", varsCsv};
        expectedVarCount = vars.size();
    } else if (!backing.attachPid.empty()) {
        // attach mode: the adapter attaches to a running
        // pid rather than launching a fresh process. Same variable
        // protocol as --target.
        if (vars.empty()) {
            r.error = "attach-mode adapter requires at least one variable";
            r.cliExit = 1;
            return r;
        }
        std::string varsCsv;
        for (const auto& v : vars) {
            if (!varsCsv.empty()) varsCsv.push_back(',');
            varsCsv += v;
        }
        adapterArgs = {"--site", site, "--attach", backing.attachPid,
                       "--var", varsCsv};
        expectedVarCount = vars.size();
    } else {
        adapterArgs = {"--fixture", backing.mockFixture, "--site", site};
        expectedVarCount = 1;
    }
    if (!adapter.start(backing.adapterPath, adapterArgs)) {
        r.error = "failed to spawn adapter '" + backing.adapterPath + "'";
        r.cliExit = 2;
        return r;
    }

    ByteSource src;
    src.readByte = [&]() -> int { return adapter.readByte(); };
    src.readBulk = [&](uint8_t* buf, size_t n) -> size_t {
        return adapter.read(buf, n);
    };

    AdapterFrame af;
    std::string drainErr;
    if (!drainAdapter(src, expectedVarCount, af, drainErr)) {
        adapter.stop();
        r.error = drainErr;
        r.cliExit = 1;
        return r;
    }

    for (auto& vf : af.vars) {
        // stdlib-bridge summary var: no bytes, not an
        // evaluable FrameView. Stash the text for the bare-identifier
        // short-circuit in dispatchOneQuery.
        if (vf.isSummary) {
            r.summaries.emplace(vf.name, std::move(vf.summaryText));
            continue;
        }
        LayoutDescriptor layout;
        layout.dtype = vf.dtype;
        layout.shape = vf.shape;
        // carry struct block into the layout.
        if (vf.hasStruct) {
            layout.isStruct = true;
            layout.structLayout = std::move(vf.structLayout);
        }
        FrameView fv = FrameView::owned(vf.name, std::move(vf.bytes), std::move(layout));
        if (vf.isShmRef) fv.markShmRef();
        r.env.variables.emplace(vf.name, std::move(fv));
    }

    {
        json cont = {{"op", "continue"}};
        std::string line = cont.dump() + "\n";
        adapter.write(line.data(), line.size());
        adapter.closeStdin();
    }
    adapter.stop(2000);

    r.ok = true;
    r.frameId = af.frameId;
    r.site = af.site;
    return r;
}

// Run one query end-to-end: parse expr → expand views → spawn adapter →
// drain → evaluate → kill adapter. Used by both the `query` subcommand
// and the `serve` subcommand's POST /query handler.
DispatchedQuery dispatchOneQuery(const std::string& exprText,
                                 const std::string& site,
                                 const QueryBacking& backing) {
    DispatchedQuery r;

    std::string parseErr;
    auto expr = parseQuery(exprText, parseErr);
    if (!expr) {
        r.error = "query parse error: " + parseErr;
        r.cliExit = 1;
        return r;
    }
    expandViews(expr, backing.views);

    std::vector<std::string> vars;
    if (!backing.target.empty() || !backing.attachPid.empty()) {
        std::string varErr;
        vars = resolveQueryVariables(*expr, varErr);
        if (vars.empty()) {
            r.error = varErr;
            r.cliExit = 1;
            return r;
        }
    }
    AdapterEnv ae = buildEnvFromAdapter(vars, site, backing);
    if (!ae.ok) {
        r.error = ae.error;
        r.cliExit = ae.cliExit;
        return r;
    }

    // a bare-identifier query against a stdlib-bridge
    // host value (uuid / decimal128 / ndarray / time_ns) resolves to its
    // default human-readable summary. A numeric op (e.g. `sum(x)`) is not
    // a bare Ident, so it still flows to evaluate() and errors as before
    // (a ns timestamp / uuid is not summable) — only the default view is
    // the readable summary, matching the stdlib default-summary behavior.
    if (expr->kind == ExprKind::Ident) {
        auto sit = ae.summaries.find(expr->name);
        if (sit != ae.summaries.end()) {
            r.ok = true;
            r.frameId = ae.frameId;
            r.typeName = "summary";
            r.valueText = sit->second;
            r.valueJson = sit->second;  // JSON string scalar
            return r;
        }
    }

    if (backing.passReportsLoaded) {
        ae.env.passReports = &backing.passReports;
    }
    EvalResult evalRes = evaluate(*expr, ae.env);
    if (!evalRes.ok) {
        r.error = "query error: " + evalRes.error;
        r.cliExit = 3;
        return r;
    }
    r.ok = true;
    r.frameId = ae.frameId;
    r.typeName = formatTypeName(evalRes.value);
    r.valueText = formatText(evalRes.value);
    try {
        r.valueJson = json::parse(formatJsonValue(evalRes.value));
    } catch (...) {
        r.valueJson = formatJsonValue(evalRes.value);
    }
    return r;
}

// ---------- summary rendering (shared by CLI subcommand + /summary route) ----------

// Look up `symbols[].summary_template` for a given topo_name in the
// debug-meta. Returns false with `err` set when the file is missing, the
// symbol is absent, or the symbol has no summary_template.
bool loadSummaryTemplate(const std::string& metaPath,
                         const std::string& topoName,
                         std::string& templateOut,
                         std::string& err) {
    std::ifstream in(metaPath);
    if (!in) { err = "cannot open '" + metaPath + "'"; return false; }
    json doc;
    try { in >> doc; }
    catch (const std::exception& e) {
        err = std::string("debug-meta parse error: ") + e.what();
        return false;
    }
    if (!doc.is_object() || !doc.contains("symbols") || !doc["symbols"].is_array()) {
        err = "debug-meta missing 'symbols' array";
        return false;
    }
    for (const auto& sym : doc["symbols"]) {
        if (!sym.is_object()) continue;
        if (sym.value("topo_name", std::string{}) != topoName) continue;
        if (!sym.contains("summary_template")) {
            err = "symbol '" + topoName + "' has no `summary` declaration";
            return false;
        }
        templateOut = sym["summary_template"].get<std::string>();
        return true;
    }
    err = "no symbol named '" + topoName + "' in debug-meta";
    return false;
}

// Snapshot helper: find the symbol entry by topo_name and
// return the distinct *container* names from its views[] (each view's
// `expr.container`). Order is deterministic: first-appearance order
// (preserving the view declaration order in main.topo). Returns false with
// `err` set when the file is missing, the symbol is absent, or the symbol
// has no views.
bool collectSymbolContainers(const std::string& metaPath,
                             const std::string& topoName,
                             std::vector<std::string>& containersOut,
                             std::string& err) {
    std::ifstream in(metaPath);
    if (!in) { err = "cannot open '" + metaPath + "'"; return false; }
    json doc;
    try { in >> doc; }
    catch (const std::exception& e) {
        err = std::string("debug-meta parse error: ") + e.what();
        return false;
    }
    if (!doc.is_object() || !doc.contains("symbols") || !doc["symbols"].is_array()) {
        err = "debug-meta missing 'symbols' array";
        return false;
    }
    for (const auto& sym : doc["symbols"]) {
        if (!sym.is_object()) continue;
        if (sym.value("topo_name", std::string{}) != topoName) continue;
        if (!sym.contains("views") || !sym["views"].is_array()) {
            err = "symbol '" + topoName + "' has no `view` declarations";
            return false;
        }
        std::set<std::string> seen;
        for (const auto& v : sym["views"]) {
            if (!v.is_object() || !v.contains("expr")) continue;
            const auto& expr = v["expr"];
            if (!expr.is_object() || !expr.contains("container")) continue;
            std::string c = expr["container"].get<std::string>();
            if (seen.insert(c).second) containersOut.push_back(c);
        }
        if (containersOut.empty()) {
            err = "symbol '" + topoName + "' has no view containers to capture";
            return false;
        }
        return true;
    }
    err = "no symbol named '" + topoName + "' in debug-meta";
    return false;
}

struct DispatchedSummary {
    bool ok = false;
    int cliExit = 0;
    std::string error;
    std::string topoName;
    std::string templateText;
    std::string rendered;
    // Per-placeholder echo: each entry { expr, ok, value?, type?, error? }.
    // Useful for HTTP clients that want to surface partial progress.
    json placeholders = json::array();
};

DispatchedSummary dispatchOneSummary(const std::string& topoName,
                                     const std::string& site,
                                     const std::string& metaPath,
                                     const QueryBacking& backing) {
    DispatchedSummary r;
    r.topoName = topoName;

    std::string tmplErr;
    if (!loadSummaryTemplate(metaPath, topoName, r.templateText, tmplErr)) {
        r.error = tmplErr;
        r.cliExit = 1;
        return r;
    }

    std::vector<topo::debug_summary::Segment> segments;
    std::string parseErr;
    if (!topo::debug_summary::parseTemplate(r.templateText, segments, parseErr)) {
        r.error = "summary template parse error: " + parseErr;
        r.cliExit = 1;
        return r;
    }

    // single multi-var adapter spawn for all placeholders.
    // Pre-parse each distinct placeholder expression, expand views, and
    // accumulate the union of variable identifiers across them. One adapter
    // call then drains all needed vars at once; each placeholder evaluates
    // against the shared Environment instead of spawning its own subprocess.
    auto distinct = topo::debug_summary::distinctPlaceholders(segments);
    struct ParsedPlaceholder {
        std::string text;
        topo::debug_query::ExprPtr ast;
        std::string parseErr;
    };
    std::vector<ParsedPlaceholder> parsed;
    parsed.reserve(distinct.size());
    std::set<std::string> unionVars;
    for (const auto& exprText : distinct) {
        ParsedPlaceholder p;
        p.text = exprText;
        p.ast = parseQuery(exprText, p.parseErr);
        if (p.ast) {
            expandViews(p.ast, backing.views);
            collectVariableIdents(*p.ast, unionVars);
        }
        parsed.push_back(std::move(p));
    }

    AdapterEnv ae;
    if (!backing.target.empty() || !backing.attachPid.empty()) {
        if (unionVars.empty()) {
            r.error = "summary template has no variable reference — `--target`"
                      " / `--attach` mode needs at least one (each `{ expr }`"
                      " must reference a host variable)";
            r.cliExit = 1;
            return r;
        }
        std::vector<std::string> varsVec(unionVars.begin(), unionVars.end());
        ae = buildEnvFromAdapter(varsVec, site, backing);
    } else {
        ae = buildEnvFromAdapter({}, site, backing);
    }
    if (!ae.ok) {
        r.error = ae.error;
        r.cliExit = ae.cliExit;
        return r;
    }
    if (backing.passReportsLoaded) {
        ae.env.passReports = &backing.passReports;
    }

    // Evaluate each placeholder against the shared Environment. Failures
    // accumulate into `placeholders[]` so the SPA can surface partial
    // progress; the first error decides the top-level cli_exit.
    std::map<std::string, std::string> resolved;
    int worstExit = 0;
    for (auto& p : parsed) {
        json entry = json::object();
        entry["expr"] = p.text;
        if (!p.ast) {
            entry["ok"] = false;
            entry["error"] = "parse error: " + p.parseErr;
            entry["cli_exit"] = 1;
            if (worstExit < 1) worstExit = 1;
            r.placeholders.push_back(std::move(entry));
            continue;
        }
        EvalResult evalRes = evaluate(*p.ast, ae.env);
        if (evalRes.ok) {
            std::string text = formatText(evalRes.value);
            entry["ok"] = true;
            entry["type"] = formatTypeName(evalRes.value);
            try {
                entry["value"] = json::parse(formatJsonValue(evalRes.value));
            } catch (...) {
                entry["value"] = formatJsonValue(evalRes.value);
            }
            entry["text"] = text;
            resolved.emplace(p.text, std::move(text));
        } else {
            entry["ok"] = false;
            entry["error"] = "query error: " + evalRes.error;
            entry["cli_exit"] = 3;
            if (worstExit < 3) worstExit = 3;
        }
        r.placeholders.push_back(std::move(entry));
    }
    if (worstExit != 0) {
        for (const auto& e : r.placeholders) {
            if (e.value("ok", true)) continue;
            r.error = "placeholder '{" + e["expr"].get<std::string>() +
                      "}': " + e["error"].get<std::string>();
            break;
        }
        r.cliExit = worstExit;
        return r;
    }

    std::string renderErr;
    bool rendered = topo::debug_summary::render(
        segments,
        [&](const std::string& expr, std::string& out, std::string& errCb) {
            auto it = resolved.find(expr);
            if (it == resolved.end()) { errCb = "unresolved (internal)"; return false; }
            out = it->second;
            return true;
        },
        r.rendered, renderErr);
    if (!rendered) {
        r.error = "summary render error: " + renderErr;
        r.cliExit = 1;
        return r;
    }
    r.ok = true;
    return r;
}

int runSummarySubcommand(const CliConfig& cfg, const std::string& argv0) {
    QueryBacking backing;
    backing.adapterPath = resolveAdapter(cfg.adapterPath, argv0);
    backing.mockFixture = cfg.mockFixture;
    backing.target = cfg.target;
    std::string regErr;
    if (!loadViewRegistry(cfg.debugMetaPath, backing.views, regErr)) {
        std::fprintf(stderr, "topo-debug: %s\n", regErr.c_str());
        return 1;
    }
    loadPassReportsFromMeta(cfg.debugMetaPath, backing);

    DispatchedSummary r = dispatchOneSummary(cfg.summaryTopoName, cfg.site,
                                              cfg.debugMetaPath, backing);
    if (!r.ok) {
        std::fprintf(stderr, "topo-debug: %s\n", r.error.c_str());
        return r.cliExit;
    }
    std::fputs((r.rendered + "\n").c_str(), stdout);
    return 0;
}

// ---------- snapshot subcommand ----------
//
// Snapshot file layout (TOPO_SNAP_V1):
//
//   TOPO_SNAP_V1\n
//   <decimal byte length of json manifest>\n
//   <json manifest (no trailing newline)>
//   <raw bytes concat — one blob per var, in vars[] order; lengths from
//    manifest.vars[i].byte_length>
//
// Manifest:
//   { "schema": "topo-snap-v1",
//     "symbol": "Buf",
//     "site": "main.cpp:13",
//     "frame": 0,
//     "vars": [
//       {"name": "data", "dtype": "i32", "shape": [16], "byte_length": 64}
//     ] }
//
// Write is atomic: bytes go to <out>.tmp, fsync, rename to <out>.

bool writeSnapshot(const std::string& outPath,
                   const std::string& topoName,
                   const AdapterEnv& ae,
                   const std::vector<std::string>& varOrder,
                   std::string& err) {
    json manifest = json::object();
    manifest["schema"] = "topo-snap-v1";
    manifest["symbol"] = topoName;
    manifest["site"] = ae.site;
    manifest["frame"] = ae.frameId;
    json varsJson = json::array();
    // Serialize vars in the order requested by varOrder (matching the
    // adapter's --var csv order). Concatenated blobs follow the same order.
    std::vector<const FrameView*> orderedViews;
    orderedViews.reserve(varOrder.size());
    for (const auto& name : varOrder) {
        auto it = ae.env.variables.find(name);
        if (it == ae.env.variables.end()) {
            err = "snapshot: adapter did not return variable '" + name + "'";
            return false;
        }
        const FrameView& fv = it->second;
        topo::debug_query::ByteSpan bs = fv.bytes();
        json v = json::object();
        v["name"] = name;
        v["dtype"] = fv.layout().dtype;
        v["shape"] = fv.layout().shape;
        v["byte_length"] = static_cast<int64_t>(bs.size);
        varsJson.push_back(std::move(v));
        orderedViews.push_back(&fv);
    }
    manifest["vars"] = std::move(varsJson);
    std::string manifestStr = manifest.dump();

    std::string tmpPath = outPath + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "snapshot: cannot open '" + tmpPath + "' for write";
            return false;
        }
        out << "TOPO_SNAP_V1\n";
        out << manifestStr.size() << "\n";
        out.write(manifestStr.data(),
                  static_cast<std::streamsize>(manifestStr.size()));
        for (const auto* fv : orderedViews) {
            topo::debug_query::ByteSpan bs = fv->bytes();
            if (bs.size > 0) {
                out.write(reinterpret_cast<const char*>(bs.data),
                          static_cast<std::streamsize>(bs.size));
            }
        }
        out.flush();
        if (!out) {
            err = "snapshot: write failure on '" + tmpPath + "'";
            return false;
        }
    }
    // Best-effort fsync via re-opening the FD; the C++ standard streams
    // don't expose it. POSIX-style fsync — non-fatal on platforms that
    // don't support it (Windows just drops the call).
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    {
        int fd = ::open(tmpPath.c_str(), O_RDONLY);
        if (fd >= 0) {
            ::fsync(fd);
            ::close(fd);
        }
    }
#endif
    std::error_code ec;
    std::filesystem::rename(tmpPath, outPath, ec);
    if (ec) {
        err = "snapshot: rename '" + tmpPath + "' -> '" + outPath + "' failed: " + ec.message();
        return false;
    }
    return true;
}

int runSnapshotSubcommand(const CliConfig& cfg, const std::string& argv0) {
    std::vector<std::string> containers;
    std::string err;
    if (!collectSymbolContainers(cfg.debugMetaPath, cfg.snapshotTopoName,
                                  containers, err)) {
        std::fprintf(stderr, "topo-debug: %s\n", err.c_str());
        return 1;
    }

    QueryBacking backing;
    backing.adapterPath = resolveAdapter(cfg.adapterPath, argv0);
    backing.target = cfg.target;
    // No view rewrite needed — we capture the raw containers directly.

    AdapterEnv ae = buildEnvFromAdapter(containers, cfg.site, backing);
    if (!ae.ok) {
        std::fprintf(stderr, "topo-debug: %s\n", ae.error.c_str());
        return ae.cliExit;
    }

    std::string writeErr;
    if (!writeSnapshot(cfg.snapshotOutPath, cfg.snapshotTopoName,
                       ae, containers, writeErr)) {
        std::fprintf(stderr, "topo-debug: %s\n", writeErr.c_str());
        return 1;
    }
    std::fprintf(stdout,
                 "wrote snapshot: %s (symbol=%s, site=%s, vars=%zu)\n",
                 cfg.snapshotOutPath.c_str(),
                 cfg.snapshotTopoName.c_str(),
                 ae.site.c_str(),
                 containers.size());
    return 0;
}

// ---------- query --replay loader ----------
//
// Parse a TOPO_SNAP_V1 file produced by runSnapshotSubcommand and reconstruct
// the same `AdapterEnv` that the live adapter would have produced. Symmetric
// to writeSnapshot — every layout field on disk maps 1:1 to a FrameView.

bool loadSnapshot(const std::string& path, AdapterEnv& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open snapshot '" + path + "'";
        return false;
    }
    std::string magic;
    if (!std::getline(in, magic)) {
        err = "snapshot '" + path + "': missing magic header";
        return false;
    }
    if (magic != "TOPO_SNAP_V1") {
        err = "snapshot '" + path + "': bad magic '" + magic + "' (expected TOPO_SNAP_V1)";
        return false;
    }
    std::string sizeLine;
    if (!std::getline(in, sizeLine)) {
        err = "snapshot '" + path + "': missing manifest size";
        return false;
    }
    size_t manifestLen = 0;
    try {
        manifestLen = static_cast<size_t>(std::stoull(sizeLine));
    } catch (...) {
        err = "snapshot '" + path + "': manifest size is not an integer: " + sizeLine;
        return false;
    }
    std::string manifestStr(manifestLen, '\0');
    in.read(manifestStr.data(), static_cast<std::streamsize>(manifestLen));
    if (!in || static_cast<size_t>(in.gcount()) != manifestLen) {
        err = "snapshot '" + path + "': truncated manifest";
        return false;
    }
    json manifest;
    try {
        manifest = json::parse(manifestStr);
    } catch (const std::exception& e) {
        err = std::string("snapshot manifest JSON error: ") + e.what();
        return false;
    }
    if (manifest.value("schema", std::string{}) != "topo-snap-v1") {
        err = "snapshot manifest: unsupported schema (need 'topo-snap-v1')";
        return false;
    }
    if (!manifest.contains("vars") || !manifest["vars"].is_array()) {
        err = "snapshot manifest: missing 'vars' array";
        return false;
    }
    out.frameId = manifest.value("frame", static_cast<int64_t>(0));
    out.site = manifest.value("site", std::string{});

    for (const auto& vj : manifest["vars"]) {
        if (!vj.is_object()) {
            err = "snapshot manifest: 'vars' entry is not an object";
            return false;
        }
        std::string name = vj.value("name", std::string{});
        if (name.empty()) {
            err = "snapshot manifest: 'vars' entry missing 'name'";
            return false;
        }
        LayoutDescriptor layout;
        layout.dtype = vj.value("dtype", std::string{});
        if (vj.contains("shape") && vj["shape"].is_array()) {
            for (const auto& d : vj["shape"]) {
                layout.shape.push_back(d.get<int64_t>());
            }
        }
        size_t byteLen = static_cast<size_t>(vj.value("byte_length", static_cast<int64_t>(0)));
        std::vector<uint8_t> bytes(byteLen);
        if (byteLen > 0) {
            in.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(byteLen));
            if (!in || static_cast<size_t>(in.gcount()) != byteLen) {
                err = "snapshot '" + path + "': truncated blob for var '" + name + "'";
                return false;
            }
        }
        FrameView fv = FrameView::owned(name, std::move(bytes), std::move(layout));
        out.env.variables.emplace(std::move(name), std::move(fv));
    }
    out.ok = true;
    return true;
}

// ---------- query subcommand ----------

int runQuerySubcommand(const CliConfig& cfg, const std::string& argv0) {
    // --replay path: skip the adapter entirely. The view registry still
    // matters because identifier expansion (e.g. `first_half` -> a slice
    // over `data`) is independent of how the bytes were captured.
    if (!cfg.replayPath.empty()) {
        AdapterEnv ae;
        std::string loadErr;
        if (!loadSnapshot(cfg.replayPath, ae, loadErr)) {
            std::fprintf(stderr, "topo-debug: %s\n", loadErr.c_str());
            return 1;
        }

        ViewRegistry views;
        topo::debug_meta::PassReportsRegistry passReports;
        bool passReportsLoaded = false;
        if (!cfg.debugMetaPath.empty()) {
            std::string regErr;
            if (!loadViewRegistry(cfg.debugMetaPath, views, regErr)) {
                std::fprintf(stderr, "topo-debug: %s\n", regErr.c_str());
                return 1;
            }
            // also probe sibling <stem>.topo-passes/.
            auto dir = passReportsDirForMeta(cfg.debugMetaPath);
            if (!dir.empty()) {
                std::string prErr;
                auto reg = topo::debug_meta::PassReportsRegistry::loadFromDirectory(
                    dir, &prErr);
                if (reg) {
                    passReports = std::move(*reg);
                    passReportsLoaded = true;
                }
            }
        }

        std::string parseErr;
        auto expr = parseQuery(cfg.exprText, parseErr);
        if (!expr) {
            std::fprintf(stderr, "topo-debug: query parse error: %s\n",
                         parseErr.c_str());
            return 1;
        }
        expandViews(expr, views);

        if (passReportsLoaded) ae.env.passReports = &passReports;
        EvalResult evalRes = evaluate(*expr, ae.env);
        if (!evalRes.ok) {
            std::fprintf(stderr, "topo-debug: query error: %s\n",
                         evalRes.error.c_str());
            return 3;
        }
        if (cfg.format == "jsonl") {
            json out = json::object();
            out["expr"] = cfg.exprText;
            out["frame"] = ae.frameId;
            out["type"] = formatTypeName(evalRes.value);
            try {
                out["result"] = json::parse(formatJsonValue(evalRes.value));
            } catch (...) {
                out["result"] = formatJsonValue(evalRes.value);
            }
            std::fputs((out.dump() + "\n").c_str(), stdout);
        } else {
            std::fputs((formatText(evalRes.value) + "\n").c_str(), stdout);
        }
        return 0;
    }

    // static (sidecar-only) path. Triggered when
    // the user omits --break: no adapter spawn, no live process, just load
    // the sidecar registry and evaluate. parseCli already validated that
    // --debug-meta or --target is present so we have a directory hint.
    if (cfg.site.empty()) {
        std::filesystem::path passesDir;
        std::string metaPath = cfg.debugMetaPath;
        if (metaPath.empty() && !cfg.target.empty()) {
            std::string autoPath = cfg.target + ".topo-dbg.json";
            if (std::filesystem::exists(autoPath)) metaPath = autoPath;
        }
        if (!metaPath.empty()) {
            passesDir = passReportsDirForMeta(metaPath);
        }
        // Fallback: <target>.topo-passes/ even when no dbg.json exists. JVM
        // / V8 projects that compile sidecars without `debug { ... }`
        // declarations land here — the protocol owner (Pass-reports) is
        // independent of the view-registry owner (dbg.json).
        if ((passesDir.empty() || !std::filesystem::exists(passesDir)) &&
            !cfg.target.empty()) {
            passesDir = std::filesystem::path(cfg.target + ".topo-passes");
        }

        topo::debug_meta::PassReportsRegistry passReports;
        bool passReportsLoaded = false;
        if (!passesDir.empty()) {
            std::string regErr;
            auto reg = topo::debug_meta::PassReportsRegistry::loadFromDirectory(
                passesDir, &regErr);
            if (reg) {
                passReports = std::move(*reg);
                passReportsLoaded = true;
            }
        }

        std::string parseErr;
        auto expr = parseQuery(cfg.exprText, parseErr);
        if (!expr) {
            std::fprintf(stderr, "topo-debug: query parse error: %s\n",
                         parseErr.c_str());
            return 1;
        }

        Environment env;
        if (passReportsLoaded) env.passReports = &passReports;
        EvalResult evalRes = evaluate(*expr, env);
        if (!evalRes.ok) {
            std::fprintf(stderr, "topo-debug: query error: %s\n",
                         evalRes.error.c_str());
            return 3;
        }
        if (cfg.format == "jsonl") {
            json out = json::object();
            out["expr"] = cfg.exprText;
            out["type"] = formatTypeName(evalRes.value);
            try {
                out["result"] = json::parse(formatJsonValue(evalRes.value));
            } catch (...) {
                out["result"] = formatJsonValue(evalRes.value);
            }
            std::fputs((out.dump() + "\n").c_str(), stdout);
        } else {
            std::fputs((formatText(evalRes.value) + "\n").c_str(), stdout);
        }
        return 0;
    }

    QueryBacking backing;
    backing.adapterPath = resolveAdapter(cfg.adapterPath, argv0);
    backing.mockFixture = cfg.mockFixture;
    backing.target = cfg.target;

    // Auto-discover *.topo-dbg.json next to --target if --debug-meta omitted.
    std::string metaPath = cfg.debugMetaPath;
    if (metaPath.empty() && !cfg.target.empty()) {
        std::string autoPath = cfg.target + ".topo-dbg.json";
        if (std::filesystem::exists(autoPath)) metaPath = autoPath;
    }
    if (!metaPath.empty()) {
        std::string regErr;
        if (!loadViewRegistry(metaPath, backing.views, regErr)) {
            std::fprintf(stderr, "topo-debug: %s\n", regErr.c_str());
            return 1;
        }
        // sibling sidecar dir probe.
        loadPassReportsFromMeta(metaPath, backing);
    }

    DispatchedQuery r = dispatchOneQuery(cfg.exprText, cfg.site, backing);
    if (!r.ok) {
        std::fprintf(stderr, "topo-debug: %s\n", r.error.c_str());
        return r.cliExit;
    }

    if (cfg.format == "jsonl") {
        json out = json::object();
        out["expr"] = cfg.exprText;
        out["frame"] = r.frameId;
        out["type"] = r.typeName;
        out["result"] = r.valueJson;
        std::fputs((out.dump() + "\n").c_str(), stdout);
    } else {
        std::fputs((r.valueText + "\n").c_str(), stdout);
    }
    return 0;
}

// ---------- serve subcommand ----------

static topo::debug_server::HttpServer* g_runningServer = nullptr;
static topo::debug_server::HttpServer* g_aiExportServer = nullptr;

extern "C" void onTerminationSignal(int) {
    // Request stop on BOTH servers. requestStop() only does async-signal-safe
    // work (an atomic store + a write() to a self-pipe), so this is safe in a
    // signal handler. Signalling the AI-export server too means it stops even
    // if the main loop has already exited and is waiting to join its thread.
    if (g_runningServer) g_runningServer->requestStop();
    if (g_aiExportServer) g_aiExportServer->requestStop();
}

// Load the entire debug-meta file into memory so each GET /dbg.json is a
// trivial string copy. Files are produced by `topo build` and are typically
// well under 1 MiB; we cap at 64 MiB defensively. Reload-on-disk-change is a
// later enhancement (watch persistence).
bool readFileToString(const std::string& path, std::string& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open '" + path + "'";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    if (out.size() > 64ull * 1024 * 1024) {
        err = "debug-meta exceeds 64 MiB safety cap";
        return false;
    }
    return true;
}

int runServeSubcommand(const CliConfig& cfg, const std::string& argv0) {
#ifdef _WIN32
    (void)cfg; (void)argv0;
    std::fprintf(stderr,
        "topo-debug serve: not supported on Windows in this build "
        "(POSIX-only — see tradeoff debug-serve-posix-only).\n");
    return 2;
#else
    using namespace topo::debug_server;

    // Eagerly load the debug-meta JSON. The serve loop hands out the cached
    // string verbatim — this also surfaces parse / path errors immediately
    // rather than per-request.
    std::string dbgJson;
    if (cfg.debugMetaPath.empty()) {
        std::fprintf(stderr,
                     "topo-debug serve: --debug-meta <path> is required (no auto-discovery in serve mode)\n");
        return 1;
    }
    {
        std::string err;
        if (!readFileToString(cfg.debugMetaPath, dbgJson, err)) {
            std::fprintf(stderr, "topo-debug serve: %s\n", err.c_str());
            return 1;
        }
        // Verify it parses, but keep the raw text for byte-for-byte serving.
        try { (void)json::parse(dbgJson); }
        catch (const std::exception& e) {
            std::fprintf(stderr, "topo-debug serve: debug-meta is not valid JSON: %s\n", e.what());
            return 1;
        }
    }

    // Optional /query backing — only configured when adapter selection is
    // complete. Otherwise /query returns 503 with a helpful hint.
    QueryBacking backing;
    bool queryEnabled = false;
    if (!cfg.serveMockFixture.empty() || !cfg.serveTarget.empty() ||
        !cfg.serveAttachPid.empty()) {
        backing.adapterPath = resolveAdapter(cfg.serveAdapter, argv0);
        backing.mockFixture = cfg.serveMockFixture;
        backing.target = cfg.serveTarget;
        // attach mode: adapter attaches to a running pid.
        backing.attachPid = cfg.serveAttachPid;
        std::string regErr;
        if (!loadViewRegistry(cfg.debugMetaPath, backing.views, regErr)) {
            std::fprintf(stderr, "topo-debug serve: %s\n", regErr.c_str());
            return 1;
        }
        loadPassReportsFromMeta(cfg.debugMetaPath, backing);
        queryEnabled = true;
    }

    // render-fn loader. Always built (the .topo
    // render_decl registry is consulted by POST /render even when no
    // search paths are registered, so the error surface stays consistent).
    // Initialised here so the lambda below captures by reference.
    RenderDeclRegistry renderDecls;
    {
        std::string rdErr;
        if (!loadRenderDeclRegistry(cfg.debugMetaPath, renderDecls, rdErr)) {
            std::fprintf(stderr, "topo-debug serve: %s\n", rdErr.c_str());
            return 1;
        }
    }
    RenderFnLoader renderLoader;
    for (const auto& spec : cfg.serveRenderFnSpecs) {
        RenderBackend backend;
        std::string dir, specErr;
        if (!parseRenderFnPathSpec(spec, backend, dir, specErr)) {
            std::fprintf(stderr, "topo-debug serve: %s\n", specErr.c_str());
            return 1;
        }
        renderLoader.addSearchPath(backend, dir);
    }
    const bool renderEnabled = renderLoader.hasAnyPath();

    // Resolve vendor + templates directories. Priority:
    //   1) CLI override (--vendor-path / --template-path)
    //   2) Install-relative path: <exe_dir>/../share/topo-core/topo-debug/<sub>
    //      (lands here when the binary is installed via cmake --install).
    //   3) Compile-time default baked in by topo-core/tools/topo-debug/
    //      CMakeLists.txt — points at the build tree (works in dev mode).
    auto resolveDir = [&argv0](const std::string& override_,
                               const char* installSub,
                               const char* compileDefault) -> std::string {
        if (!override_.empty()) return override_;
        std::filesystem::path self(argv0);
        std::error_code ec;
        auto canon = std::filesystem::weakly_canonical(self, ec);
        if (!ec) {
            auto installDir = canon.parent_path().parent_path()
                / "share" / "topo-core" / "topo-debug" / installSub;
            if (std::filesystem::exists(installDir)) {
                return installDir.string();
            }
        }
        return compileDefault ? std::string(compileDefault) : std::string();
    };
    std::string vendorDir = resolveDir(cfg.serveVendorPath, "vendor",
#ifdef TOPO_DEBUG_VENDOR_DEFAULT_DIR
        TOPO_DEBUG_VENDOR_DEFAULT_DIR
#else
        nullptr
#endif
    );
    // The templates dir is read at request time, so a user/LLM edit takes
    // effect on the next browser refresh with no tool rebuild
    // (acceptance: template editing live-reload).
    std::string templatesDir = resolveDir(cfg.serveTemplatePath, "templates",
#ifdef TOPO_DEBUG_TEMPLATES_DEFAULT_DIR
        TOPO_DEBUG_TEMPLATES_DEFAULT_DIR
#else
        nullptr
#endif
    );

    // REPL history ring shared with the AI export server's
    // GET /ai/recent-queries. Declared before route registration so the
    // /query + /ws handlers can append to it. Capped to the last 50.
    std::shared_ptr<std::vector<json>> recentQueries =
        std::make_shared<std::vector<json>>();
    std::shared_ptr<std::mutex> recentMu = std::make_shared<std::mutex>();
    auto recordQuery = [recentQueries, recentMu](const std::string& expr,
                                                 const std::string& site,
                                                 bool ok,
                                                 const std::string& detail) {
        std::lock_guard<std::mutex> lk(*recentMu);
        json e = json::object();
        e["expr"] = expr;
        e["site"] = site;
        e["ok"] = ok;
        if (!detail.empty()) e["detail"] = detail;
        recentQueries->push_back(std::move(e));
        if (recentQueries->size() > 50) {
            recentQueries->erase(recentQueries->begin());
        }
    };

    HttpServer server(cfg.serveHost, cfg.servePort);

    server.route("GET", "/", [](const HttpRequest&) {
        return htmlOk(kIndexHtml);
    });
    server.route("GET", "/app.js", [](const HttpRequest&) {
        return jsOk(kAppJs);
    });
    server.route("GET", "/dbg.json", [&dbgJson](const HttpRequest&) {
        return jsonOk(dbgJson);
    });
    server.route("GET", "/healthz", [](const HttpRequest&) {
        return jsonOk("{\"ok\":true}");
    });
    auto exitToStatus = [](int cliExit) {
        if (cliExit == 1) return 400;
        if (cliExit == 2) return 503;
        if (cliExit == 3) return 422;
        return 500;
    };

    server.route("POST", "/query", [&backing, queryEnabled, exitToStatus,
                                     recordQuery](const HttpRequest& req) {
        if (!queryEnabled) {
            return textErr(503,
                "/query is not configured for this server. Restart with "
                "--mock-fixture <name>  or  --target <bin> --adapter <path>.");
        }
        std::string exprText, site;
        try {
            json body = json::parse(req.body);
            exprText = body.value("expr", std::string{});
            site = body.value("site", std::string{});
        } catch (const std::exception& e) {
            return textErr(400, std::string("invalid JSON body: ") + e.what());
        }
        if (exprText.empty() || site.empty()) {
            return textErr(400, "request body must include non-empty \"expr\" and \"site\"");
        }
        DispatchedQuery r = dispatchOneQuery(exprText, site, backing);
        json out = json::object();
        if (r.ok) {
            out["ok"] = true;
            out["expr"] = exprText;
            out["site"] = site;
            out["frame"] = r.frameId;
            out["type"] = r.typeName;
            out["result"] = r.valueJson;
            out["text"] = r.valueText;
            recordQuery(exprText, site, true, r.valueText);
            return jsonOk(out.dump(2));
        }
        out["ok"] = false;
        out["expr"] = exprText;
        out["site"] = site;
        out["error"] = r.error;
        out["cli_exit"] = r.cliExit;
        recordQuery(exprText, site, false, r.error);
        HttpResponse resp = jsonOk(out.dump(2));
        resp.status = exitToStatus(r.cliExit);
        return resp;
    });

    // POST /summary: render a symbol's `summary_template`
    // against a live frame. Body: { symbol, site }. Response echoes the
    // template, per-placeholder evaluations, and the final substituted text.
    const std::string metaPathCopy = cfg.debugMetaPath;

    // WebSocket-backed streaming query/summary. The SPA opens
    // `ws://host:port/ws` on load and reuses the connection for every
    // /query and /summary request (avoiding the per-call connect overhead
    // and unlocking later live-watch features without re-plumbing the wire
    // protocol). When upgrade fails the client falls back to POST.
    //
    // Frame contract (one JSON message per text frame, both directions):
    //   client → {"id":"<corr>","op":"query","expr":"sum(data)","site":"main.cpp:13"}
    //   client → {"id":"<corr>","op":"summary","symbol":"Mesh","site":"..."}
    //   server → {"id":"<corr>","ok":true,"result":..., "type":"...","text":"..."}
    //   server → {"id":"<corr>","ok":true,"rendered":"...","template":"..."}
    //   server → {"id":"<corr>","ok":false,"error":"...","cli_exit":N}
    server.routeRaw("GET", "/ws",
                    [&backing, queryEnabled, metaPathCopy](const HttpRequest& req, int connFd) {
        if (!wsCompleteHandshake(connFd, req.headers)) {
            // Best-effort: send a plain 400 before bailing.
            const char* err =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 16\r\n"
                "Connection: close\r\n\r\n"
                "ws upgrade fail";
            (void)::send(connFd, err, std::strlen(err), 0);
            return false;
        }
        for (;;) {
            std::string msg;
            if (!wsRecvTextFrame(connFd, msg)) return true;
            std::string id;
            std::string op;
            json reply = json::object();
            try {
                json req2 = json::parse(msg);
                id = req2.value("id", std::string{});
                op = req2.value("op", std::string{});
                reply["id"] = id;
                if (!queryEnabled) {
                    reply["ok"] = false;
                    reply["error"] =
                        "/ws op requires --mock-fixture or --target+--adapter at serve time";
                    reply["cli_exit"] = 2;
                } else if (op == "query") {
                    std::string expr = req2.value("expr", std::string{});
                    std::string site = req2.value("site", std::string{});
                    if (expr.empty() || site.empty()) {
                        reply["ok"] = false;
                        reply["error"] = "query requires non-empty 'expr' and 'site'";
                        reply["cli_exit"] = 1;
                    } else {
                        DispatchedQuery r = dispatchOneQuery(expr, site, backing);
                        reply["op"] = "query";
                        reply["expr"] = expr;
                        reply["site"] = site;
                        if (r.ok) {
                            reply["ok"] = true;
                            reply["frame"] = r.frameId;
                            reply["type"] = r.typeName;
                            reply["result"] = r.valueJson;
                            reply["text"] = r.valueText;
                        } else {
                            reply["ok"] = false;
                            reply["error"] = r.error;
                            reply["cli_exit"] = r.cliExit;
                        }
                    }
                } else if (op == "summary") {
                    std::string symbol = req2.value("symbol", std::string{});
                    std::string site = req2.value("site", std::string{});
                    if (symbol.empty() || site.empty()) {
                        reply["ok"] = false;
                        reply["error"] = "summary requires non-empty 'symbol' and 'site'";
                        reply["cli_exit"] = 1;
                    } else {
                        DispatchedSummary r =
                            dispatchOneSummary(symbol, site, metaPathCopy, backing);
                        reply["op"] = "summary";
                        reply["symbol"] = symbol;
                        reply["site"] = site;
                        reply["template"] = r.templateText;
                        reply["placeholders"] = r.placeholders;
                        if (r.ok) {
                            reply["ok"] = true;
                            reply["rendered"] = r.rendered;
                        } else {
                            reply["ok"] = false;
                            reply["error"] = r.error;
                            reply["cli_exit"] = r.cliExit;
                        }
                    }
                } else {
                    reply["ok"] = false;
                    reply["error"] = "unknown op '" + op + "' (expected 'query' or 'summary')";
                    reply["cli_exit"] = 1;
                }
            } catch (const std::exception& e) {
                reply["id"] = id;
                reply["ok"] = false;
                reply["error"] = std::string("invalid ws message: ") + e.what();
                reply["cli_exit"] = 1;
            }
            if (!wsSendTextFrame(connFd, reply.dump())) return true;
        }
    });

    server.route("POST", "/summary", [&backing, queryEnabled, metaPathCopy,
                                       exitToStatus](const HttpRequest& req) {
        if (!queryEnabled) {
            return textErr(503,
                "/summary is not configured for this server. Restart with "
                "--mock-fixture <name>  or  --target <bin> --adapter <path>.");
        }
        std::string symbol, site;
        try {
            json body = json::parse(req.body);
            symbol = body.value("symbol", std::string{});
            site = body.value("site", std::string{});
        } catch (const std::exception& e) {
            return textErr(400, std::string("invalid JSON body: ") + e.what());
        }
        if (symbol.empty() || site.empty()) {
            return textErr(400, "request body must include non-empty \"symbol\" and \"site\"");
        }
        DispatchedSummary r = dispatchOneSummary(symbol, site, metaPathCopy, backing);
        json out = json::object();
        out["symbol"] = symbol;
        out["site"] = site;
        out["template"] = r.templateText;
        out["placeholders"] = r.placeholders;
        if (r.ok) {
            out["ok"] = true;
            out["rendered"] = r.rendered;
            return jsonOk(out.dump(2));
        }
        out["ok"] = false;
        out["error"] = r.error;
        out["cli_exit"] = r.cliExit;
        HttpResponse resp = jsonOk(out.dump(2));
        resp.status = exitToStatus(r.cliExit);
        return resp;
    });

    // POST /render: dispatch a render_decl to its
    // user-authored dlopen'd render-fn. Body: { method, site, [input?] }.
    // The server resolves the input variables from the declared render_decl
    // (the user does not repeat them in the request), evaluates each via the
    // same dispatchOneQuery path that powers /query, packages the results
    // into a single JSON object (key = variable name, value = Compute layer
    // result), and feeds that to topo_render_<method>. The render-fn's
    // output JSON is returned under `output`.
    server.route("POST", "/render", [&renderDecls, &renderLoader, renderEnabled,
                                      &backing, queryEnabled, exitToStatus]
                                     (const HttpRequest& req) {
        std::string method, site;
        json bodyJson;
        try {
            bodyJson = json::parse(req.body);
            method = bodyJson.value("method", std::string{});
            site = bodyJson.value("site", std::string{});
        } catch (const std::exception& e) {
            return textErr(400, std::string("invalid JSON body: ") + e.what());
        }
        if (method.empty() || site.empty()) {
            return textErr(400, "request body must include non-empty \"method\" and \"site\"");
        }
        auto declIt = renderDecls.find(method);
        if (declIt == renderDecls.end()) {
            return textErr(404,
                "no `render method=" + method + "` declaration in debug-meta; "
                "render dispatch is allow-listed against declared methods to "
                "prevent path-traversal-style dlopen attempts");
        }
        if (!renderEnabled) {
            return textErr(503,
                "/render is not configured for this server. Restart with "
                "one or more --render-fn-path <dir> arguments pointing at "
                "directories containing libtopo_render_*.{so,dylib}.");
        }
        if (!queryEnabled) {
            return textErr(503,
                "/render needs a query backing to evaluate its input "
                "variables. Restart with --mock-fixture <name>  or  "
                "--target <bin> --adapter <path>.");
        }

        // Build the input JSON object by evaluating each declared input
        // variable through dispatchOneQuery. Each variable name is wrapped
        // in `sample(<var>, BIG)` so the evaluator returns the full array
        // contents as a JSON list rather than a FrameView metadata stub
        // (the bare-identifier form gives `{dtype, shape}` only). `sample`
        // clamps to the actual element count, so passing a large N is
        // safe — see Evaluator's `sample` builtin.
        constexpr int64_t kRenderInputSampleCap = 1 << 20;  // 1M elements
        json input = json::object();
        json varTrace = json::array();
        for (const auto& var : declIt->second.inputs) {
            std::string expr = "sample(" + var + ", " +
                std::to_string(kRenderInputSampleCap) + ")";
            DispatchedQuery qr = dispatchOneQuery(expr, site, backing);
            json one = json::object();
            one["var"] = var;
            if (qr.ok) {
                one["ok"] = true;
                one["type"] = qr.typeName;
                one["result"] = qr.valueJson;
                input[var] = qr.valueJson;
            } else {
                one["ok"] = false;
                one["error"] = qr.error;
                one["cli_exit"] = qr.cliExit;
                varTrace.push_back(one);
                // Bail out on the first variable failure — the render-fn
                // would otherwise get partial inputs and produce a
                // misleading payload.
                json out = json::object();
                out["ok"] = false;
                out["method"] = method;
                out["site"] = site;
                out["error"] = "input variable '" + var + "' failed to "
                              "evaluate: " + qr.error;
                out["cli_exit"] = qr.cliExit;
                out["variables"] = varTrace;
                HttpResponse resp = jsonOk(out.dump(2));
                resp.status = exitToStatus(qr.cliExit);
                return resp;
            }
            varTrace.push_back(one);
        }

        RenderResult rr = renderLoader.invoke(method, input.dump());
        json out = json::object();
        out["method"] = method;
        out["site"] = site;
        out["variables"] = varTrace;
        if (!rr.ok) {
            out["ok"] = false;
            out["error"] = rr.error;
            // The error is generated by user code; surface as 500 so the
            // SPA shows it as a server-side failure rather than a user
            // request error (the inputs were valid; the user's render-fn
            // returned false).
            HttpResponse resp = jsonOk(out.dump(2));
            resp.status = 500;
            return resp;
        }
        // Parse the render-fn output back into JSON so we can embed it
        // inside the wrapper response without double-encoding. If parsing
        // fails the function returned an invalid contract.
        try {
            out["ok"] = true;
            out["output"] = json::parse(rr.outputJson);
        } catch (const std::exception& e) {
            out["ok"] = false;
            out["error"] = std::string("render-fn '") + method +
                "' produced output that is not valid JSON: " + e.what();
            HttpResponse resp = jsonOk(out.dump(2));
            resp.status = 500;
            return resp;
        }
        return jsonOk(out.dump(2));
    });

    // GET /vendor/<file>: serve the third-party
    // JS/CSS files (Chart.js etc.) that the SPA loads at runtime. Strict
    // basename-only lookup: subdirectories and path traversal characters
    // are rejected before touching the filesystem.
    // GET /templates/<name>: render a user-editable
    // `*.html.tpl` mustache-subset template against the current frame /
    // symbol / render data. The file is read FRESH on every request so a
    // user/LLM edit takes effect on the next browser refresh with NO tool
    // rebuild (acceptance criterion). Data binding:
    //   ?symbol=<S>&site=<L>   → POST /summary data { symbol, site,
    //                            rendered, template, placeholders[] }
    //   ?method=<M>&site=<L>   → POST /render  data { method, site, output }
    //   (none)                 → { symbols: [...] } from debug-meta
    const std::string templatesDirCopy = templatesDir;
    const std::string dbgJsonCopy = dbgJson;
    auto renderTemplateRoute =
        [templatesDirCopy, dbgJsonCopy, &renderDecls, &renderLoader,
         renderEnabled, &backing, queryEnabled, metaPathCopy]
        (const std::string& name, const std::string& query) -> HttpResponse {
        if (name.empty() || name[0] == '.' ||
            name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos ||
            name.find("..") != std::string::npos) {
            return textErr(400, "invalid template name");
        }
        if (templatesDirCopy.empty()) {
            return textErr(503,
                "templates directory is not configured. Pass "
                "--template-path <dir> or rebuild Topo so the default "
                "build dir is baked into the binary.");
        }
        std::filesystem::path p =
            std::filesystem::path(templatesDirCopy) / name;
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) {
            return textErr(404, "template '" + name + "' not found");
        }
        std::ifstream in(p, std::ios::binary);
        if (!in) return textErr(500, "template '" + name + "' open failed");
        std::ostringstream oss;
        oss << in.rdbuf();
        std::string tpl = oss.str();

        // Parse the query string (very small: key=value&key=value, no %xx
        // decoding beyond the common '+' = space — debug sites are file:line
        // which need no escaping).
        std::map<std::string, std::string> q;
        {
            size_t i = 0;
            while (i < query.size()) {
                size_t amp = query.find('&', i);
                std::string kv = query.substr(
                    i, amp == std::string::npos ? std::string::npos : amp - i);
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = kv.substr(0, eq);
                    std::string v = kv.substr(eq + 1);
                    for (auto& c : v) if (c == '+') c = ' ';
                    q[k] = v;
                }
                if (amp == std::string::npos) break;
                i = amp + 1;
            }
        }

        json data = json::object();
        if (q.count("symbol") && q.count("site")) {
            if (queryEnabled) {
                DispatchedSummary r = dispatchOneSummary(
                    q["symbol"], q["site"], metaPathCopy, backing);
                data["symbol"] = q["symbol"];
                data["site"] = q["site"];
                data["template"] = r.templateText;
                data["rendered"] = r.ok ? r.rendered : std::string{};
                data["ok"] = r.ok;
                json ph = json::array();
                for (const auto& p2 : r.placeholders) {
                    if (p2.contains("expr"))
                        ph.push_back(p2["expr"].get<std::string>());
                }
                data["placeholders"] = ph;
            } else {
                data["symbol"] = q["symbol"];
                data["site"] = q["site"];
                data["rendered"] = "";
                data["error"] = "no query backing configured";
            }
        } else if (q.count("method") && q.count("site")) {
            data["method"] = q["method"];
            data["site"] = q["site"];
            auto declIt = renderDecls.find(q["method"]);
            if (declIt != renderDecls.end() && renderEnabled && queryEnabled) {
                json input = json::object();
                bool inputOk = true;
                for (const auto& var : declIt->second.inputs) {
                    DispatchedQuery qr = dispatchOneQuery(
                        "sample(" + var + ", 1048576)", q["site"], backing);
                    if (qr.ok) {
                        input[var] = qr.valueJson;
                    } else { inputOk = false; break; }
                }
                if (inputOk) {
                    RenderResult rr =
                        renderLoader.invoke(q["method"], input.dump());
                    if (rr.ok) {
                        try { data["output"] = json::parse(rr.outputJson); }
                        catch (...) { data["output"] = rr.outputJson; }
                    } else {
                        data["error"] = rr.error;
                    }
                }
            }
        } else {
            // No binding params — expose the symbol table so a template can
            // iterate {{#each symbols}}.
            try {
                json meta = json::parse(dbgJsonCopy);
                json syms = json::array();
                if (meta.contains("symbols") && meta["symbols"].is_array()) {
                    for (const auto& s : meta["symbols"]) {
                        json one = json::object();
                        one["topo_name"] = s.value("topo_name", "");
                        one["kind"] = s.value("kind", "");
                        one["host_symbol"] = s.value("host_symbol", "");
                        syms.push_back(std::move(one));
                    }
                }
                data["symbols"] = std::move(syms);
            } catch (...) {}
        }

        topo::debug::TemplateRenderResult tr =
            topo::debug::renderTemplate(tpl, data);
        if (!tr.ok) {
            return textErr(422,
                "template '" + name + "' render error: " + tr.error);
        }
        HttpResponse resp;
        resp.status = 200;
        resp.contentType = "text/html; charset=utf-8";
        resp.body = tr.output;
        return resp;
    };

    const std::string vendorDirCopy = vendorDir;
    server.fallback([vendorDirCopy, renderTemplateRoute](const HttpRequest& req) {
        const std::string tplPrefix = "/templates/";
        if (req.method == "GET" && req.path.rfind(tplPrefix, 0) == 0) {
            std::string rest = req.path.substr(tplPrefix.size());
            std::string name = rest, query;
            size_t qm = rest.find('?');
            if (qm != std::string::npos) {
                name = rest.substr(0, qm);
                query = rest.substr(qm + 1);
            }
            return renderTemplateRoute(name, query);
        }
        const std::string prefix = "/vendor/";
        if (req.method == "GET" && req.path.rfind(prefix, 0) == 0) {
            std::string name = req.path.substr(prefix.size());
            // Reject empties, hidden files, separators, and any kind of
            // traversal token. We deliberately do NOT canonicalise &
            // re-check, because a strict character allow-list is easier
            // to audit and the vendor directory is flat by contract.
            if (name.empty() || name[0] == '.' ||
                name.find('/') != std::string::npos ||
                name.find('\\') != std::string::npos ||
                name.find("..") != std::string::npos) {
                return textErr(400, "invalid vendor file name");
            }
            if (vendorDirCopy.empty()) {
                return textErr(503,
                    "vendor static directory is not configured. Pass "
                    "--vendor-path <dir> or rebuild Topo so the default "
                    "build dir is baked into the binary.");
            }
            std::filesystem::path p =
                std::filesystem::path(vendorDirCopy) / name;
            std::error_code ec;
            if (!std::filesystem::exists(p, ec)) {
                return textErr(404, "vendor file '" + name + "' not found");
            }
            std::ifstream in(p, std::ios::binary);
            if (!in) {
                return textErr(500, "vendor file '" + name + "' open failed");
            }
            std::ostringstream oss;
            oss << in.rdbuf();
            HttpResponse resp;
            resp.status = 200;
            // Crude content-type sniffing — sufficient for the JS/CSS
            // bundles we ship. The browser is the consumer; everything
            // else falls back to octet-stream.
            if (name.size() >= 3 && name.substr(name.size() - 3) == ".js") {
                resp.contentType = "application/javascript";
            } else if (name.size() >= 4 && name.substr(name.size() - 4) == ".css") {
                resp.contentType = "text/css; charset=utf-8";
            } else if (name.size() >= 5 && name.substr(name.size() - 5) == ".json") {
                resp.contentType = "application/json";
            } else if (name.size() >= 3 && name.substr(name.size() - 3) == ".md") {
                resp.contentType = "text/markdown; charset=utf-8";
            } else if (name.size() >= 5 &&
                       name.substr(name.size() - 5) == ".html") {
                // speedscope ships as a single-file HTML
                // SPA the main page embeds via <iframe src=/vendor/...>.
                resp.contentType = "text/html; charset=utf-8";
            } else {
                resp.contentType = "application/octet-stream";
            }
            resp.body = oss.str();
            return resp;
        }
        return textErr(404, "not found");
    });

    // `--once <expr>`: evaluate the expression a single
    // time against the configured backing, emit the JSON result on stdout
    // (the "push to browser" surface is the same JSON the SPA's /query
    // returns; a one-shot CLI consumer / smoke test reads it here), and
    // exit WITHOUT entering the accept loop. Requires --break + a backing
    // (validated in parseCli).
    if (!cfg.serveOnceExpr.empty()) {
        DispatchedQuery r =
            dispatchOneQuery(cfg.serveOnceExpr, cfg.serveOnceSite, backing);
        json out = json::object();
        out["expr"] = cfg.serveOnceExpr;
        out["site"] = cfg.serveOnceSite;
        if (r.ok) {
            out["ok"] = true;
            out["frame"] = r.frameId;
            out["type"] = r.typeName;
            out["result"] = r.valueJson;
            out["text"] = r.valueText;
            std::fprintf(stdout, "%s\n", out.dump(2).c_str());
            std::fflush(stdout);
            return 0;
        }
        out["ok"] = false;
        out["error"] = r.error;
        out["cli_exit"] = r.cliExit;
        std::fprintf(stdout, "%s\n", out.dump(2).c_str());
        std::fflush(stdout);
        return r.cliExit ? r.cliExit : 1;
    }

    // read-only AI-collaboration export server. A SECOND
    // HttpServer bound to 127.0.0.1 only (never the configured --host, even
    // if the user widened the main server) exposing three GET endpoints an
    // LLM can poll without mutating state:
    //   GET /ai/current-frame   summary + structured Compute result
    //   GET /ai/symbols         the *.topo-dbg.json symbol table
    //   GET /ai/recent-queries  REPL history captured this process
    // No auth, loopback-only (same trust model as Jupyter).
    std::unique_ptr<HttpServer> aiServer;
    std::unique_ptr<std::thread> aiThread;
    if (cfg.serveAiEnabled) {
        aiServer = std::make_unique<HttpServer>("127.0.0.1", cfg.serveAiPort);
        const std::string aiDbg = dbgJson;
        aiServer->route("GET", "/healthz", [](const HttpRequest&) {
            return jsonOk("{\"ok\":true}");
        });
        aiServer->route("GET", "/ai/symbols",
                        [aiDbg](const HttpRequest&) {
            try {
                json meta = json::parse(aiDbg);
                json out = json::object();
                out["ok"] = true;
                out["schema_version"] = meta.value("schema_version", json{});
                out["symbols"] = meta.contains("symbols")
                                      ? meta["symbols"] : json::array();
                return jsonOk(out.dump(2));
            } catch (const std::exception& e) {
                return textErr(500,
                    std::string("debug-meta parse failed: ") + e.what());
            }
        });
        aiServer->route("GET", "/ai/recent-queries",
                        [recentQueries, recentMu](const HttpRequest&) {
            std::lock_guard<std::mutex> lk(*recentMu);
            json out = json::object();
            out["ok"] = true;
            out["queries"] = *recentQueries;
            return jsonOk(out.dump(2));
        });
        aiServer->route("GET", "/ai/current-frame",
                        [aiDbg, queryEnabled](const HttpRequest&) {
            json out = json::object();
            out["ok"] = true;
            out["query_backing"] = queryEnabled;
            try {
                json meta = json::parse(aiDbg);
                out["schema_version"] = meta.value("schema_version", json{});
                out["source"] = meta.contains("source")
                                    ? meta["source"] : json{};
                out["symbol_count"] =
                    meta.contains("symbols") && meta["symbols"].is_array()
                        ? static_cast<int>(meta["symbols"].size()) : 0;
            } catch (...) {}
            out["note"] = "read-only AI export; POST a query via the main "
                          "server's /query to populate recent-queries";
            return jsonOk(out.dump(2));
        });
        aiServer->fallback([](const HttpRequest&) {
            return textErr(404,
                "AI export is read-only; only GET /ai/current-frame, "
                "/ai/symbols, /ai/recent-queries (and /healthz) exist");
        });
        aiThread = std::make_unique<std::thread>([&]() {
            std::string aiAddr;
            aiServer->run(aiAddr, [&]() {
                std::fprintf(stderr,
                    "topo-debug serve: AI export listening on "
                    "http://127.0.0.1:%u\n", aiServer->actualPort());
                std::fprintf(stderr,
                    "  ai routes: GET /ai/current-frame   GET /ai/symbols"
                    "   GET /ai/recent-queries\n");
                std::fflush(stderr);
            });
        });
    }

    g_runningServer = &server;
    g_aiExportServer = aiServer ? aiServer.get() : nullptr;
    std::signal(SIGINT, onTerminationSignal);
    std::signal(SIGTERM, onTerminationSignal);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif

    std::string addr;
    // the listening banner fires from inside run() once the
    // socket is bound, so harnesses passing `--port 0` see the OS-assigned
    // port without racing against accept(). The banner stays grep-friendly:
    // `listening on http://<host>:<port>` on a single line.
    auto onListen = [&]() {
        std::fprintf(stderr, "topo-debug serve: listening on http://%s:%u\n",
                     cfg.serveHost.c_str(), server.actualPort());
        std::fprintf(stderr,
                     "  routes: GET /   GET /app.js   GET /dbg.json   GET /healthz   GET /ws"
                     "   GET /vendor/<file>%s%s\n",
                     queryEnabled ? "   POST /query   POST /summary" : "",
                     renderEnabled ? "   POST /render" : "");
        std::fprintf(stderr,
                     "  debug-meta: %s%s\n",
                     cfg.debugMetaPath.c_str(),
                     queryEnabled ? "" : "  (POST /query and /summary disabled — no adapter configured)");
        if (!vendorDir.empty()) {
            std::fprintf(stderr, "  vendor:     %s\n", vendorDir.c_str());
        }
        if (!cfg.serveRenderFnSpecs.empty()) {
            std::fprintf(stderr, "  render-fn search path:\n");
            for (const auto& p : cfg.serveRenderFnSpecs) {
                std::fprintf(stderr, "    - %s\n", p.c_str());
            }
        } else {
            std::fprintf(stderr,
                "  (POST /render disabled — no --render-fn-path configured)\n");
        }
        if (!templatesDir.empty()) {
            std::fprintf(stderr, "  templates:  %s  (GET /templates/<name>)\n",
                         templatesDir.c_str());
        }
        std::fflush(stderr);

        // `--open`: launch the user's default browser at
        // the served URL once the listener is up. macOS uses `open`,
        // Linux `xdg-open`; failures are non-fatal (headless / no browser).
        if (cfg.serveOpen) {
            char urlBuf[256];
            std::snprintf(urlBuf, sizeof(urlBuf), "http://%s:%u/",
                          cfg.serveHost.c_str(), server.actualPort());
#if defined(__APPLE__)
            const char* opener = "open";
#else
            const char* opener = "xdg-open";
#endif
            std::fprintf(stderr,
                "topo-debug serve: --open launching %s %s\n",
                opener, urlBuf);
            std::fflush(stderr);
            (void)topo::platform::runProcess(
                opener, {std::string(urlBuf)}, false);
        }
    };

    int rc = server.run(addr, onListen);

    // tear the AI export server down with the main one.
    if (aiServer) {
        aiServer->requestStop();
        if (aiThread && aiThread->joinable()) aiThread->join();
    }

    g_runningServer = nullptr;
    g_aiExportServer = nullptr;
    return rc;
#endif // _WIN32
}

} // namespace

int main(int argc, char** argv) {
    CliConfig cfg;
    std::string err;
    if (!parseCli(argc, argv, cfg, err)) {
        if (!err.empty()) std::fprintf(stderr, "topo-debug: %s\n", err.c_str());
        printUsage(stderr);
        return 1;
    }
    if (cfg.subcommand == "query") return runQuerySubcommand(cfg, argv[0]);
    if (cfg.subcommand == "snapshot") return runSnapshotSubcommand(cfg, argv[0]);
    if (cfg.subcommand == "summary") return runSummarySubcommand(cfg, argv[0]);
    if (cfg.subcommand == "serve") return runServeSubcommand(cfg, argv[0]);
    // Unreachable — parseCli rejects unknown subcommands.
    return 1;
}
