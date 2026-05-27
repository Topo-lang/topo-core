// Debug metadata emitter test.
//
// Verifies:
//   1. emit() succeeds on a SymbolTable populated with a `debug T { ... }` block.
//   2. The produced JSON has the documented top-level shape (schema_version,
//      source, symbols) and the documented per-symbol fields.
//   3. `backend_ext` is an *empty object* until backend extensions populate it.
//   4. Golden round-trip: the JSON matches a checked-in golden file byte-for-byte,
//      modulo trailing whitespace — proves output is deterministic.

#include "topo/Basic/Diagnostic.h"
#include "topo/Debug/Emitter.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace topo;

namespace {

std::string readWholeFile(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

SymbolTable analyze(const std::string& source) {
    DiagnosticEngine diag;
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    auto ast = parser.parseTopoFile();
    EXPECT_FALSE(diag.hasErrors()) << "parser errors";
    SemanticAnalyzer sema(diag);
    auto syms = sema.analyze(*ast);
    EXPECT_FALSE(diag.hasErrors()) << "sema errors";
    return syms;
}

} // namespace

TEST(DebugEmitter, BaseShape) {
    const std::string source = R"(
std::import("cstdint", int32_t);

namespace geom {
  public:
    type Mesh {
      public:
        int32_t vertexCount;
    }
}

debug Mesh {
  view active_vertices from vertexCount;
  view sliced from vertexCount[0..64];
  summary "Mesh has {vertexCount} verts";
  inactive_region vertexCount[64..1024] hidden;
}
)";

    SymbolTable syms = analyze(source);
    ASSERT_EQ(syms.debugEntries().size(), 1u);

    debug_meta::SourceManifest manifest;
    manifest.topoFiles = {"basic.topo"};
    nlohmann::json doc = debug_meta::buildJson(syms, manifest);

    ASSERT_EQ(doc["schema_version"], "1");
    ASSERT_TRUE(doc["source"]["topo_files"].is_array());
    ASSERT_EQ(doc["source"]["topo_files"][0], "basic.topo");

    ASSERT_TRUE(doc["symbols"].is_array());
    ASSERT_EQ(doc["symbols"].size(), 1u);
    const auto& sym = doc["symbols"][0];
    EXPECT_EQ(sym["topo_name"], "Mesh");
    EXPECT_EQ(sym["host_symbol"], "geom::Mesh");
    EXPECT_EQ(sym["kind"], "type");
    EXPECT_TRUE(sym["layout_inverse"].is_object());
    EXPECT_TRUE(sym["layout_inverse"].empty()) << "layout_inverse stays empty by default";

    ASSERT_EQ(sym["views"].size(), 2u);
    EXPECT_EQ(sym["views"][0]["name"], "active_vertices");
    EXPECT_EQ(sym["views"][0]["expr"]["kind"], "field");
    EXPECT_EQ(sym["views"][1]["name"], "sliced");
    EXPECT_EQ(sym["views"][1]["expr"]["kind"], "slice");
    EXPECT_EQ(sym["views"][1]["expr"]["start"], 0);
    EXPECT_EQ(sym["views"][1]["expr"]["end"], 64);

    EXPECT_EQ(sym["summary_template"], "Mesh has {vertexCount} verts");

    ASSERT_EQ(sym["inactive_regions"].size(), 1u);
    EXPECT_EQ(sym["inactive_regions"][0]["mode"], "hidden");

    EXPECT_TRUE(sym["backend_ext"].is_object());
    EXPECT_TRUE(sym["backend_ext"].empty()) << "no backend extensions emitted by default";
}

TEST(DebugEmitter, EmptyWhenNoDebugDecls) {
    const std::string source = R"(
std::import("cstdint", int32_t);

namespace svc {
  public:
    type Foo {
      public:
        int32_t x;
    }
}
)";
    SymbolTable syms = analyze(source);
    EXPECT_TRUE(syms.debugEntries().empty());

    debug_meta::SourceManifest manifest;
    nlohmann::json doc = debug_meta::buildJson(syms, manifest);
    ASSERT_TRUE(doc["symbols"].is_array());
    EXPECT_EQ(doc["symbols"].size(), 0u);
}

TEST(DebugEmitter, GoldenFileRoundTrip) {
    // Read the spec fixture, analyze it, emit, and compare against the
    // checked-in golden JSON. Output is sorted-keys + 2-space indent (see
    // Emitter.cpp) so the comparison is byte-for-byte stable.
    std::filesystem::path fixturesDir{TOPO_SPEC_FIXTURES_DIR};
    auto fixture = fixturesDir / "debug" / "valid" / "basic_view_and_summary.topo";
    std::string source = readWholeFile(fixture);
    ASSERT_FALSE(source.empty()) << "fixture missing: " << fixture;

    SymbolTable syms = analyze(source);
    ASSERT_EQ(syms.debugEntries().size(), 1u);

    auto tmpFile = std::filesystem::temp_directory_path() /
                   "topo-debug-golden-basic.topo-dbg.json";
    debug_meta::EmitOptions opts;
    opts.outPath = tmpFile;
    opts.source.topoFiles = {"basic_view_and_summary.topo"};
    ASSERT_TRUE(debug_meta::emit(syms, opts));

    auto goldenPath = fixturesDir / "debug" / "golden" / "basic_view_and_summary.topo-dbg.json";
    std::string emitted = readWholeFile(tmpFile);
    std::string golden = readWholeFile(goldenPath);
    ASSERT_FALSE(golden.empty()) << "golden file missing: " << goldenPath;

    EXPECT_EQ(emitted, golden) << "emitter output drift; re-baseline " << goldenPath
                               << " if intentional.";
}
