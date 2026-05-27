#ifndef TOPO_DEBUG_SERVER_RENDERFNLOADER_H
#define TOPO_DEBUG_SERVER_RENDERFNLOADER_H

// Dynamic loader for user-authored render functions.
//
// A small backend abstraction over the loader.
// Every backend resolves a render-fn named `<method>`
// from a registered search path and invokes it with a single Compute-layer
// JSON object, receiving JSON (or an error) back. Four backends ship:
//
//   C++     (kCpp)    dlopen `libtopo_render_<method>.{so,dylib}`, call the
//                     C-ABI export `extern "C" bool topo_render_<method>(
//                     const char* input_json, char** out_json_utf8,
//                     char** out_err_utf8)` — the callee mallocs the
//                     output and the loader free()s it. In-process,
//                     fastest, zero spawn.
//
//   Java    (kJava)   `java -cp <dir> topo.render.<Method>` — a class with a
//                     `public static void main(String[])` that reads the
//                     input JSON from stdin and writes output JSON to
//                     stdout. The classpath dir is the search path; the
//                     URLClassLoader is `java` itself loading <dir>.
//
//   Python  (kPython) `python3 -c "import topo_render_<method> as m; ..."`
//                     with the search path on PYTHONPATH. The module must
//                     expose `render(input: dict) -> object`.
//
//   Node    (kNode)   `node -e "require('<dir>/topo_render_<method>.js')..."`
//                     The CommonJS module must export a function taking the
//                     parsed input object and returning a JSON-able value.
//
// Backend selection is by `--render-fn-path <kind>:<dir>` prefix (kind ∈
// {cpp,java,python,node}); a bare `<dir>` defaults to cpp for backward
// compatibility with the original slice and its e2e fixtures.
//
// The non-C++ backends run the host interpreter as a short-lived child
// process. This deliberately trades a fork+exec per render for keeping the
// server process immune to a buggy user render-fn (open question:
// a crashing dlopen kills the server; an out-of-process interpreter does
// not). Stdin = input JSON; stdout = output JSON; non-zero exit or stderr
// text = error surfaced verbatim.

#include "topo/Platform/SharedLibrary.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace topo::debug_server {

// Result of one render-fn invocation. Exactly one of `outputJson` / `error`
// is meaningful, signalled by `ok`.
struct RenderResult {
    bool ok = false;
    std::string outputJson;   // valid JSON on success
    std::string error;        // human-readable diagnostic on failure
};

// Which host runtime backs a render-fn search path.
enum class RenderBackend { kCpp, kJava, kPython, kNode };

// Parse a `--render-fn-path` value of the form `[<kind>:]<dir>`. Returns
// false (and leaves `errOut`) on an unknown kind prefix. A bare directory
// (no recognised `kind:` prefix) maps to kCpp for backward compatibility.
bool parseRenderFnPathSpec(const std::string& spec, RenderBackend& kindOut,
                           std::string& dirOut, std::string& errOut);

// Human-readable backend name for diagnostics.
const char* renderBackendName(RenderBackend b);

class RenderFnLoader {
public:
    RenderFnLoader() = default;

    // Append a directory served by `backend`. Duplicate (backend,dir) adds
    // are a no-op. Order is the lookup order within a backend.
    void addSearchPath(RenderBackend backend, const std::string& dir);

    // Back-compat shim — registers `dir` as a C++ search path.
    void addSearchPath(const std::string& dir) {
        addSearchPath(RenderBackend::kCpp, dir);
    }

    // True when at least one search path (any backend) is registered.
    bool hasAnyPath() const;

    // Invoke the render-fn named `method` with the given JSON `inputJson`.
    // The loader tries each backend's search paths in registration order;
    // the first backend that resolves `method` handles the call. On any
    // failure returns `{ok=false, error=...}` with a backend-tagged
    // diagnostic. Method names are restricted to `[A-Za-z0-9_]` so they are
    // safe to slot into file names / class names without escaping.
    RenderResult invoke(const std::string& method, const std::string& inputJson);

private:
    using RenderFn = bool (*)(const char*, char**, char**);

    struct LoadedCpp {
        std::unique_ptr<topo::platform::SharedLibrary> lib;
        RenderFn fn = nullptr;
    };

    // C++ in-process path (original slice, unchanged behaviour).
    RenderResult invokeCpp(const std::string& method,
                           const std::string& inputJson);
    // Out-of-process interpreter path (Java / Python / Node).
    RenderResult invokeProcess(RenderBackend backend,
                               const std::string& method,
                               const std::string& inputJson);

    struct PathSet {
        std::vector<std::string> cpp;
        std::vector<std::string> java;
        std::vector<std::string> python;
        std::vector<std::string> node;
    };
    PathSet paths_;
    std::unordered_map<std::string, std::shared_ptr<LoadedCpp>> cppCache_;
    std::mutex mu_;
};

} // namespace topo::debug_server

#endif // TOPO_DEBUG_SERVER_RENDERFNLOADER_H
