#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace topo;

static std::vector<Token> lexAll(const std::string& source, DiagnosticEngine& diag) {
    Lexer lexer(source, "<test>", diag);
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.nextToken();
        tokens.push_back(tok);
        if (tok.is(TokenKind::Eof)) break;
    }
    return tokens;
}

// --- Keyword tests ---

TEST(Lexer, Keywords) {
    DiagnosticEngine diag;
    // Keywords (int/bool/std are now identifiers)
    auto tokens = lexAll(
        "namespace using import public protected private internal fn "
        "stage const void true false return",
        diag);
    ASSERT_FALSE(diag.hasErrors());
    // 14 keywords + Eof
    ASSERT_EQ(tokens.size(), 15u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_namespace);
    EXPECT_EQ(tokens[1].kind, TokenKind::KW_using);
    EXPECT_EQ(tokens[2].kind, TokenKind::KW_import);
    EXPECT_EQ(tokens[3].kind, TokenKind::KW_public);
    EXPECT_EQ(tokens[4].kind, TokenKind::KW_protected);
    EXPECT_EQ(tokens[5].kind, TokenKind::KW_private);
    EXPECT_EQ(tokens[6].kind, TokenKind::KW_internal);
    EXPECT_EQ(tokens[7].kind, TokenKind::KW_fn);
    EXPECT_EQ(tokens[8].kind, TokenKind::KW_stage);
    EXPECT_EQ(tokens[9].kind, TokenKind::KW_const);
    EXPECT_EQ(tokens[10].kind, TokenKind::KW_void);
    EXPECT_EQ(tokens[11].kind, TokenKind::KW_true);
    EXPECT_EQ(tokens[12].kind, TokenKind::KW_false);
    EXPECT_EQ(tokens[13].kind, TokenKind::KW_return);
    EXPECT_EQ(tokens[14].kind, TokenKind::Eof);
}

// `bool` joined the stdlib keyword set; `int` and `std`
// remain identifiers (no stdlib type uses those keywords).
TEST(Lexer, IntAndStdAreIdentifiers) {
    DiagnosticEngine diag;
    auto tokens = lexAll("int std", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 3u); // 2 identifiers + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "int");
    EXPECT_EQ(tokens[1].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[1].text, "std");
}

TEST(Lexer, StdlibTypeKeywords) {
    DiagnosticEngine diag;
    auto tokens = lexAll("bool i64 f64 string optional slice", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 7u); // 6 keywords + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_bool);
    EXPECT_EQ(tokens[1].kind, TokenKind::KW_i64);
    EXPECT_EQ(tokens[2].kind, TokenKind::KW_f64);
    EXPECT_EQ(tokens[3].kind, TokenKind::KW_string);
    EXPECT_EQ(tokens[4].kind, TokenKind::KW_optional);
    EXPECT_EQ(tokens[5].kind, TokenKind::KW_slice);
}

TEST(Lexer, RecordKeyword) {
    DiagnosticEngine diag;
    auto tokens = lexAll("record", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 2u); // keyword + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_record);
}

TEST(Lexer, HandlerFlowKeywords) {
    DiagnosticEngine diag;
    auto tokens = lexAll("handler flow", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 3u); // handler + flow + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_handler);
    EXPECT_EQ(tokens[1].kind, TokenKind::KW_flow);
}

// --- Priority keyword test ---

TEST(Lexer, PriorityKeyword) {
    DiagnosticEngine diag;
    auto tokens = lexAll("priority", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 2u); // 1 keyword + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::KW_priority);
    EXPECT_EQ(tokens[0].text, "priority");
}

// --- Identifier tests ---

TEST(Lexer, Identifiers) {
    DiagnosticEngine diag;
    auto tokens = lexAll("foo _bar camelCase snake_case ABC123", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 6u); // 5 identifiers + Eof
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(tokens[i].kind, TokenKind::Identifier);
    }
    EXPECT_EQ(tokens[0].text, "foo");
    EXPECT_EQ(tokens[1].text, "_bar");
    EXPECT_EQ(tokens[2].text, "camelCase");
    EXPECT_EQ(tokens[3].text, "snake_case");
    EXPECT_EQ(tokens[4].text, "ABC123");
}

// --- Literal tests ---

TEST(Lexer, IntegerLiterals) {
    DiagnosticEngine diag;
    auto tokens = lexAll("0 42 12345", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tokens[0].text, "0");
    EXPECT_EQ(tokens[1].kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tokens[1].text, "42");
    EXPECT_EQ(tokens[2].kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tokens[2].text, "12345");
}

TEST(Lexer, StringLiterals) {
    DiagnosticEngine diag;
    auto tokens = lexAll("\"hello\" \"world\"", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].text, "hello");
    EXPECT_EQ(tokens[1].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[1].text, "world");
}

// --- Operator tests ---

TEST(Lexer, Operators) {
    DiagnosticEngine diag;
    auto tokens = lexAll("= -> :: .", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Assign);
    EXPECT_EQ(tokens[1].kind, TokenKind::Arrow);
    EXPECT_EQ(tokens[2].kind, TokenKind::ColonColon);
    EXPECT_EQ(tokens[3].kind, TokenKind::Dot);
}

// --- Delimiter tests ---

TEST(Lexer, Delimiters) {
    DiagnosticEngine diag;
    auto tokens = lexAll("{}()<>,:;&*", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 12u); // 11 delimiters + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::LCurly);
    EXPECT_EQ(tokens[1].kind, TokenKind::RCurly);
    EXPECT_EQ(tokens[2].kind, TokenKind::LParen);
    EXPECT_EQ(tokens[3].kind, TokenKind::RParen);
    EXPECT_EQ(tokens[4].kind, TokenKind::LAngle);
    EXPECT_EQ(tokens[5].kind, TokenKind::RAngle);
    EXPECT_EQ(tokens[6].kind, TokenKind::Comma);
    EXPECT_EQ(tokens[7].kind, TokenKind::Colon);
    EXPECT_EQ(tokens[8].kind, TokenKind::Semicolon);
    EXPECT_EQ(tokens[9].kind, TokenKind::Amp);
    EXPECT_EQ(tokens[10].kind, TokenKind::Star);
}

// --- Comment tests ---

TEST(Lexer, LineCommentSkipped) {
    DiagnosticEngine diag;
    auto tokens = lexAll("foo // this is a comment\nbar", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 3u); // foo, bar, Eof
    EXPECT_EQ(tokens[0].text, "foo");
    EXPECT_EQ(tokens[1].text, "bar");
}

TEST(Lexer, BlockCommentSkipped) {
    DiagnosticEngine diag;
    auto tokens = lexAll("foo /* block comment */ bar", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].text, "foo");
    EXPECT_EQ(tokens[1].text, "bar");
}

// --- Error cases ---

TEST(Lexer, UnterminatedBlockComment) {
    DiagnosticEngine diag;
    auto tokens = lexAll("foo /* unterminated", diag);
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Lexer, UnterminatedString) {
    DiagnosticEngine diag;
    auto tokens = lexAll("\"unterminated", diag);
    EXPECT_TRUE(diag.hasErrors());
    // Should still produce an Error token
    bool hasError = false;
    for (const auto& tok : tokens) {
        if (tok.is(TokenKind::Error)) hasError = true;
    }
    EXPECT_TRUE(hasError);
}

TEST(Lexer, IllegalCharacter) {
    DiagnosticEngine diag;
    auto tokens = lexAll("@", diag);
    EXPECT_TRUE(diag.hasErrors());
    bool hasError = false;
    for (const auto& tok : tokens) {
        if (tok.is(TokenKind::Error)) hasError = true;
    }
    EXPECT_TRUE(hasError);
}

// --- Source location tracking ---

TEST(Lexer, SourceLocationTracking) {
    DiagnosticEngine diag;
    auto tokens = lexAll("foo\n  bar", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_GE(tokens.size(), 2u);
    // foo at line 1, column 1
    EXPECT_EQ(tokens[0].location.line, 1);
    EXPECT_EQ(tokens[0].location.column, 1);
    // bar at line 2, column 3
    EXPECT_EQ(tokens[1].location.line, 2);
    EXPECT_EQ(tokens[1].location.column, 3);
}

// --- Empty input ---

TEST(Lexer, EmptyInput) {
    DiagnosticEngine diag;
    auto tokens = lexAll("", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Eof);
}

// --- Semicolon delimiter ---

TEST(Lexer, SemicolonSeparator) {
    DiagnosticEngine diag;
    auto tokens = lexAll(";", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Semicolon);
}

// --- New operator tests ---

TEST(Lexer, ArithmeticOperators) {
    DiagnosticEngine diag;
    auto tokens = lexAll("+ - / !", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 5u); // 4 ops + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::Plus);
    EXPECT_EQ(tokens[1].kind, TokenKind::Minus);
    EXPECT_EQ(tokens[2].kind, TokenKind::Slash);
    EXPECT_EQ(tokens[3].kind, TokenKind::Bang);
}

TEST(Lexer, ComparisonOperators) {
    DiagnosticEngine diag;
    auto tokens = lexAll("== != <= >=", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 5u); // 4 ops + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::EqEq);
    EXPECT_EQ(tokens[1].kind, TokenKind::NotEq);
    EXPECT_EQ(tokens[2].kind, TokenKind::LessEq);
    EXPECT_EQ(tokens[3].kind, TokenKind::GreaterEq);
}

TEST(Lexer, LogicalOperators) {
    DiagnosticEngine diag;
    auto tokens = lexAll("&& ||", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 3u); // 2 ops + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::AmpAmp);
    EXPECT_EQ(tokens[1].kind, TokenKind::PipePipe);
}

TEST(Lexer, OperatorDisambiguation) {
    DiagnosticEngine diag;
    // '=' vs '==', '!' vs '!=', '<' vs '<=', '>' vs '>='
    auto tokens = lexAll("= == ! != < <= > >=", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 9u); // 8 tokens + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::Assign);
    EXPECT_EQ(tokens[1].kind, TokenKind::EqEq);
    EXPECT_EQ(tokens[2].kind, TokenKind::Bang);
    EXPECT_EQ(tokens[3].kind, TokenKind::NotEq);
    EXPECT_EQ(tokens[4].kind, TokenKind::LAngle);
    EXPECT_EQ(tokens[5].kind, TokenKind::LessEq);
    EXPECT_EQ(tokens[6].kind, TokenKind::RAngle);
    EXPECT_EQ(tokens[7].kind, TokenKind::GreaterEq);
}

TEST(Lexer, MinusVsArrow) {
    DiagnosticEngine diag;
    // '-' alone vs '->' as arrow
    auto tokens = lexAll("- -> -", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 4u); // 3 tokens + Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::Minus);
    EXPECT_EQ(tokens[1].kind, TokenKind::Arrow);
    EXPECT_EQ(tokens[2].kind, TokenKind::Minus);
}

TEST(Lexer, AmpVsAmpAmp) {
    DiagnosticEngine diag;
    auto tokens = lexAll("& &&", diag);
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Amp);
    EXPECT_EQ(tokens[1].kind, TokenKind::AmpAmp);
}

// --- Comment preservation tests ---

TEST(Lexer, PreserveLineComment) {
    DiagnosticEngine diag;
    std::string src = "foo // hello world\nbar";
    Lexer lexer(src, "<test>", diag);
    lexer.setPreserveComments(true);
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.nextToken();
        tokens.push_back(tok);
        if (tok.is(TokenKind::Eof)) break;
    }
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 3u); // foo, bar, Eof
    ASSERT_EQ(lexer.comments().size(), 1u);
    EXPECT_EQ(lexer.comments()[0].text, "// hello world");
    EXPECT_FALSE(lexer.comments()[0].isBlock);
    EXPECT_EQ(lexer.comments()[0].location.line, 1);
    EXPECT_EQ(lexer.comments()[0].location.column, 5);
}

TEST(Lexer, PreserveBlockComment) {
    DiagnosticEngine diag;
    std::string src = "foo /* block */ bar";
    Lexer lexer(src, "<test>", diag);
    lexer.setPreserveComments(true);
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.nextToken();
        tokens.push_back(tok);
        if (tok.is(TokenKind::Eof)) break;
    }
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(tokens.size(), 3u);
    ASSERT_EQ(lexer.comments().size(), 1u);
    EXPECT_EQ(lexer.comments()[0].text, "/* block */");
    EXPECT_TRUE(lexer.comments()[0].isBlock);
}

TEST(Lexer, PreserveCommentsDefaultOff) {
    DiagnosticEngine diag;
    std::string src = "foo // comment\nbar";
    Lexer lexer(src, "<test>", diag);
    // preserveComments is off by default
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.nextToken();
        tokens.push_back(tok);
        if (tok.is(TokenKind::Eof)) break;
    }
    ASSERT_FALSE(diag.hasErrors());
    EXPECT_TRUE(lexer.comments().empty());
}

TEST(Lexer, PreserveMultipleCommentsOrdered) {
    DiagnosticEngine diag;
    std::string src = "// first\nfoo /* second */ bar // third\nbaz";
    Lexer lexer(src, "<test>", diag);
    lexer.setPreserveComments(true);
    while (!lexer.nextToken().is(TokenKind::Eof)) {}
    ASSERT_FALSE(diag.hasErrors());
    ASSERT_EQ(lexer.comments().size(), 3u);
    EXPECT_EQ(lexer.comments()[0].text, "// first");
    EXPECT_EQ(lexer.comments()[1].text, "/* second */");
    EXPECT_EQ(lexer.comments()[2].text, "// third");
    // Verify ordering by line
    EXPECT_EQ(lexer.comments()[0].location.line, 1);
    EXPECT_EQ(lexer.comments()[1].location.line, 2);
    EXPECT_EQ(lexer.comments()[2].location.line, 2);
}

TEST(Lexer, CommentsAvailableAfterParsing) {
    // Simulate what LSP does: Lexer -> Parser -> then read comments
    DiagnosticEngine diag;
    std::string src = "// header\nnamespace app { }";
    Lexer lexer(src, "<test>", diag);
    lexer.setPreserveComments(true);
    // Consume all tokens (as Parser would)
    while (!lexer.nextToken().is(TokenKind::Eof)) {}
    ASSERT_FALSE(diag.hasErrors());
    // Comments should still be accessible
    ASSERT_EQ(lexer.comments().size(), 1u);
    EXPECT_EQ(lexer.comments()[0].text, "// header");
}

// --- New tokens for class/template system ---

TEST(Lexer, ClassKeywords) {
    DiagnosticEngine diag;
    std::string src = "class static explicit virtual template typename";
    Lexer lexer(src, "<test>", diag);

    auto t1 = lexer.nextToken();
    EXPECT_EQ(t1.kind, TokenKind::KW_class);
    EXPECT_EQ(t1.text, "class");

    auto t2 = lexer.nextToken();
    EXPECT_EQ(t2.kind, TokenKind::KW_static);

    auto t3 = lexer.nextToken();
    EXPECT_EQ(t3.kind, TokenKind::KW_explicit);

    auto t4 = lexer.nextToken();
    EXPECT_EQ(t4.kind, TokenKind::KW_virtual);

    auto t5 = lexer.nextToken();
    EXPECT_EQ(t5.kind, TokenKind::KW_template);

    auto t6 = lexer.nextToken();
    EXPECT_EQ(t6.kind, TokenKind::KW_typename);
}

TEST(Lexer, TildeToken) {
    DiagnosticEngine diag;
    std::string src = "~Foo";
    Lexer lexer(src, "<test>", diag);

    auto t1 = lexer.nextToken();
    EXPECT_EQ(t1.kind, TokenKind::Tilde);
    EXPECT_EQ(t1.text, "~");

    auto t2 = lexer.nextToken();
    EXPECT_EQ(t2.kind, TokenKind::Identifier);
    EXPECT_EQ(t2.text, "Foo");
}

TEST(Lexer, EllipsisToken) {
    DiagnosticEngine diag;
    std::string src = "...";
    Lexer lexer(src, "<test>", diag);

    auto t1 = lexer.nextToken();
    EXPECT_EQ(t1.kind, TokenKind::Ellipsis);
    EXPECT_EQ(t1.text, "...");
}

TEST(Lexer, DotVsEllipsis) {
    // A single dot should remain Dot, not become Ellipsis
    DiagnosticEngine diag;
    std::string src = "a.b";
    Lexer lexer(src, "<test>", diag);

    auto t1 = lexer.nextToken();
    EXPECT_EQ(t1.kind, TokenKind::Identifier);
    auto t2 = lexer.nextToken();
    EXPECT_EQ(t2.kind, TokenKind::Dot);
    auto t3 = lexer.nextToken();
    EXPECT_EQ(t3.kind, TokenKind::Identifier);
}

TEST(Lexer, Phase4Keywords) {
    DiagnosticEngine diag;
    std::string src = "constraint requires adapt instantiate for";
    Lexer lexer(src, "<test>", diag);

    auto t1 = lexer.nextToken();
    EXPECT_EQ(t1.kind, TokenKind::KW_constraint);
    auto t2 = lexer.nextToken();
    EXPECT_EQ(t2.kind, TokenKind::KW_requires);
    auto t3 = lexer.nextToken();
    EXPECT_EQ(t3.kind, TokenKind::KW_adapt);
    auto t4 = lexer.nextToken();
    EXPECT_EQ(t4.kind, TokenKind::KW_instantiate);
    auto t5 = lexer.nextToken();
    EXPECT_EQ(t5.kind, TokenKind::KW_for);
}
