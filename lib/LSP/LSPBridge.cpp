#include "topo/LSP/LSPBridge.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>

namespace topo::lsp {

// ============================================================================
// Lifecycle
// ============================================================================

LSPBridge::LSPBridge(std::string logPrefix) : logPrefix_(std::move(logPrefix)) {
    if (const char* env = std::getenv("TOPO_CHECK_NO_LSP")) {
        if (std::string(env) == "1") {
            lspDisabled_ = true;
            std::cerr << logPrefix_ << " LSP disabled via TOPO_CHECK_NO_LSP=1\n";
        }
    }
}

LSPBridge::~LSPBridge() {
    stop();
}

bool LSPBridge::startProcess(const std::string& exe, const std::vector<std::string>& args, const std::string& rootUri) {
    if (lspDisabled_) return false;
    connectionLost_ = false;

    std::cerr << logPrefix_ << " Starting: " << exe;
    for (const auto& a : args)
        std::cerr << " " << a;
    std::cerr << "\n";

    if (!process_.start(exe, args)) {
        std::cerr << logPrefix_ << " Failed to start\n";
        return false;
    }

    // Start reader thread
    stopping_ = false;
    readerThread_ = std::thread(&LSPBridge::readerLoop, this);

    // Send LSP initialize
    json clientCaps = json::object();

    // Declare semantic tokens support so servers advertise their legend
    clientCaps["textDocument"]["semanticTokens"] = {
        {"requests", {{"full", true}}},
        {"tokenTypes", {
            "namespace", "type", "class", "enum", "interface",
            "struct", "typeParameter", "parameter", "variable", "property",
            "enumMember", "event", "function", "method", "macro",
            "keyword", "modifier", "comment", "string", "number",
            "regexp", "operator", "decorator"
        }},
        {"tokenModifiers", {
            "declaration", "definition", "readonly", "static",
            "deprecated", "abstract", "async", "modification",
            "documentation", "defaultLibrary"
        }},
        {"formats", {"relative"}},
        {"dynamicRegistration", false}
    };

    // Request hierarchical document symbols so `range` covers the full
    // method body (for line→function lookup).  When this capability is
    // not set, some servers (notably jdtls) fall back to the legacy
    // SymbolInformation[] response whose `location.range` only covers
    // the identifier, breaking enclosing-function queries.
    clientCaps["textDocument"]["documentSymbol"] = {
        {"hierarchicalDocumentSymbolSupport", true},
        {"dynamicRegistration", false}
    };

    // Modern language servers (notably pyright) only treat a project as a
    // workspace — and therefore only populate workspace/symbol for project
    // files — when the client advertises `workspaceFolders` in initialize.
    // The legacy `rootUri` alone is insufficient for them (clangd tolerates
    // it, pyright does not). Send both so symbol queries resolve the
    // workspace on every backend.
    // (Without this, PyrightBridge.findDefinition never resolves fixture
    //  symbols and the happy-path tests skip after the full readiness wait
    //  even with a language server installed.)
    std::string folderName = "workspace";
    if (!rootUri.empty()) {
        std::string trimmed = rootUri;
        while (!trimmed.empty() && trimmed.back() == '/') trimmed.pop_back();
        auto slash = trimmed.find_last_of('/');
        if (slash != std::string::npos && slash + 1 < trimmed.size()) {
            folderName = trimmed.substr(slash + 1);
        }
    }

    json initParams = {{"processId", nullptr},
                       {"rootUri", rootUri},
                       {"workspaceFolders",
                        rootUri.empty()
                            ? json(nullptr)
                            : json::array({{{"uri", rootUri},
                                            {"name", folderName}}})},
                       {"capabilities", clientCaps}};

    auto response = sendRequest("initialize", initParams);
    // A conforming `initialize` reply carries a JSON object `result` (the
    // InitializeResult, which holds `capabilities`). sendRequest() maps a
    // JSON-RPC `error` reply to nullopt, but a malformed reply with neither
    // `result` nor `error` slips through as an empty json{} — accepting
    // that as a successful handshake leaves the bridge wrongly "available"
    // talking to a server that never initialized. Require a real result
    // object so a malformed/incomplete handshake fails start() cleanly.
    // (Surfaced by the fake-langserver test harness.)
    if (!response || !response->is_object() || response->empty()) {
        std::cerr << logPrefix_ << " initialize failed (no/invalid result)\n";
        stop();
        return false;
    }

    // Store server capabilities for subclass use
    if (response->contains("capabilities")) {
        serverCapabilities_ = (*response)["capabilities"];
    }

    // Send initialized notification
    sendNotification("initialized", json::object());

    available_ = true;
    std::cerr << logPrefix_ << " started successfully\n";
    return true;
}

void LSPBridge::stop() {
    // Atomically clear available_ so we only send shutdown/exit once, but
    // always proceed with thread/process cleanup even if available_ was
    // already false (e.g. child died before initialization completed).
    bool wasAvailable = available_.exchange(false);

    if (wasAvailable) {
        // Use a short, non-retrying timeout for shutdown. clangd (and some
        // other servers) occasionally fail to ack `shutdown` in time because
        // they are already tearing down; the default 5s + 1 retry waste ~10s
        // of wall-clock time per stop() and emit two noisy "request timeout"
        // lines. process_.stop(3000) below will forcibly terminate the
        // child regardless, so a single short wait is sufficient.
        //
        // Set stopping_ AFTER the shutdown request so the request's CV wait
        // loop doesn't short-circuit on the `if (stopping_) return` branch
        // before the server has a chance to respond. stopping_ is set below
        // before joining the reader thread.
        auto response = sendRequest("shutdown", json::object(), shutdownTimeout_, /*retry=*/false);
        if (!response) {
            std::cerr << logPrefix_ << " shutdown unacknowledged, forcing exit\n";
        }
        sendNotification("exit", json::object());
    }

    stopping_ = true;

    process_.stop(3000);

    if (readerThread_.joinable()) {
        responseCv_.notify_all();
        readerThread_.join();
    }

    int fb = fallbackCount_.load();
    if (fb > 0) {
        std::cerr << logPrefix_ << " session summary: " << fb
                  << " request(s) fell back to regex analysis\n";
    }
    fallbackCount_.store(0);

    stopping_ = false;
    if (wasAvailable) {
        std::cerr << logPrefix_ << " stopped\n";
    }
}

bool LSPBridge::isAvailable() const {
    return !lspDisabled_ && available_;
}

bool LSPBridge::hasSemanticTokens() const {
    return serverCapabilities_.contains("semanticTokensProvider");
}

void LSPBridge::setTimeouts(std::chrono::milliseconds firstRequest, std::chrono::milliseconds normal) {
    firstRequestTimeout_ = firstRequest;
    normalTimeout_ = normal;
}

bool LSPBridge::waitForIndex(std::chrono::milliseconds timeout) {
    if (lspDisabled_ || !available_) return false;

    auto savedFirst = firstRequestTimeout_;
    firstRequestTimeout_ = timeout;
    firstRequestDone_ = false;

    json params = {{"query", ""}};
    auto response = sendRequest("workspace/symbol", params);

    firstRequestTimeout_ = savedFirst;

    if (response) return true;

    std::cerr << logPrefix_ << " server did not respond within "
              << timeout.count() << "ms index wait\n";
    return false;
}

// ============================================================================
// Query helpers
// ============================================================================

std::optional<SymbolResult> LSPBridge::queryWorkspaceSymbol(const std::string& name) {
    // Extract simple name (last component after ::)
    std::string query = name;
    auto lastSep = name.rfind("::");
    if (lastSep != std::string::npos) {
        query = name.substr(lastSep + 2);
    }

    json params = {{"query", query}};
    auto response = sendRequest("workspace/symbol", params);
    if (!response || !response->is_array() || response->empty()) {
        return std::nullopt;
    }

    // Defensively extract a SymbolResult. Per the LSP spec a WorkspaceSymbol's
    // location may be just {uri} (range optional), and a non-conformant server
    // may send wrong-typed fields — so check each access instead of blindly
    // calling get<>(), which would throw and crash the bridge.
    auto extract = [this](const json& sym) -> std::optional<SymbolResult> {
        auto locIt = sym.find("location");
        if (locIt == sym.end() || !locIt->is_object()) return std::nullopt;
        auto uriIt = locIt->find("uri");
        if (uriIt == locIt->end() || !uriIt->is_string()) return std::nullopt;
        SymbolResult result;
        result.file = uriToPath(uriIt->get<std::string>());
        result.line = 0;
        result.column = 0;
        if (auto rangeIt = locIt->find("range"); rangeIt != locIt->end() && rangeIt->is_object()) {
            if (auto startIt = rangeIt->find("start"); startIt != rangeIt->end() && startIt->is_object()) {
                if (auto l = startIt->find("line"); l != startIt->end() && l->is_number_integer())
                    result.line = l->get<int>();
                if (auto c = startIt->find("character"); c != startIt->end() && c->is_number_integer())
                    result.column = c->get<int>();
            }
        }
        return result;
    };

    // Find best match: prefer exact qualified name match
    for (const auto& sym : *response) {
        std::string symName = sym.value("name", "");
        std::string containerName = sym.value("containerName", "");
        std::string fullName = containerName.empty() ? symName : containerName + "::" + symName;

        if (fullName == name || symName == query) {
            if (auto r = extract(sym)) return r;
        }
    }

    // Fallback: return first result
    return extract((*response)[0]);
}

// ============================================================================
// JSON-RPC communication
// ============================================================================

std::optional<json> LSPBridge::sendRequest(const std::string& method, const json& params) {
    if (lspDisabled_) return std::nullopt;

    auto timeout = firstRequestDone_ ? normalTimeout_ : firstRequestTimeout_;
    firstRequestDone_ = true;
    return sendRequest(method, params, timeout, /*retry=*/true);
}

std::optional<json> LSPBridge::sendRequest(const std::string& method, const json& params,
                                           std::chrono::milliseconds timeout, bool retry) {
    if (lspDisabled_) return std::nullopt;

    // Attempt count: initial + optional 1 retry on timeout.
    const int maxAttempts = retry ? 2 : 1;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        int id = nextId_++;

        json msg = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};

        if (!writeMessage(msg)) {
            // Write failure means connection loss — avoid retrying.
            std::cerr << logPrefix_ << " server became unresponsive during analysis"
                      << " — remaining files will use regex fallback\n";
            fallbackCount_.fetch_add(1);
            return std::nullopt;
        }

        std::unique_lock<std::mutex> lock(responseMutex_);
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (pendingResponses_.find(id) == pendingResponses_.end()) {
            if (responseCv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                // Timed out. Clean up the pending entry so the reader thread
                // does not deliver a stale response later.
                pendingResponses_.erase(id);

                if (connectionLost_) {
                    // Connection lost — avoid retrying.
                    std::cerr << logPrefix_ << " server became unresponsive during analysis"
                              << " — remaining files will use regex fallback\n";
                    fallbackCount_.fetch_add(1);
                    return std::nullopt;
                }

                if (!retry) {
                    // Non-retrying caller (e.g. stop()): return silently so the
                    // caller can emit its own contextual log line instead of
                    // the generic "request timeout" stderr noise.
                    return std::nullopt;
                }

                if (attempt == 0) {
                    std::cerr << logPrefix_ << " request timeout: " << method
                              << " (retrying once)\n";
                    break; // break inner while to retry
                }

                std::cerr << logPrefix_ << " request timeout: " << method
                          << " (retry exhausted)\n";
                return std::nullopt;
            }
            if (stopping_) return std::nullopt;
            if (connectionLost_) {
                std::cerr << logPrefix_ << " server became unresponsive during analysis"
                          << " — remaining files will use regex fallback\n";
                fallbackCount_.fetch_add(1);
                return std::nullopt;
            }
        }

        auto it = pendingResponses_.find(id);
        if (it == pendingResponses_.end()) {
            // Timeout branch: inner while broke out for retry.
            continue;
        }

        json response = std::move(it->second);
        pendingResponses_.erase(it);

        if (response.contains("error")) {
            std::cerr << logPrefix_ << " error: " << response["error"].dump() << "\n";
            return std::nullopt;
        }

        return response.value("result", json{});
    }

    return std::nullopt;
}

void LSPBridge::sendNotification(const std::string& method, const json& params) {
    json msg = {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
    writeMessage(msg);
}

bool LSPBridge::writeMessage(const json& msg) {
    std::string body = msg.dump();
    std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    std::string data = header + body;
    return process_.write(data.c_str(), data.size());
}

std::optional<json> LSPBridge::readMessage() {
    // Read headers
    int contentLength = -1;

    while (true) {
        std::string line;
        while (true) {
            int ch = process_.readByte();
            if (ch < 0) return std::nullopt;
            if (ch == '\r') {
                int next = process_.readByte();
                if (next == '\n') break;
                line += static_cast<char>(ch);
                if (next >= 0) line += static_cast<char>(next);
            } else {
                line += static_cast<char>(ch);
            }
        }

        if (line.empty()) break; // End of headers

        const std::string prefix = "Content-Length: ";
        if (line.substr(0, prefix.size()) == prefix) {
            // A non-numeric Content-Length must NOT take down the reader
            // thread via an uncaught std::invalid_argument/out_of_range from
            // std::stoi (that std::terminate's the whole process). Treat a
            // malformed header as connection loss instead.
            // (Surfaced by the fake-langserver "bad-length" mode.)
            try {
                contentLength = std::stoi(line.substr(prefix.size()));
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }
    }

    if (contentLength <= 0) return std::nullopt;

    // Read body
    std::string body(contentLength, '\0');
    size_t totalRead = 0;
    while (totalRead < static_cast<size_t>(contentLength)) {
        size_t n = process_.read(&body[totalRead], contentLength - totalRead);
        if (n == 0) return std::nullopt;
        totalRead += n;
    }

    try {
        return json::parse(body);
    } catch (const json::parse_error&) {
        return std::nullopt;
    }
}

void LSPBridge::readerLoop() {
    while (!stopping_) {
        auto msg = readMessage();
        if (!msg) {
            if (!stopping_) {
                std::cerr << logPrefix_ << " connection lost\n";
                available_ = false;
                connectionLost_ = true;
                responseCv_.notify_all();
            }
            break;
        }

        // Only integer ids correspond to our outstanding requests. A
        // malformed response whose `id` is a string/object would throw an
        // uncaught nlohmann::json::type_error from get<int>() and
        // std::terminate the whole process on this reader thread — ignore
        // such frames instead. (Surfaced by the fake-langserver
        // "type-mismatch" mode.)
        if (msg->contains("id") && (*msg)["id"].is_number_integer()) {
            int id = (*msg)["id"].get<int>();
            {
                std::lock_guard<std::mutex> lock(responseMutex_);
                pendingResponses_[id] = std::move(*msg);
            }
            responseCv_.notify_all();
        }
    }
}

// ============================================================================
// Document Analysis API
// ============================================================================

void LSPBridge::parseSemanticTokenLegend() {
    if (serverCapabilities_.contains("semanticTokensProvider")) {
        auto& stp = serverCapabilities_["semanticTokensProvider"];
        if (stp.contains("legend")) {
            auto& legend = stp["legend"];
            if (legend.contains("tokenTypes"))
                tokenTypes_ = legend["tokenTypes"].get<std::vector<std::string>>();
            if (legend.contains("tokenModifiers"))
                tokenModifiers_ = legend["tokenModifiers"].get<std::vector<std::string>>();
        }
    }
}

void LSPBridge::openDocument(const std::string& filePath) {
    if (!isAvailable()) return;

    // Read file content
    std::ifstream ifs(filePath);
    if (!ifs) return;
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    json params;
    params["textDocument"]["uri"] = pathToUri(filePath);
    params["textDocument"]["languageId"] = languageId();
    params["textDocument"]["version"] = 1;
    params["textDocument"]["text"] = content;

    sendNotification("textDocument/didOpen", params);
}

void LSPBridge::closeDocument(const std::string& filePath) {
    if (!isAvailable()) return;

    json params;
    params["textDocument"]["uri"] = pathToUri(filePath);

    sendNotification("textDocument/didClose", params);
}

std::vector<LSPBridge::SemanticToken> LSPBridge::getSemanticTokens(const std::string& filePath) {
    std::vector<SemanticToken> result;
    if (!isAvailable()) return result;

    json params;
    params["textDocument"]["uri"] = pathToUri(filePath);

    auto resp = sendRequest("textDocument/semanticTokens/full", params);
    if (!resp || !resp->contains("data")) return result;

    auto& data = (*resp)["data"];
    if (!data.is_array()) return result;

    int currentLine = 0;
    int currentCol = 0;

    for (size_t i = 0; i + 4 < data.size(); i += 5) {
        int deltaLine = data[i].get<int>();
        int deltaStartChar = data[i + 1].get<int>();
        int length = data[i + 2].get<int>();
        int typeIndex = data[i + 3].get<int>();
        int modifierBits = data[i + 4].get<int>();

        if (deltaLine > 0) {
            currentLine += deltaLine;
            currentCol = deltaStartChar;
        } else {
            currentCol += deltaStartChar;
        }

        SemanticToken token;
        token.line = currentLine;
        token.column = currentCol;
        token.length = length;
        token.type = (typeIndex >= 0 && typeIndex < static_cast<int>(tokenTypes_.size()))
                     ? tokenTypes_[typeIndex] : "unknown";

        // Decode modifier bitmask
        std::string mods;
        for (size_t bit = 0; bit < tokenModifiers_.size(); ++bit) {
            if (modifierBits & (1 << bit)) {
                if (!mods.empty()) mods += ",";
                mods += tokenModifiers_[bit];
            }
        }
        token.modifiers = mods;

        result.push_back(std::move(token));
    }

    return result;
}

bool LSPBridge::hasDocumentSymbol() const {
    return serverCapabilities_.contains("documentSymbolProvider");
}

namespace {

/// Map an LSP SymbolKind integer to our normalized kind string.
/// LSP spec SymbolKind enum: 5=Class, 6=Method, 9=Constructor, 11=Interface,
/// 12=Function, 19=Namespace, 22=Struct.
std::string symbolKindToString(int kind) {
    switch (kind) {
        case 5:  return "class";
        case 6:  return "method";
        case 9:  return "method";      // treat constructor as method for containment
        case 11: return "class";       // interface
        case 12: return "function";
        case 19: return "namespace";
        case 22: return "class";       // struct
        case 23: return "class";       // event
        default: return "other";
    }
}

/// Normalize a symbol name for enclosing-function matching.
/// Strips parameter signatures (jdtls: "read_file(int)" -> "read_file"),
/// leading keywords (rust-analyzer: "fn foo" -> "foo"), and trailing
/// generic/return-type clutter. The goal is a simple identifier that
/// matches how .topo declarations are spelled.
std::string normalizeSymbolName(std::string name) {
    // Drop leading keyword prefixes (e.g. "fn ", "impl ", "struct ").
    auto space = name.find(' ');
    if (space != std::string::npos && space + 1 < name.size()) {
        name = name.substr(space + 1);
    }
    // Strip parameter list — first '(' marks the signature start.
    auto paren = name.find('(');
    if (paren != std::string::npos) {
        name = name.substr(0, paren);
    }
    // Strip trailing whitespace.
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
        name.pop_back();
    }
    return name;
}

/// Convert a hierarchical DocumentSymbol JSON node into DocumentSymbolInfo.
LSPBridge::DocumentSymbolInfo parseHierarchicalSymbol(const json& node) {
    LSPBridge::DocumentSymbolInfo info;
    if (node.contains("name") && node["name"].is_string()) {
        info.name = normalizeSymbolName(node["name"].get<std::string>());
    }
    int kindInt = 0;
    if (node.contains("kind") && node["kind"].is_number_integer()) {
        kindInt = node["kind"].get<int>();
    }
    info.kind = symbolKindToString(kindInt);

    if (node.contains("range") && node["range"].contains("start") && node["range"].contains("end")) {
        info.startLine = node["range"]["start"].value("line", 0);
        info.endLine = node["range"]["end"].value("line", 0);
    }

    if (node.contains("children") && node["children"].is_array()) {
        info.children.reserve(node["children"].size());
        for (const auto& child : node["children"]) {
            info.children.push_back(parseHierarchicalSymbol(child));
        }
    }
    return info;
}

/// Rebuild a hierarchy from a flat SymbolInformation[] response by walking
/// `containerName` links.  Returns a list of top-level symbols (those with
/// no matching container parent).
std::vector<LSPBridge::DocumentSymbolInfo> parseFlatSymbols(const json& arr) {
    struct Raw {
        LSPBridge::DocumentSymbolInfo info;
        std::string container;
    };
    std::vector<Raw> raws;
    raws.reserve(arr.size());

    for (const auto& node : arr) {
        Raw r;
        if (node.contains("name") && node["name"].is_string()) {
            r.info.name = normalizeSymbolName(node["name"].get<std::string>());
        }
        int kindInt = 0;
        if (node.contains("kind") && node["kind"].is_number_integer()) {
            kindInt = node["kind"].get<int>();
        }
        r.info.kind = symbolKindToString(kindInt);

        if (node.contains("location") && node["location"].contains("range")) {
            const auto& range = node["location"]["range"];
            if (range.contains("start")) r.info.startLine = range["start"].value("line", 0);
            if (range.contains("end")) r.info.endLine = range["end"].value("line", 0);
        }
        if (node.contains("containerName") && node["containerName"].is_string()) {
            r.container = node["containerName"].get<std::string>();
        }
        raws.push_back(std::move(r));
    }

    // Index by name so we can find parents by containerName.  Names may be
    // ambiguous in flat responses — if multiple classes share a name we
    // attach children to the first one whose range encloses the child.
    std::vector<LSPBridge::DocumentSymbolInfo> roots;
    auto findParent = [&](const Raw& child) -> LSPBridge::DocumentSymbolInfo* {
        if (child.container.empty()) return nullptr;
        // Walk roots (and children recursively) looking for a symbol whose
        // name matches the container AND whose range contains child.startLine.
        std::function<LSPBridge::DocumentSymbolInfo*(
            std::vector<LSPBridge::DocumentSymbolInfo>&)> walk =
            [&](std::vector<LSPBridge::DocumentSymbolInfo>& list)
                -> LSPBridge::DocumentSymbolInfo* {
            for (auto& s : list) {
                if (s.name == child.container &&
                    s.startLine <= child.info.startLine &&
                    s.endLine >= child.info.endLine) {
                    return &s;
                }
                if (auto* nested = walk(s.children)) return nested;
            }
            return nullptr;
        };
        return walk(roots);
    };

    // First pass: classes / namespaces / structs become roots or children
    // based on containerName lookup.  Second pass: functions/methods attach
    // to their containers.  A single pass in source order usually works
    // because the LSP server emits containers before their members.
    for (auto& r : raws) {
        auto* parent = findParent(r);
        if (parent) {
            parent->children.push_back(std::move(r.info));
        } else {
            roots.push_back(std::move(r.info));
        }
    }
    return roots;
}

} // anonymous namespace

std::vector<LSPBridge::DocumentSymbolInfo> LSPBridge::getDocumentSymbols(
    const std::string& filePath) {
    std::vector<DocumentSymbolInfo> result;
    if (!isAvailable()) return result;

    json params;
    params["textDocument"]["uri"] = pathToUri(filePath);

    auto resp = sendRequest("textDocument/documentSymbol", params);
    if (!resp || !resp->is_array()) return result;

    if (resp->empty()) return result;

    // Detect hierarchical (DocumentSymbol[]) vs flat (SymbolInformation[])
    // by presence of `location` field: flat responses always have `location`
    // whereas hierarchical responses have `range` + optional `children`.
    const auto& first = (*resp)[0];
    if (first.contains("location")) {
        result = parseFlatSymbols(*resp);
    } else {
        result.reserve(resp->size());
        for (const auto& node : *resp) {
            result.push_back(parseHierarchicalSymbol(node));
        }
    }
    return result;
}

namespace {

/// Recursive search for the deepest function/method containing `line`.
/// Accumulates class/namespace ancestors into `pathPrefix` so callers get
/// a fully qualified name back.  Returns empty string if no match.
std::string findEnclosingImpl(
    const std::vector<LSPBridge::DocumentSymbolInfo>& symbols,
    int line,
    const std::string& separator,
    const std::string& pathPrefix) {

    std::string best;
    for (const auto& s : symbols) {
        if (line < s.startLine || line > s.endLine) continue;

        const std::string nextPath = pathPrefix.empty()
                                     ? s.name
                                     : pathPrefix + separator + s.name;

        if (s.kind == "function" || s.kind == "method") {
            best = nextPath;
            // Keep searching children — a nested function is preferred.
            auto nested = findEnclosingImpl(s.children, line, separator, nextPath);
            if (!nested.empty()) best = nested;
        } else {
            // Class / namespace: descend but don't record as an enclosing
            // function itself.
            auto nested = findEnclosingImpl(s.children, line, separator, nextPath);
            if (!nested.empty()) return nested;
        }
    }
    return best;
}

} // anonymous namespace

std::string LSPBridge::findEnclosingFunction(
    const std::vector<DocumentSymbolInfo>& symbols,
    int line,
    const std::string& separator) {
    return findEnclosingImpl(symbols, line, separator, "");
}

std::optional<SymbolResult> LSPBridge::getDefinitionAt(
    const std::string& filePath, int line, int column) {
    if (!isAvailable()) return std::nullopt;

    json params;
    params["textDocument"]["uri"] = pathToUri(filePath);
    params["position"]["line"] = line;        // 0-based
    params["position"]["character"] = column;  // 0-based

    auto resp = sendRequest("textDocument/definition", params);
    if (!resp) return std::nullopt;

    // Response can be a Location, Location[], or LocationLink[]
    json loc;
    if (resp->is_array() && !resp->empty()) {
        loc = (*resp)[0];
    } else if (resp->is_object() && resp->contains("uri")) {
        loc = *resp;
    } else {
        return std::nullopt;
    }

    // Handle LocationLink (has targetUri/targetRange) vs Location (has uri/range)
    std::string uri;
    int defLine = 0, defCol = 0;

    if (loc.contains("targetUri")) {
        uri = loc["targetUri"].get<std::string>();
        if (loc.contains("targetRange")) {
            defLine = loc["targetRange"]["start"]["line"].get<int>();
            defCol = loc["targetRange"]["start"]["character"].get<int>();
        }
    } else if (loc.contains("uri")) {
        uri = loc["uri"].get<std::string>();
        if (loc.contains("range")) {
            defLine = loc["range"]["start"]["line"].get<int>();
            defCol = loc["range"]["start"]["character"].get<int>();
        }
    } else {
        return std::nullopt;
    }

    SymbolResult sr;
    sr.file = uriToPath(uri);
    sr.line = defLine;
    sr.column = defCol;
    return sr;
}

std::optional<std::string> LSPBridge::getHoverAt(
    const std::string& filePath, int line, int column) {
    if (!isAvailable()) return std::nullopt;

    json params;
    params["textDocument"]["uri"] = pathToUri(filePath);
    params["position"]["line"] = line;
    params["position"]["character"] = column;

    auto resp = sendRequest("textDocument/hover", params);
    if (!resp || resp->is_null() || !resp->contains("contents")) {
        if (const char* dbg = std::getenv("TOPO_LSP_HOVER_DEBUG"); dbg && std::string(dbg) == "1") {
            std::cerr << logPrefix_ << " hover miss: "
                      << (resp ? resp->dump() : "null") << "\n";
        }
        return std::nullopt;
    }

    auto& contents = (*resp)["contents"];
    // MarkupContent: { kind: "markdown"|"plaintext", value: "..." }
    if (contents.is_object() && contents.contains("value")) {
        return contents["value"].get<std::string>();
    }
    // String (legacy MarkedString as plain string)
    if (contents.is_string()) {
        return contents.get<std::string>();
    }
    // Deprecated MarkedString[] format — jdtls returns hover as an array of
    // `{ language, value }` objects and/or raw strings. Concatenate all values
    // with newlines so downstream parsers (e.g. extractQualifiedName) still see
    // the signature text they expect.
    if (contents.is_array()) {
        std::string joined;
        for (const auto& entry : contents) {
            if (entry.is_string()) {
                if (!joined.empty()) joined += "\n";
                joined += entry.get<std::string>();
            } else if (entry.is_object() && entry.contains("value")) {
                if (!joined.empty()) joined += "\n";
                joined += entry["value"].get<std::string>();
            }
        }
        if (!joined.empty()) return joined;
    }
    if (const char* dbg = std::getenv("TOPO_LSP_HOVER_DEBUG"); dbg && std::string(dbg) == "1") {
        std::cerr << logPrefix_ << " hover unparsed: " << contents.dump() << "\n";
    }
    return std::nullopt;
}

// ============================================================================
// URI helpers
// ============================================================================

std::string LSPBridge::pathToUri(const std::string& path) {
    // LSP requires absolute URIs. Some servers (notably jdtls) silently ignore
    // relative URIs like "file:///./src/App.java" because they do not match the
    // workspace root. Normalize to an absolute path before encoding.
    namespace fs = std::filesystem;
    fs::path p(path);
    if (!p.is_absolute()) {
        std::error_code ec;
        auto abs = fs::absolute(p, ec);
        if (!ec) p = abs;
    }
    std::string normalized = p.generic_string();
    // Unix: path starts with / → file:// + /path = file:///path
    // Windows: path is C:/path → file:/// + C:/path = file:///C:/path
    if (!normalized.empty() && normalized[0] == '/') {
        return "file://" + normalized;
    }
    return "file:///" + normalized;
}

std::string LSPBridge::uriToPath(const std::string& uri) {
    const std::string prefix = "file://";
    if (uri.size() > prefix.size() && uri.substr(0, prefix.size()) == prefix) {
        // After "file://" the path starts: /path (Unix) or /C:/path (Windows)
        std::string raw = uri.substr(prefix.size());
        // Decode percent-encoded characters
        std::string decoded;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '%' && i + 2 < raw.size()) {
                auto hex = raw.substr(i + 1, 2);
                decoded += static_cast<char>(std::stoi(hex, nullptr, 16));
                i += 2;
            } else {
                decoded += raw[i];
            }
        }
#ifdef _WIN32
        // Windows: /C:/path → C:/path
        if (decoded.size() >= 3 && decoded[0] == '/' &&
            std::isalpha(static_cast<unsigned char>(decoded[1])) && decoded[2] == ':') {
            decoded = decoded.substr(1);
        }
#endif
        return decoded;
    }
    return uri;
}

} // namespace topo::lsp
