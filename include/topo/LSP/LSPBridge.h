#ifndef TOPO_LSP_LSPBRIDGE_H
#define TOPO_LSP_LSPBRIDGE_H

#include "topo/Platform/Process.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace topo::lsp {

using json = nlohmann::json;

/// Unified symbol result for all bridge implementations.
struct SymbolResult {
    std::string file;
    int line = 0;
    int column = 0;
};

// Type aliases for backward compatibility with test code
using ClangdSymbolResult = SymbolResult;
using RustSymbolResult = SymbolResult;

/// Base class for external LSP server bridges (clangd, rust-analyzer, etc.).
/// Provides JSON-RPC communication, reader thread, and URI helpers.
class LSPBridge {
public:
    explicit LSPBridge(std::string logPrefix);
    virtual ~LSPBridge();

    // Prevent copying
    LSPBridge(const LSPBridge&) = delete;
    LSPBridge& operator=(const LSPBridge&) = delete;

    /// Start the bridge for the given workspace root URI.
    /// Concrete bridges resolve their own tool paths internally.
    virtual bool start(const std::string& rootUri) = 0;

    void stop();
    bool isAvailable() const;

    /// Human-readable language name for hover/diagnostic labels (e.g. "C++", "Rust").
    virtual std::string displayName() const { return languageId(); }

    /// Configure request timeouts.
    void setTimeouts(std::chrono::milliseconds firstRequest, std::chrono::milliseconds normal);

    /// Wait for server indexing to complete. Sends a workspace/symbol probe
    /// and waits up to `timeout` for any response.
    bool waitForIndex(std::chrono::milliseconds timeout = std::chrono::milliseconds{15000});

    virtual std::optional<SymbolResult> findDefinition(const std::string& qualifiedName,
                                                       const std::vector<std::string>& sourceFiles) = 0;

    /// Find the host-language type definition for a named type (e.g. from
    /// std::import). Default implementation delegates to findDefinition().
    /// Subclasses may override to scan include/source directories directly.
    virtual std::optional<SymbolResult> findTypeDefinition(const std::string& typeName,
                                                           const std::vector<std::string>& sourceFiles,
                                                           const std::vector<std::string>& /*includeDirs*/) {
        return findDefinition(typeName, sourceFiles);
    }

    virtual std::vector<SymbolResult> findReferences(const std::string& qualifiedName,
                                                     const std::vector<std::string>& sourceFiles) = 0;

    virtual std::optional<std::string> getHoverInfo(const std::string& qualifiedName,
                                                    const std::vector<std::string>& sourceFiles) = 0;

    // --- Document Analysis API (standard LSP protocol) ---

    /// Language identifier for textDocument/didOpen (e.g. "cpp", "rust", "java", "python").
    /// Subclasses must override.
    virtual std::string languageId() const = 0;

    /// Semantic token from textDocument/semanticTokens/full.
    struct SemanticToken {
        int line;       // 0-based
        int column;     // 0-based
        int length;
        std::string type;      // "function", "method", "variable", "type", "macro", etc.
        std::string modifiers; // comma-separated: "declaration", "definition", "readonly", etc.
    };

    /// Normalized view of a textDocument/documentSymbol entry.
    /// Both the hierarchical DocumentSymbol[] and the legacy flat
    /// SymbolInformation[] responses are reduced to this shape by
    /// getDocumentSymbols() — subclasses do not need to distinguish.
    struct DocumentSymbolInfo {
        std::string name;        // simple name: "read_file", "App"
        std::string kind;        // "function" | "method" | "class" | "namespace" | "other"
        int startLine = 0;       // 0-based inclusive
        int endLine = 0;         // 0-based inclusive
        std::vector<DocumentSymbolInfo> children;
    };

    /// Open a document for analysis. Sends textDocument/didOpen with file content.
    void openDocument(const std::string& filePath);

    /// Close a previously opened document.
    void closeDocument(const std::string& filePath);

    /// Whether the server supports textDocument/semanticTokens.
    bool hasSemanticTokens() const;

    /// Whether the server supports textDocument/documentSymbol.
    bool hasDocumentSymbol() const;

    /// Get all semantic tokens for an opened document.
    std::vector<SemanticToken> getSemanticTokens(const std::string& filePath);

    /// Get the outline (documentSymbol tree) for an opened document.
    /// Returns an empty vector if the server does not implement the
    /// protocol or the request fails.  Both hierarchical DocumentSymbol[]
    /// and flat SymbolInformation[] responses are normalized into the
    /// DocumentSymbolInfo tree — for flat responses the implementation
    /// reconstructs parent/child links from the legacy `containerName`
    /// field so callers can walk the tree uniformly.
    std::vector<DocumentSymbolInfo> getDocumentSymbols(const std::string& filePath);

    /// Walk a DocumentSymbolInfo tree and return the qualified name of
    /// the deepest function/method whose range contains `line` (0-based).
    /// Class/namespace ancestors are joined with `separator` ("::" for
    /// C++/Rust/Topo canonical form, "." for Java/Python display).  Returns
    /// an empty string if no enclosing function is found.
    static std::string findEnclosingFunction(
        const std::vector<DocumentSymbolInfo>& symbols,
        int line,
        const std::string& separator = "::");

    /// Resolve definition at a position. Line and column are 0-based.
    std::optional<SymbolResult> getDefinitionAt(const std::string& filePath, int line, int column);

    /// Get hover info at a position. Line and column are 0-based.
    std::optional<std::string> getHoverAt(const std::string& filePath, int line, int column);

protected:
    bool startProcess(const std::string& exe, const std::vector<std::string>& args, const std::string& rootUri);

    std::optional<json> sendRequest(const std::string& method, const json& params);

    /// Low-level request variant that accepts a caller-provided timeout and
    /// a retry flag. Used by the shutdown path to apply a short, non-retrying
    /// timeout without inheriting the default 5s + 1 retry behavior that
    /// produces ~10s of wall-clock waste when the server is slow to ack
    /// `shutdown`. Existing callers continue to use the 2-arg overload which
    /// preserves the retry=true + firstRequest/normal timeout semantics.
    std::optional<json> sendRequest(const std::string& method, const json& params,
                                    std::chrono::milliseconds timeout, bool retry);

    void sendNotification(const std::string& method, const json& params);

    std::optional<SymbolResult> queryWorkspaceSymbol(const std::string& name);

    static std::string pathToUri(const std::string& path);
    static std::string uriToPath(const std::string& uri);

    std::string logPrefix_;
    std::atomic<bool> available_{false};
    std::atomic<bool> stopping_{false};

    /// When true, all LSP operations return empty/nullopt immediately.
    /// Set via environment variable TOPO_CHECK_NO_LSP=1.
    bool lspDisabled_ = false;

    /// Timeout for the first LSP request (server indexing). Default: 30 seconds.
    std::chrono::milliseconds firstRequestTimeout_{30000};

    /// Timeout for subsequent LSP requests. Default: 5 seconds.
    std::chrono::milliseconds normalTimeout_{5000};

    /// Timeout for the LSP `shutdown` request issued by stop(). Kept short
    /// and non-retrying because the subsequent `process_.stop()` forcibly
    /// terminates the child — waiting 5s+5s on a slow/unresponsive shutdown
    /// ack is pure wall-clock waste. Default: 1 second.
    std::chrono::milliseconds shutdownTimeout_{1000};

    /// Whether the first request has been made.
    bool firstRequestDone_ = false;

    /// Server capabilities from the initialize response.
    /// Subclasses can read this after startProcess() returns true.
    json serverCapabilities_ = json::object();

    /// Parse semantic token legend from serverCapabilities_. Call from subclass start() after startProcess().
    void parseSemanticTokenLegend();

    /// Token type and modifier legends from server capabilities.
    std::vector<std::string> tokenTypes_;
    std::vector<std::string> tokenModifiers_;

private:
    bool writeMessage(const json& msg);
    std::optional<json> readMessage();
    void readerLoop();

    std::atomic<int> fallbackCount_{0};
    std::atomic<bool> connectionLost_{false};

    int nextId_ = 1;
    std::thread readerThread_;
    std::mutex responseMutex_;
    std::condition_variable responseCv_;
    std::unordered_map<int, json> pendingResponses_;

    topo::platform::PipedProcess process_;
};

} // namespace topo::lsp

#endif // TOPO_LSP_LSPBRIDGE_H
