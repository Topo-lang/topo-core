#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Transform/SemanticVerifier.h"
#include "topo/Transform/TokenStreamRewriter.h"
#include "topo/Transform/TransformRecord.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace topo;

static std::vector<Token> lexAllLenient(const std::string& /*source*/, DiagnosticEngine& /*diag*/, Lexer& lexer) {
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.nextToken();
        tokens.push_back(tok);
        if (tok.is(TokenKind::Eof)) break;
    }
    return tokens;
}

// --- Test 1: Lenient lexer maps alias keywords to canonical TokenKinds ---

TEST(LenientLexer, KeywordAliases) {
    DiagnosticEngine diag;
    std::string source = "struct trait impl module pub def func unique";
    Lexer lexer(source, "<test>", diag);
    lexer.setLenientMode(true);

    auto tokens = lexAllLenient(source, diag, lexer);
    ASSERT_FALSE(diag.hasErrors());
    // 8 aliases + Eof
    ASSERT_EQ(tokens.size(), 9u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_type);
    EXPECT_EQ(tokens[0].text, "struct");
    EXPECT_EQ(tokens[1].kind, TokenKind::KW_constraint);
    EXPECT_EQ(tokens[1].text, "trait");
    EXPECT_EQ(tokens[2].kind, TokenKind::KW_adapt);
    EXPECT_EQ(tokens[2].text, "impl");
    EXPECT_EQ(tokens[3].kind, TokenKind::KW_namespace);
    EXPECT_EQ(tokens[3].text, "module");
    EXPECT_EQ(tokens[4].kind, TokenKind::KW_public);
    EXPECT_EQ(tokens[4].text, "pub");
    EXPECT_EQ(tokens[5].kind, TokenKind::KW_fn);
    EXPECT_EQ(tokens[5].text, "def");
    EXPECT_EQ(tokens[6].kind, TokenKind::KW_fn);
    EXPECT_EQ(tokens[6].text, "func");
    EXPECT_EQ(tokens[7].kind, TokenKind::KW_owned);
    EXPECT_EQ(tokens[7].text, "unique");
}

// --- Test 2: Transform records are collected ---

TEST(LenientLexer, TransformRecords) {
    DiagnosticEngine diag;
    std::string source = "struct Widget trait Drawable";
    Lexer lexer(source, "<test>", diag);
    lexer.setLenientMode(true);

    auto tokens = lexAllLenient(source, diag, lexer);
    ASSERT_FALSE(diag.hasErrors());

    const auto& records = lexer.transformRecords();
    ASSERT_EQ(records.size(), 2u);

    // First record: struct -> type
    EXPECT_EQ(records[0].originalText, "struct");
    EXPECT_EQ(records[0].canonicalText, "type");
    EXPECT_EQ(records[0].kind, transform::TransformRecord::KeywordAlias);
    EXPECT_EQ(records[0].location.line, 1);
    EXPECT_EQ(records[0].location.column, 1);

    // Second record: trait -> constraint
    EXPECT_EQ(records[1].originalText, "trait");
    EXPECT_EQ(records[1].canonicalText, "constraint");
    EXPECT_EQ(records[1].kind, transform::TransformRecord::KeywordAlias);
    EXPECT_EQ(records[1].location.line, 1);
    EXPECT_EQ(records[1].location.column, 15);
}

// --- Test 3: No records when input is already canonical ---

TEST(LenientLexer, NoChangesWhenCanonical) {
    DiagnosticEngine diag;
    std::string source = "namespace type constraint adapt fn public owned";
    Lexer lexer(source, "<test>", diag);
    lexer.setLenientMode(true);

    auto tokens = lexAllLenient(source, diag, lexer);
    ASSERT_FALSE(diag.hasErrors());

    // All should be recognized as standard keywords, no transforms
    EXPECT_TRUE(lexer.transformRecords().empty());

    EXPECT_EQ(tokens[0].kind, TokenKind::KW_namespace);
    EXPECT_EQ(tokens[1].kind, TokenKind::KW_type);
    EXPECT_EQ(tokens[2].kind, TokenKind::KW_constraint);
    EXPECT_EQ(tokens[3].kind, TokenKind::KW_adapt);
    EXPECT_EQ(tokens[4].kind, TokenKind::KW_fn);
    EXPECT_EQ(tokens[5].kind, TokenKind::KW_public);
    EXPECT_EQ(tokens[6].kind, TokenKind::KW_owned);
}

// --- Test 4: Strict mode ignores aliases (treats them as identifiers) ---

TEST(StrictLexer, IgnoresAliases) {
    DiagnosticEngine diag;
    std::string source = "struct trait impl module pub def func";
    Lexer lexer(source, "<test>", diag);
    // lenientMode is false by default

    auto tokens = lexAllLenient(source, diag, lexer);
    ASSERT_FALSE(diag.hasErrors());
    // 7 identifiers + Eof
    ASSERT_EQ(tokens.size(), 8u);
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(tokens[i].kind, TokenKind::Identifier)
            << "Token " << i << " ('" << tokens[i].text << "') should be Identifier in strict mode";
    }
    EXPECT_TRUE(lexer.transformRecords().empty());
}

// --- Test 5: All alias variants covered ---

TEST(LenientLexer, AllAliasVariants) {
    DiagnosticEngine diag;
    std::string source =
        "struct "                   // -> type (record is no longer an
                                    //   alias: it is a stdlib type keyword)
        "trait interface protocol " // -> constraint
        "impl implement "           // -> adapt
        "module package mod "       // -> namespace
        "pub export "               // -> public
        "unique own "               // -> owned
        "extern "                   // -> external
        "def func function";        // -> fn
    Lexer lexer(source, "<test>", diag);
    lexer.setLenientMode(true);

    auto tokens = lexAllLenient(source, diag, lexer);
    ASSERT_FALSE(diag.hasErrors());

    // 17 aliases + Eof
    ASSERT_EQ(tokens.size(), 18u);

    // type alias
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_type);
    // constraint aliases
    EXPECT_EQ(tokens[1].kind, TokenKind::KW_constraint);
    EXPECT_EQ(tokens[2].kind, TokenKind::KW_constraint);
    EXPECT_EQ(tokens[3].kind, TokenKind::KW_constraint);
    // adapt aliases
    EXPECT_EQ(tokens[4].kind, TokenKind::KW_adapt);
    EXPECT_EQ(tokens[5].kind, TokenKind::KW_adapt);
    // namespace aliases
    EXPECT_EQ(tokens[6].kind, TokenKind::KW_namespace);
    EXPECT_EQ(tokens[7].kind, TokenKind::KW_namespace);
    EXPECT_EQ(tokens[8].kind, TokenKind::KW_namespace);
    // public aliases
    EXPECT_EQ(tokens[9].kind, TokenKind::KW_public);
    EXPECT_EQ(tokens[10].kind, TokenKind::KW_public);
    // owned aliases
    EXPECT_EQ(tokens[11].kind, TokenKind::KW_owned);
    EXPECT_EQ(tokens[12].kind, TokenKind::KW_owned);
    // external alias
    EXPECT_EQ(tokens[13].kind, TokenKind::KW_external);
    EXPECT_EQ(tokens[13].text, "extern");
    // fn aliases
    EXPECT_EQ(tokens[14].kind, TokenKind::KW_fn);
    EXPECT_EQ(tokens[15].kind, TokenKind::KW_fn);
    EXPECT_EQ(tokens[16].kind, TokenKind::KW_fn);

    // All should have transform records
    EXPECT_EQ(lexer.transformRecords().size(), 17u);
}

// --- Test 6: class keyword is NOT an alias (handled by Parser deprecation) ---

TEST(LenientLexer, ClassNotInAliasMap) {
    DiagnosticEngine diag;
    std::string source = "class";
    Lexer lexer(source, "<test>", diag);
    lexer.setLenientMode(true);

    auto tokens = lexAllLenient(source, diag, lexer);
    ASSERT_FALSE(diag.hasErrors());
    // class is in the standard keywords map, not in aliases
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_class);
    EXPECT_TRUE(lexer.transformRecords().empty());
}

// --- TokenStreamRewriter tests ---

TEST(TokenStreamRewriter, StageParens) {
    std::string source = "stage(1) init()";
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    lexer.setLenientMode(true);

    topo::transform::TokenStreamRewriter rewriter(lexer);
    auto tokens = rewriter.rewrite();

    // Should produce: KW_stage LAngle IntegerLiteral("1") RAngle Identifier("init") LParen RParen Eof
    ASSERT_GE(tokens.size(), 8u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_stage);
    EXPECT_EQ(tokens[1].kind, TokenKind::LAngle);
    EXPECT_EQ(tokens[2].kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tokens[2].text, "1");
    EXPECT_EQ(tokens[3].kind, TokenKind::RAngle);

    const auto& records = rewriter.transformRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].originalText, "stage(1)");
    EXPECT_EQ(records[0].canonicalText, "stage<1>");
    EXPECT_EQ(records[0].kind, transform::TransformRecord::StructuralRewrite);
}

TEST(TokenStreamRewriter, AtStageParens) {
    std::string source = "@stage(2) run()";
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    lexer.setLenientMode(true);

    topo::transform::TokenStreamRewriter rewriter(lexer);
    auto tokens = rewriter.rewrite();

    ASSERT_GE(tokens.size(), 8u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_stage);
    EXPECT_EQ(tokens[1].kind, TokenKind::LAngle);
    EXPECT_EQ(tokens[2].text, "2");
    EXPECT_EQ(tokens[3].kind, TokenKind::RAngle);

    ASSERT_EQ(rewriter.transformRecords().size(), 1u);
    EXPECT_EQ(rewriter.transformRecords()[0].originalText, "@stage(2)");
}

TEST(TokenStreamRewriter, HashBracketStage) {
    std::string source = "#[stage(3)] process()";
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    lexer.setLenientMode(true);

    topo::transform::TokenStreamRewriter rewriter(lexer);
    auto tokens = rewriter.rewrite();

    ASSERT_GE(tokens.size(), 8u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_stage);
    EXPECT_EQ(tokens[1].kind, TokenKind::LAngle);
    EXPECT_EQ(tokens[2].text, "3");
    EXPECT_EQ(tokens[3].kind, TokenKind::RAngle);

    ASSERT_EQ(rewriter.transformRecords().size(), 1u);
    EXPECT_EQ(rewriter.transformRecords()[0].originalText, "#[stage(3)]");
}

TEST(TokenStreamRewriter, AtVisibility) {
    std::string source = "@public";
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    lexer.setLenientMode(true);

    topo::transform::TokenStreamRewriter rewriter(lexer);
    auto tokens = rewriter.rewrite();

    // Should produce: KW_public Colon Eof
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_public);
    EXPECT_EQ(tokens[1].kind, TokenKind::Colon);

    ASSERT_EQ(rewriter.transformRecords().size(), 1u);
    EXPECT_EQ(rewriter.transformRecords()[0].originalText, "@public");
    EXPECT_EQ(rewriter.transformRecords()[0].canonicalText, "public:");
}

TEST(TokenStreamRewriter, AtOwned) {
    std::string source = "@owned";
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    lexer.setLenientMode(true);

    topo::transform::TokenStreamRewriter rewriter(lexer);
    auto tokens = rewriter.rewrite();

    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_owned);

    ASSERT_EQ(rewriter.transformRecords().size(), 1u);
    EXPECT_EQ(rewriter.transformRecords()[0].originalText, "@owned");
    EXPECT_EQ(rewriter.transformRecords()[0].canonicalText, "owned");
}

TEST(TokenStreamRewriter, NoRewriteStrict) {
    std::string source = "@stage(1) init()";
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    // Strict mode (default) — @ is an error token, no structural rewriting

    topo::transform::TokenStreamRewriter rewriter(lexer);
    auto tokens = rewriter.rewrite();

    EXPECT_TRUE(rewriter.transformRecords().empty());
}

TEST(TokenStreamRewriter, KeywordAliasText) {
    // The Lexer maps alias text to canonical TokenKind but preserves original text.
    // rewriteKeywordAliasText() should fix the text to canonical form.
    std::string source = "namespace test { public: extern void loadConfig(); }";
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    lexer.setLenientMode(true);

    topo::transform::TokenStreamRewriter rewriter(lexer);
    auto tokens = rewriter.rewrite();

    // Find the token that was "extern" — should now have text "external"
    bool foundExternal = false;
    for (const auto& tok : tokens) {
        if (tok.kind == TokenKind::KW_external) {
            EXPECT_EQ(tok.text, "external");
            foundExternal = true;
        }
    }
    EXPECT_TRUE(foundExternal) << "Expected KW_external token in output";

    // Should have a rewriter record for extern -> external
    const auto& records = rewriter.transformRecords();
    bool foundRecord = false;
    for (const auto& rec : records) {
        if (rec.originalText == "extern" && rec.canonicalText == "external") {
            EXPECT_EQ(rec.kind, transform::TransformRecord::KeywordAlias);
            foundRecord = true;
        }
    }
    EXPECT_TRUE(foundRecord) << "Expected transform record for extern -> external";
}

TEST(TokenStreamRewriter, KeywordAliasTextAllAliases) {
    // Verify rewriteKeywordAliasText covers all alias families
    std::string source = "struct trait impl module pub unique def extern";
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    lexer.setLenientMode(true);

    topo::transform::TokenStreamRewriter rewriter(lexer);
    auto tokens = rewriter.rewrite();

    // All alias tokens should have canonical text after rewrite
    // struct->type, trait->constraint, impl->adapt, module->namespace,
    // pub->public, unique->owned, def->fn, extern->external
    ASSERT_GE(tokens.size(), 9u); // 8 tokens + Eof
    EXPECT_EQ(tokens[0].text, "type");
    EXPECT_EQ(tokens[1].text, "constraint");
    EXPECT_EQ(tokens[2].text, "adapt");
    EXPECT_EQ(tokens[3].text, "namespace");
    EXPECT_EQ(tokens[4].text, "public");
    EXPECT_EQ(tokens[5].text, "owned");
    EXPECT_EQ(tokens[6].text, "fn");
    EXPECT_EQ(tokens[7].text, "external");
}

TEST(TokenStreamRewriter, KeywordAliasTextNoChangeWhenCanonical) {
    // Canonical keywords should not produce rewriter records
    std::string source = "type constraint adapt namespace public owned fn external";
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    lexer.setLenientMode(true);

    topo::transform::TokenStreamRewriter rewriter(lexer);
    auto tokens = rewriter.rewrite();

    EXPECT_TRUE(rewriter.transformRecords().empty())
        << "Canonical keywords should not produce rewriter transform records";
}

// --- SemanticVerifier tests ---

static SymbolTable analyzeSource(const std::string& source) {
    DiagnosticEngine diag;
    Lexer lexer(source, "test.topo", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    SemanticAnalyzer sema(diag);
    return sema.analyze(static_cast<const TopoFile&>(*ast));
}

TEST(SemanticVerifier, IdenticalTablesPass) {
    std::string source = R"(
namespace app {
  public:
    void init();
    void process();
}
)";
    auto sym1 = analyzeSource(source);
    auto sym2 = analyzeSource(source);

    transform::SemanticVerifier verifier;
    auto result = verifier.verify(sym1, sym2);
    EXPECT_TRUE(result.equivalent);
    EXPECT_TRUE(result.differences.empty());
}

TEST(SemanticVerifier, MissingFunctionDetected) {
    std::string sourceA = R"(
namespace app {
  public:
    void init();
    void process();
}
)";
    std::string sourceB = R"(
namespace app {
  public:
    void init();
}
)";
    auto symA = analyzeSource(sourceA);
    auto symB = analyzeSource(sourceB);

    transform::SemanticVerifier verifier;
    auto result = verifier.verify(symA, symB);
    EXPECT_FALSE(result.equivalent);
    EXPECT_FALSE(result.differences.empty());
}

TEST(SemanticVerifier, ExtraFunctionDetected) {
    std::string sourceA = R"(
namespace app {
  public:
    void init();
}
)";
    std::string sourceB = R"(
namespace app {
  public:
    void init();
    void cleanup();
}
)";
    auto symA = analyzeSource(sourceA);
    auto symB = analyzeSource(sourceB);

    transform::SemanticVerifier verifier;
    auto result = verifier.verify(symA, symB);
    EXPECT_FALSE(result.equivalent);
    // Should mention the unexpected function
    bool foundUnexpected = false;
    for (const auto& d : result.differences) {
        if (d.find("unexpected") != std::string::npos && d.find("cleanup") != std::string::npos) {
            foundUnexpected = true;
        }
    }
    EXPECT_TRUE(foundUnexpected);
}

TEST(SemanticVerifier, ReturnTypeMismatchDetected) {
    std::string sourceA = R"(
namespace app {
  public:
    Int compute();
}
)";
    std::string sourceB = R"(
namespace app {
  public:
    String compute();
}
)";
    auto symA = analyzeSource(sourceA);
    auto symB = analyzeSource(sourceB);

    transform::SemanticVerifier verifier;
    auto result = verifier.verify(symA, symB);
    EXPECT_FALSE(result.equivalent);
    bool foundReturnMismatch = false;
    for (const auto& d : result.differences) {
        if (d.find("return type") != std::string::npos) {
            foundReturnMismatch = true;
        }
    }
    EXPECT_TRUE(foundReturnMismatch);
}

TEST(SemanticVerifier, EmptyTablesEquivalent) {
    SymbolTable a, b;
    transform::SemanticVerifier verifier;
    auto result = verifier.verify(a, b);
    EXPECT_TRUE(result.equivalent);
    EXPECT_TRUE(result.differences.empty());
}
