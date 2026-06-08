// LSPBridge robustness unit coverage.
//
// LSPBridge fronts clangd / rust-analyzer / jdtls / pyright and reads
// percent-escaped URIs and server-supplied capability JSON — all untrusted.
// These tests pin the hardening that keeps a malformed URI, a wrong-typed
// semantic-token legend, or a wide modifier bitmask from invoking UB or
// throwing out of the (barrier-less) bridge:
//
//   - uriToPath must not throw std::invalid_argument on a non-hex %XX escape.
//   - pathToUri must percent-encode so it round-trips through uriToPath
//     (paths with '%', spaces, reserved bytes keep their identity).
//   - parseSemanticTokenLegend must not throw on a non-string-array legend.
//
// LSPBridge is abstract and pathToUri/uriToPath/parseSemanticTokenLegend are
// protected; a minimal concrete subclass exposes them for direct testing
// without spawning a real language server.

#include "topo/LSP/LSPBridge.h"

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

namespace {

using topo::lsp::LSPBridge;
using topo::lsp::SymbolResult;
using json = nlohmann::json;

// Concrete bridge that wires up the pure-virtual surface with no-op
// implementations (we never start a process) and re-exposes the protected
// static URI helpers + legend parser the production handlers rely on.
class TestBridge final : public LSPBridge {
public:
    TestBridge() : LSPBridge("[test]") {}

    bool start(const std::string&) override { return false; }
    std::optional<SymbolResult> findDefinition(
        const std::string&, const std::vector<std::string>&) override {
        return std::nullopt;
    }
    std::vector<SymbolResult> findReferences(
        const std::string&, const std::vector<std::string>&) override {
        return {};
    }
    std::optional<std::string> getHoverInfo(
        const std::string&, const std::vector<std::string>&) override {
        return std::nullopt;
    }
    std::string languageId() const override { return "test"; }

    static std::string callPathToUri(const std::string& p) { return pathToUri(p); }
    static std::string callUriToPath(const std::string& u) { return uriToPath(u); }

    // Inject a capabilities document and run the (protected) legend parser.
    void setCapabilities(json caps) { serverCapabilities_ = std::move(caps); }
    void runParseLegend() { parseSemanticTokenLegend(); }
    const std::vector<std::string>& tokenTypes() const { return tokenTypes_; }
    const std::vector<std::string>& tokenModifiers() const {
        return tokenModifiers_;
    }
};

// Regression: a non-hex %XX escape (e.g. "%ZZ" / "%g1") previously reached a
// bare std::stoi, which throws std::invalid_argument and would unwind out of
// every handler that calls uriToPath. The decode must now leave a malformed
// escape as the literal '%' and never throw.
TEST(LSPBridgeUriTest, UriToPathToleratesBadHexEscape) {
    struct Case { const char* uri; const char* expected; };
    const Case cases[] = {
        // Non-hex escape chars: pass through verbatim (literal '%').
        {"file:///tmp/%ZZbad", "/tmp/%ZZbad"},
        {"file:///tmp/%g1", "/tmp/%g1"},
        // Truncated escape at end of string: nothing to decode.
        {"file:///tmp/%", "/tmp/%"},
        {"file:///tmp/%4", "/tmp/%4"},
        // A valid escape still decodes ('%20' -> space).
        {"file:///tmp/a%20b", "/tmp/a b"},
        // Mixed valid + invalid in one URI.
        {"file:///t%20%ZZ", "/t %ZZ"},
    };
    for (const auto& c : cases) {
        std::string out;
        ASSERT_NO_THROW(out = TestBridge::callUriToPath(c.uri))
            << "uri=" << c.uri;
        EXPECT_EQ(out, c.expected) << "uri=" << c.uri;
    }
}

// Regression: pathToUri previously did no percent-encoding, so it was NOT the
// inverse of uriToPath — a path containing '%' / space / reserved byte got a
// different identity on round-trip (and a bare '%' produced a malformed escape
// uriToPath then had to defend against). pathToUri must now encode so the
// pair round-trips.
TEST(LSPBridgeUriTest, PathToUriPercentEncodesAndRoundTrips) {
    // Inputs must be absolute *per platform*: on Windows pathToUri absolutizes a
    // path without a drive (prepending the current drive), which would break the
    // round-trip. Feed genuinely-absolute paths so the test exercises percent
    // encoding + round-trip, not absolutization.
#ifdef _WIN32
    const char* absPaths[] = {
        "C:/tmp/has space/App.java",
        "C:/tmp/has%percent/x.cpp",
        "C:/tmp/weird#?&=name.rs",
        "C:/tmp/plain/file.py",
    };
#else
    const char* absPaths[] = {
        "/tmp/has space/App.java",
        "/tmp/has%percent/x.cpp",
        "/tmp/weird#?&=name.rs",
        "/tmp/plain/file.py",
    };
#endif
    for (const char* p : absPaths) {
        std::string uri;
        ASSERT_NO_THROW(uri = TestBridge::callPathToUri(p)) << "path=" << p;
        // Reserved/space bytes must be percent-encoded (no literal space).
        EXPECT_EQ(uri.find(' '), std::string::npos)
            << "uri should not contain a raw space: " << uri;
        // The pair must round-trip back to the original absolute path.
        std::string back = TestBridge::callUriToPath(uri);
        EXPECT_EQ(back, std::string(p)) << "round-trip failed for " << p;
    }
}

// '/' and ':' (Windows drive colon) stay literal so the URI keeps its path
// structure; only out-of-set bytes get encoded.
TEST(LSPBridgeUriTest, PathToUriKeepsSeparatorsLiteral) {
#ifdef _WIN32
    // Drive-absolute on Windows so pathToUri does not re-absolutize.
    std::string uri = TestBridge::callPathToUri("C:/a/b/c.txt");
    EXPECT_EQ(uri, "file:///C:/a/b/c.txt");
#else
    std::string uri = TestBridge::callPathToUri("/a/b/c.txt");
    EXPECT_EQ(uri, "file:///a/b/c.txt");
#endif
}

// Regression: parseSemanticTokenLegend called get<std::vector<std::string>>()
// guarded only by contains(); a non-array or non-string-array legend threw an
// uncaught json::type_error at bridge startup. It must now degrade to an
// empty/partial list without throwing.
TEST(LSPBridgeLegendTest, MalformedLegendDoesNotThrow) {
    // tokenTypes is a string instead of an array; tokenModifiers mixes a
    // non-string element into the array.
    json caps;
    caps["semanticTokensProvider"]["legend"]["tokenTypes"] = "not-an-array";
    caps["semanticTokensProvider"]["legend"]["tokenModifiers"] =
        json::array({"declaration", 42, "static"});

    TestBridge b;
    b.setCapabilities(caps);
    ASSERT_NO_THROW(b.runParseLegend());
    // Non-array tokenTypes → empty list (skipped, not crashed).
    EXPECT_TRUE(b.tokenTypes().empty());
    // Mixed-array tokenModifiers → only the string elements survive.
    ASSERT_EQ(b.tokenModifiers().size(), 2u);
    EXPECT_EQ(b.tokenModifiers()[0], "declaration");
    EXPECT_EQ(b.tokenModifiers()[1], "static");
}

// A well-formed string-array legend is parsed verbatim.
TEST(LSPBridgeLegendTest, WellFormedLegendIsParsed) {
    json caps;
    caps["semanticTokensProvider"]["legend"]["tokenTypes"] =
        json::array({"namespace", "type", "function"});
    caps["semanticTokensProvider"]["legend"]["tokenModifiers"] =
        json::array({"declaration", "readonly"});

    TestBridge b;
    b.setCapabilities(caps);
    ASSERT_NO_THROW(b.runParseLegend());
    ASSERT_EQ(b.tokenTypes().size(), 3u);
    EXPECT_EQ(b.tokenTypes()[1], "type");
    ASSERT_EQ(b.tokenModifiers().size(), 2u);
    EXPECT_EQ(b.tokenModifiers()[1], "readonly");
}

} // namespace
