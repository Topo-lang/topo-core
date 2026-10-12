#include "topo/Basic/Diagnostic.h"
#include "topo/Build/BackendProtocol.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Transform/TokenStreamRewriter.h"
#include <gtest/gtest.h>
using namespace topo;

TEST(ExternalTypeLinkage, ExistingExternalAliasNormalizesAtTypePosition) {
    DiagnosticEngine diag;
    const std::string source = "namespace app { public: extern struct Log { public: void write(); } }";
    Lexer lexer(source, "alias.topo", diag);
    lexer.setLenientMode(true);
    transform::TokenStreamRewriter rewriter(lexer);
    auto tokens = rewriter.rewrite();
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[5].kind, TokenKind::KW_external); EXPECT_EQ(tokens[5].text, "external");
    EXPECT_EQ(tokens[6].kind, TokenKind::KW_type); EXPECT_EQ(tokens[6].text, "type");
    EXPECT_EQ(rewriter.transformRecords().size(), 2u);
    lexer.setTokenBuffer(std::move(tokens));
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile(); ASSERT_NE(ast, nullptr);
    SemanticAnalyzer sema(diag); auto symbols = sema.analyze(*ast);
    EXPECT_FALSE(diag.hasErrors());
    ASSERT_NE(symbols.findClassSymbol("app::Log"), nullptr);
    EXPECT_TRUE(symbols.findFunction("app::Log::write")->isExternal);
}

TEST(ExternalTypeLinkage, StrictAliasStaysUnsupportedAndCanonicalTokensStayUnchanged) {
    DiagnosticEngine diag;
    const std::string aliasSource = "namespace app { public: extern struct Log {} }";
    Lexer alias(aliasSource, "strict.topo", diag);
    transform::TokenStreamRewriter strict(alias); auto tokens = strict.rewrite();
    EXPECT_EQ(tokens[5].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[6].kind, TokenKind::Identifier);
    EXPECT_TRUE(strict.transformRecords().empty());
    const std::string canonicalSource = "namespace app { public: external type Log { public: void write(); } }";
    Lexer canonical(canonicalSource, "canonical.topo", diag);
    canonical.setLenientMode(true);
    transform::TokenStreamRewriter rewriter(canonical); rewriter.rewrite();
    EXPECT_TRUE(rewriter.transformRecords().empty());
}

TEST(ExternalTypeLinkage, TypeScriptBackendRequestRetainsWholeTypeAndMemberProvenance) {
    DiagnosticEngine diag;
    const std::string source = "namespace app { public: external type Log { public: void write(); private: void hidden(); } type Other { public: void write(); } }";
    Lexer lexer(source, "ts.topo", diag);
    Parser parser(lexer, diag); auto ast = parser.parseTopoFile(); ASSERT_NE(ast, nullptr);
    SemanticAnalyzer sema(diag);
    build::BackendRequest original; original.language = HostLanguage::TypeScript;
    original.symbolTable = sema.analyze(*ast); ASSERT_FALSE(diag.hasErrors());
    build::BackendRequest restored;
    ASSERT_TRUE(build::deserializeBackendRequest(build::serializeBackendRequest(original), restored));
    EXPECT_EQ(restored.language, HostLanguage::TypeScript);
    const auto* cls = restored.symbolTable.findClassSymbol("app::Log"); ASSERT_NE(cls, nullptr);
    EXPECT_TRUE(cls->isExternal); EXPECT_TRUE(cls->explicitlyExternal); EXPECT_EQ(cls->externalOwner, "app::Log");
    for (const auto* name : {"app::Log::write", "app::Log::hidden"}) {
        const auto* fn = restored.symbolTable.findFunction(name); ASSERT_NE(fn, nullptr);
        EXPECT_TRUE(fn->isExternal); EXPECT_FALSE(fn->explicitlyExternal);
        EXPECT_EQ(fn->owningType, "app::Log"); EXPECT_EQ(fn->externalOwner, "app::Log");
    }
    EXPECT_FALSE(restored.symbolTable.findFunction("app::Other::write")->isExternal);
}
