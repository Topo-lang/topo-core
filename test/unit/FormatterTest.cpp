// Formatter regression tests.
//
// The headline coverage here is the top-level `debug T { ... }` data-loss
// bug: the formatter's top-level "rest" loop only knew how to pretty-print
// NamespaceDecl, so a DebugDecl fell through and was DELETED from formatted
// output. The fix re-emits any unhandled top-level node verbatim from its
// source span. The fuzzer's idempotency property could not catch this:
// once the node was dropped, `format(format(x)) == format(x)` still held.

#include "topo/Basic/Diagnostic.h"
#include "topo/Format/Formatter.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"

#include <gtest/gtest.h>
#include <string>

using namespace topo;

namespace {

// Lex + parse `source`, then run the formatter. Asserts the parse was
// diagnostic-clean so the test exercises the well-formed-AST contract.
std::string formatSource(const std::string& source) {
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    lexer.setPreserveComments(true);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    EXPECT_TRUE(ast) << "parse returned null for source:\n" << source;
    EXPECT_FALSE(diag.hasErrors()) << "unexpected parse diagnostics for source:\n" << source;
    if (!ast) return {};
    format::Formatter fmt;
    return fmt.format(*ast, source, lexer.comments());
}

} // namespace

// A top-level `debug` declaration must survive formatting — it was being
// silently deleted before the verbatim-fallback fix.
TEST(FormatterDebugDecl, TopLevelDebugDeclSurvivesFormat) {
    const std::string source =
        "debug Particle {\n"
        "    view positions from p[0..4];\n"
        "    summary \"n={count}\";\n"
        "}\n";

    std::string out = formatSource(source);
    // The whole declaration must be present, not dropped.
    EXPECT_NE(out.find("debug Particle"), std::string::npos) << out;
    EXPECT_NE(out.find("view positions from p[0..4];"), std::string::npos) << out;
    EXPECT_NE(out.find("summary \"n={count}\";"), std::string::npos) << out;
}

// A debug decl mixed with content the formatter DOES pretty-print: the
// namespace formats normally and the debug decl is preserved verbatim.
TEST(FormatterDebugDecl, DebugDeclAlongsideNamespacePreserved) {
    const std::string source =
        "namespace app {\n"
        "public:\n"
        "    void run();\n"
        "}\n"
        "\n"
        "debug Frame {\n"
        "    summary \"frame\";\n"
        "}\n";

    std::string out = formatSource(source);
    EXPECT_NE(out.find("namespace app"), std::string::npos) << out;
    EXPECT_NE(out.find("debug Frame"), std::string::npos) << out;
    EXPECT_NE(out.find("summary \"frame\";"), std::string::npos) << out;
}

// Formatting must be idempotent on a debug decl: the second pass equals the
// first. (Before the fix idempotency held only because the node was gone —
// here we assert it holds with the node PRESENT.)
TEST(FormatterDebugDecl, DebugDeclFormatIsIdempotent) {
    const std::string source =
        "debug Particle {\n"
        "    summary \"n={count}\";\n"
        "}\n";

    std::string first = formatSource(source);
    ASSERT_NE(first.find("debug Particle"), std::string::npos) << first;
    std::string second = formatSource(first);
    EXPECT_EQ(first, second);
}
