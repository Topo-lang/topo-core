// Multi-backend render-fn loader implementation.
//
// C++  : in-process dlopen (original slice).
// Java/Python/Node : out-of-process interpreter, stdin=input JSON,
//                    stdout=output JSON, non-zero exit / stderr = error.

#include "topo/Debug/Server/RenderFnLoader.h"

#include "topo/Platform/Process.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace topo::debug_server {

namespace {

const char* libExt() {
#if defined(__APPLE__)
    return "dylib";
#elif defined(_WIN32)
    return "dll";
#else
    return "so";
#endif
}

std::string libFileName(const std::string& method) {
    return std::string("libtopo_render_") + method + "." + libExt();
}

bool validMethodName(const std::string& method, std::string& err) {
    if (method.empty()) {
        err = "render-fn method name is empty";
        return false;
    }
    for (char c : method) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            err = "render-fn method name '" + method +
                  "' contains invalid character (only [A-Za-z0-9_] allowed)";
            return false;
        }
    }
    return true;
}

// `topo.render.<method>` → class name `topo.render.Foo` is awkward to derive
// generically; we keep the JVM contract simple: the class is literally
// `topo_render_<method>` in the default package, with a static main that
// pipes stdin→stdout. Mirrors the Python/Node module naming.
std::string moduleBase(const std::string& method) {
    return "topo_render_" + method;
}

// Run `exe args...`, feeding `input` to stdin, returning captured stdout.
// On non-zero exit or empty stdout, fills `err` from stderr/diagnostics.
bool runInterpreter(const std::string& exe,
                    const std::vector<std::string>& args,
                    const std::string& input, const std::string& backendTag,
                    const std::string& method, std::string& outStdout,
                    std::string& err) {
    topo::platform::PipedProcess proc;
    if (!proc.start(exe, args)) {
        err = backendTag + " render-fn '" + method + "': failed to spawn '" +
              exe + "' (is it on PATH?)";
        return false;
    }
    if (!input.empty()) {
        proc.write(input.data(), input.size());
    }
    proc.closeStdin();
    std::string captured;
    char buf[4096];
    for (;;) {
        size_t n = proc.read(buf, sizeof(buf));
        if (n == 0) break;
        captured.append(buf, n);
    }
    proc.stop(5000);
    int code = proc.exitCode();
    if (code != 0) {
        std::ostringstream os;
        os << backendTag << " render-fn '" << method
           << "' exited with code " << code;
        if (!captured.empty()) {
            os << "; output so far: " << captured;
        }
        err = os.str();
        return false;
    }
    if (captured.empty()) {
        err = backendTag + " render-fn '" + method +
              "' produced no stdout (expected a JSON document)";
        return false;
    }
    outStdout = std::move(captured);
    return true;
}

} // namespace

const char* renderBackendName(RenderBackend b) {
    switch (b) {
        case RenderBackend::kCpp: return "cpp";
        case RenderBackend::kJava: return "java";
        case RenderBackend::kPython: return "python";
        case RenderBackend::kNode: return "node";
    }
    return "?";
}

bool parseRenderFnPathSpec(const std::string& spec, RenderBackend& kindOut,
                           std::string& dirOut, std::string& errOut) {
    // Recognised prefixes. We only treat `kind:` as a backend selector when
    // `kind` is one of the four known names — otherwise a Windows path like
    // `C:\foo` would be misread. (Windows serve is out of scope, but the
    // guard keeps the parse unsurprising.)
    auto tryPrefix = [&](const char* name, RenderBackend b) -> bool {
        std::string p = std::string(name) + ":";
        if (spec.rfind(p, 0) == 0) {
            kindOut = b;
            dirOut = spec.substr(p.size());
            return true;
        }
        return false;
    };
    if (tryPrefix("cpp", RenderBackend::kCpp) ||
        tryPrefix("java", RenderBackend::kJava) ||
        tryPrefix("python", RenderBackend::kPython) ||
        tryPrefix("node", RenderBackend::kNode)) {
        if (dirOut.empty()) {
            errOut = "--render-fn-path '" + spec +
                     "' has an empty directory after the backend prefix";
            return false;
        }
        return true;
    }
    // Bare directory → C++ (backward compatible with the original slice).
    kindOut = RenderBackend::kCpp;
    dirOut = spec;
    return true;
}

void RenderFnLoader::addSearchPath(RenderBackend backend,
                                   const std::string& dir) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::string>* vec = nullptr;
    switch (backend) {
        case RenderBackend::kCpp: vec = &paths_.cpp; break;
        case RenderBackend::kJava: vec = &paths_.java; break;
        case RenderBackend::kPython: vec = &paths_.python; break;
        case RenderBackend::kNode: vec = &paths_.node; break;
    }
    if (std::find(vec->begin(), vec->end(), dir) != vec->end()) return;
    vec->push_back(dir);
}

bool RenderFnLoader::hasAnyPath() const {
    return !paths_.cpp.empty() || !paths_.java.empty() ||
           !paths_.python.empty() || !paths_.node.empty();
}

RenderResult RenderFnLoader::invoke(const std::string& method,
                                    const std::string& inputJson) {
    RenderResult r;
    std::string verr;
    if (!validMethodName(method, verr)) {
        r.error = verr;
        return r;
    }

    // Snapshot the path sets under the lock so the per-backend probes do
    // not hold mu_ across a (possibly slow) child-process render.
    std::vector<std::string> cppPaths, javaPaths, pyPaths, nodePaths;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cppPaths = paths_.cpp;
        javaPaths = paths_.java;
        pyPaths = paths_.python;
        nodePaths = paths_.node;
    }

    std::vector<std::string> tried;

    // C++ first (in-process, cheapest, original behaviour).
    if (!cppPaths.empty()) {
        RenderResult cr = invokeCpp(method, inputJson);
        // invokeCpp only returns "not found" via error text; treat a
        // not-found as a fall-through to other backends, but a real load
        // failure (file exists, dlopen rejected) is terminal.
        if (cr.ok) return cr;
        if (cr.error.rfind("no render-fn library found", 0) != 0) {
            // A concrete failure (bad .so / missing symbol / threw): stop.
            return cr;
        }
        tried.push_back("cpp(" + std::to_string(cppPaths.size()) + " path(s))");
    }

    auto tryProc = [&](RenderBackend b, const std::vector<std::string>& dirs,
                       const std::string& fileRel) -> bool {
        for (const auto& d : dirs) {
            std::filesystem::path p = std::filesystem::path(d) / fileRel;
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                r = invokeProcess(b, method, inputJson);
                return true;
            }
        }
        if (!dirs.empty()) {
            tried.push_back(std::string(renderBackendName(b)) + "(" +
                            std::to_string(dirs.size()) + " path(s), no " +
                            fileRel + ")");
        }
        return false;
    };

    std::string base = moduleBase(method);
    if (tryProc(RenderBackend::kJava, javaPaths, base + ".class")) return r;
    if (tryProc(RenderBackend::kPython, pyPaths, base + ".py")) return r;
    if (tryProc(RenderBackend::kNode, nodePaths, base + ".js")) return r;

    std::ostringstream os;
    os << "no render-fn found for method '" << method
       << "' in any registered backend.";
    if (tried.empty()) {
        os << " (no --render-fn-path configured)";
    } else {
        os << " Tried:";
        for (const auto& t : tried) os << "\n  - " << t;
    }
    r.ok = false;
    r.error = os.str();
    return r;
}

RenderResult RenderFnLoader::invokeCpp(const std::string& method,
                                       const std::string& inputJson) {
    RenderResult r;
    std::shared_ptr<LoadedCpp> entry;
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = cppCache_.find(method);
        if (it != cppCache_.end()) {
            entry = it->second;
        } else {
            std::vector<std::string> tried;
            std::string fileName = libFileName(method);
            std::unique_ptr<topo::platform::SharedLibrary> hit;
            std::string hitPath;
            for (const auto& dir : paths_.cpp) {
                std::filesystem::path candidate =
                    std::filesystem::path(dir) / fileName;
                tried.push_back(candidate.string());
                std::error_code ec;
                if (!std::filesystem::exists(candidate, ec)) continue;
                auto lib = std::make_unique<topo::platform::SharedLibrary>();
                if (!lib->load(candidate.string())) {
                    r.error = "failed to dlopen render-fn library at '" +
                              candidate.string() +
                              "' (path exists but loader rejected it)";
                    return r;
                }
                hit = std::move(lib);
                hitPath = candidate.string();
                break;
            }
            if (!hit) {
                std::ostringstream os;
                os << "no render-fn library found for method '" << method
                   << "'. Tried:";
                if (tried.empty()) {
                    os << " (no cpp --render-fn-path configured)";
                } else {
                    for (const auto& t : tried) os << "\n  - " << t;
                }
                r.error = os.str();
                return r;
            }
            std::string symbol = std::string("topo_render_") + method;
            void* p = hit->getSymbol(symbol);
            if (!p) {
                r.error = "render-fn library '" + hitPath +
                          "' has no exported symbol '" + symbol + "'";
                return r;
            }
            auto loaded = std::make_shared<LoadedCpp>();
            loaded->lib = std::move(hit);
            loaded->fn = reinterpret_cast<RenderFn>(p);
            cppCache_.emplace(method, loaded);
            entry = loaded;
        }
    }

    char* outJson = nullptr;
    char* outErr = nullptr;
    bool ok = false;
    try {
        ok = entry->fn(inputJson.c_str(), &outJson, &outErr);
    } catch (...) {
        if (outJson) std::free(outJson);
        if (outErr) std::free(outErr);
        r.error = "render-fn '" + method + "' threw a C++ exception across "
                  "the C ABI boundary; the function must trap exceptions "
                  "internally and report failure via its out_err parameter";
        return r;
    }

    if (ok) {
        if (outJson) {
            r.outputJson.assign(outJson);
        } else {
            r.error = "render-fn '" + method +
                      "' returned true but did not write *out_json_utf8";
            if (outErr) std::free(outErr);
            return r;
        }
        if (outErr) std::free(outErr);
        std::free(outJson);
        r.ok = true;
        return r;
    }

    if (outErr) {
        r.error = "render-fn '" + method + "' failed: " + outErr;
        std::free(outErr);
    } else {
        r.error = "render-fn '" + method +
                  "' returned false without writing *out_err_utf8";
    }
    if (outJson) std::free(outJson);
    return r;
}

RenderResult RenderFnLoader::invokeProcess(RenderBackend backend,
                                           const std::string& method,
                                           const std::string& inputJson) {
    RenderResult r;
    std::string base = moduleBase(method);

    // Locate the directory holding the module for `backend`.
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const std::vector<std::string>* dirs = nullptr;
        std::string fileRel;
        switch (backend) {
            case RenderBackend::kJava:
                dirs = &paths_.java; fileRel = base + ".class"; break;
            case RenderBackend::kPython:
                dirs = &paths_.python; fileRel = base + ".py"; break;
            case RenderBackend::kNode:
                dirs = &paths_.node; fileRel = base + ".js"; break;
            case RenderBackend::kCpp:
                r.error = "internal: invokeProcess called for cpp backend";
                return r;
        }
        for (const auto& d : *dirs) {
            std::filesystem::path p = std::filesystem::path(d) / fileRel;
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) { dir = d; break; }
        }
        if (dir.empty()) {
            r.error = std::string(renderBackendName(backend)) +
                      " render-fn '" + method + "' not found on its path";
            return r;
        }
    }

    // The Python/Node one-liners embed `dir` inside a single-quoted string
    // literal. A path containing a quote or backslash would let the user
    // break out of the literal; reject it rather than mis-escape. (cpp/java
    // do not interpolate into a script string so they are unaffected.)
    if (backend == RenderBackend::kPython || backend == RenderBackend::kNode) {
        if (dir.find('\'') != std::string::npos ||
            dir.find('\\') != std::string::npos ||
            dir.find('\n') != std::string::npos) {
            r.error = std::string(renderBackendName(backend)) +
                      " render-fn search dir contains a quote/backslash/"
                      "newline which is unsafe to embed in the interpreter "
                      "bootstrap; rename the directory";
            return r;
        }
    }

    std::string exe;
    std::vector<std::string> args;
    std::string tag = renderBackendName(backend);
    switch (backend) {
        case RenderBackend::kJava:
            // `java -cp <dir> topo_render_<method>` — the class's static
            // main reads stdin JSON and writes stdout JSON. <dir> on the
            // classpath is the URLClassLoader source.
            exe = "java";
            args = {"-cp", dir, base};
            break;
        case RenderBackend::kPython:
            // Run python3 with the search dir on sys.path; import the
            // module and call render(input). The module exposes
            // `render(input: dict) -> object`.
            exe = "python3";
            args = {
                "-c",
                "import sys,json;sys.path.insert(0," +
                    std::string("'") + dir + "');" +
                    "import " + base + " as m;" +
                    "print(json.dumps(m.render(json.load(sys.stdin))))"};
            break;
        case RenderBackend::kNode:
            // require() the CommonJS module and call its exported function.
            exe = "node";
            args = {
                "-e",
                "let d='';process.stdin.on('data',c=>d+=c);"
                "process.stdin.on('end',()=>{"
                "const f=require(" +
                    std::string("'") + dir + "/" + base + ".js');" +
                    "const fn=(typeof f==='function')?f:f.render;" +
                    "Promise.resolve(fn(JSON.parse(d))).then(o=>"
                    "process.stdout.write(JSON.stringify(o)))"
                    ".catch(e=>{console.error(String(e));process.exit(1);});"
                    "});"};
            break;
        case RenderBackend::kCpp:
            r.error = "internal: invokeProcess called for cpp backend";
            return r;
    }

    std::string out;
    std::string err;
    if (!runInterpreter(exe, args, inputJson, tag, method, out, err)) {
        r.ok = false;
        r.error = err;
        return r;
    }
    // Trim trailing newline the interpreter's print() typically appends so
    // the caller's json::parse() sees a clean document.
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    r.ok = true;
    r.outputJson = std::move(out);
    return r;
}

} // namespace topo::debug_server
