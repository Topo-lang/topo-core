#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Sema/SymbolTable.h"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace topo;

static std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

class FrontendPerf : public ::testing::Test {
protected:
    std::string perfSource_;

    void SetUp() override {
        perfSource_ = readFile(TOPO_TEST_FIXTURES_DIR "/perf_large.topo");
        ASSERT_FALSE(perfSource_.empty()) << "perf_large.topo not found";
    }
};

TEST_F(FrontendPerf, LexerThroughput) {
    DiagnosticEngine diag;

    auto start = std::chrono::high_resolution_clock::now();

    Lexer lexer(perfSource_, "perf_large.topo", diag);
    int tokenCount = 0;
    while (true) {
        Token tok = lexer.nextToken();
        if (tok.is(TokenKind::Eof)) break;
        ++tokenCount;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "PERF_LEXER_TOKENS: " << tokenCount << std::endl;
    std::cout << "PERF_LEXER_US: " << us << std::endl;
    if (us > 0) {
        std::cout << "PERF_LEXER_TOKENS_PER_MS: " << (tokenCount * 1000.0 / us) << std::endl;
    }

    // Sanity: a 500-function file should produce well over 500 tokens
    EXPECT_GT(tokenCount, 500);
    EXPECT_FALSE(diag.hasErrors());
}

TEST_F(FrontendPerf, ParserThroughput) {
    DiagnosticEngine diag;

    auto start = std::chrono::high_resolution_clock::now();

    Lexer lexer(perfSource_, "perf_large.topo", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "PERF_PARSER_US: " << us << std::endl;

    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(diag.hasErrors());
}

TEST_F(FrontendPerf, SemaThroughput) {
    DiagnosticEngine diag;

    // Parse first (not timed)
    Lexer lexer(perfSource_, "perf_large.topo", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_NE(ast, nullptr);
    ASSERT_FALSE(diag.hasErrors());

    auto start = std::chrono::high_resolution_clock::now();

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "PERF_SEMA_US: " << us << std::endl;
    std::cout << "PERF_SEMA_FUNCTIONS: " << symbols.functions().size() << std::endl;

    EXPECT_FALSE(diag.hasErrors());
    // 503 functions: 3 stages + 100 compute + 100 transform + 100 helper
    //               + 100 internal + 100 check
    EXPECT_GT(symbols.functions().size(), 400u);
}

TEST_F(FrontendPerf, FullPipelineThroughput) {
    DiagnosticEngine diag;

    auto start = std::chrono::high_resolution_clock::now();

    Lexer lexer(perfSource_, "perf_large.topo", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    ASSERT_NE(ast, nullptr);

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "PERF_FULL_PIPELINE_US: " << us << std::endl;
    std::cout << "PERF_FULL_PIPELINE_FUNCTIONS: " << symbols.functions().size() << std::endl;

    EXPECT_FALSE(diag.hasErrors());
    EXPECT_GT(symbols.functions().size(), 400u);
}
