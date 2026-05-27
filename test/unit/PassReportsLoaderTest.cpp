// PassReportsRegistry loader + pass_decision evaluator.
//
// Verifies:
//   1. loadFromDirectory parses sibling sidecar JSON files and indexes them
//      by Pass name; absent / malformed files are silently skipped.
//   2. findHeader returns the typed header for known passes and nullptr for
//      unknown ones.
//   3. The Evaluator's `pass_decision("PassName")` builtin returns the
//      header's decision string when the registry is plumbed through the
//      Environment.

#include "topo/Debug/PassReportsLoader.h"
#include "topo/Debug/Query/Evaluator.h"
#include "topo/Debug/Query/Parser.h"
#include "topo/Debug/Query/Value.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <random>
#include <string>

namespace fs = std::filesystem;
using nlohmann::json;
using namespace topo::debug_meta;
using namespace topo::debug_query;

namespace {

// Create a unique temp directory under the system temp; caller is expected
// to clean up. We don't use `std::filesystem::temp_directory_path()` alone
// because parallel tests would collide on a single shared dir.
fs::path makeTempDir(const std::string& tag) {
    std::random_device rd;
    auto base = fs::temp_directory_path() /
                ("topo-pass-reports-test-" + tag + "-" + std::to_string(rd()));
    fs::create_directories(base);
    return base;
}

void writeJsonFile(const fs::path& path, const json& body) {
    std::ofstream out(path);
    out << body.dump(2);
}

// Build the on-disk shape that PassReportsSidecar.cpp produces: top-level
// object with a `header { ... }` sub-object plus pass-specific detail.
json sidecarFor(const std::string& passName, const std::string& decision,
                const std::string& category = "OPT", bool fired = true) {
    return {
        {"header",
         {{"pass", passName},
          {"category", category},
          {"fired", fired},
          {"fired_count", fired ? 1 : 0},
          {"decision", decision},
          {"reason", "test fixture"},
          {"elapsed_ns", 1234}}},
        // A representative detail field — kept opaque by the loader, so
        // the exact key doesn't matter, but a non-empty object exercises
        // the "detail = doc minus header" code path.
        {"candidates", json::array()}};
}

} // namespace

TEST(PassReportsLoaderTest, LoadsSidecarsByPassName) {
    auto dir = makeTempDir("load");
    writeJsonFile(dir / "DataLayoutPass.json",
                  sidecarFor("DataLayoutPass", "auto_soa"));
    writeJsonFile(dir / "TopoFlattenPass.json",
                  sidecarFor("TopoFlattenPass", "no_targets"));

    std::string err;
    auto reg = PassReportsRegistry::loadFromDirectory(dir, &err);
    ASSERT_TRUE(reg.has_value()) << err;
    EXPECT_EQ(reg->size(), 2u);

    const auto* dl = reg->findHeader("DataLayoutPass");
    ASSERT_NE(dl, nullptr);
    EXPECT_EQ(dl->decision, "auto_soa");
    EXPECT_EQ(dl->passName, "DataLayoutPass");
    EXPECT_EQ(dl->category, "OPT");
    EXPECT_TRUE(dl->fired);
    EXPECT_EQ(dl->firedCount, 1);
    EXPECT_EQ(dl->reason, "test fixture");
    EXPECT_EQ(dl->elapsedNs, 1234);

    const auto* tf = reg->findHeader("TopoFlattenPass");
    ASSERT_NE(tf, nullptr);
    EXPECT_EQ(tf->decision, "no_targets");

    EXPECT_EQ(reg->findHeader("NonexistentPass"), nullptr);

    // Detail is the whole object minus the `header` block, opaque to topo-core.
    const auto* detail = reg->findDetail("DataLayoutPass");
    ASSERT_NE(detail, nullptr);
    EXPECT_TRUE(detail->is_object());
    EXPECT_FALSE(detail->contains("header"));
    EXPECT_TRUE(detail->contains("candidates"));

    fs::remove_all(dir);
}

TEST(PassReportsLoaderTest, MissingDirectoryReturnsEmptyRegistry) {
    auto dir = fs::temp_directory_path() / "topo-pass-reports-test-missing-xyz123";
    fs::remove_all(dir);  // ensure it doesn't exist

    std::string err;
    auto reg = PassReportsRegistry::loadFromDirectory(dir, &err);
    ASSERT_TRUE(reg.has_value()) << "missing dir should yield empty registry, not failure";
    EXPECT_TRUE(reg->empty());
    EXPECT_EQ(reg->findHeader("Anything"), nullptr);
}

TEST(PassReportsLoaderTest, SkipsMalformedFiles) {
    auto dir = makeTempDir("malformed");
    writeJsonFile(dir / "GoodPass.json", sidecarFor("GoodPass", "applied"));
    // A file that is JSON but missing the `header` block — should be skipped,
    // not fail the whole load.
    {
        std::ofstream out(dir / "MissingHeader.json");
        out << R"({"candidates": []})";
    }
    // A file that isn't valid JSON at all.
    {
        std::ofstream out(dir / "BrokenJson.json");
        out << "not-json-at-all";
    }
    // A non-.json file — should be ignored.
    {
        std::ofstream out(dir / "README.txt");
        out << "ignore me";
    }

    std::string err;
    auto reg = PassReportsRegistry::loadFromDirectory(dir, &err);
    ASSERT_TRUE(reg.has_value()) << err;
    EXPECT_EQ(reg->size(), 1u);
    EXPECT_NE(reg->findHeader("GoodPass"), nullptr);
    EXPECT_EQ(reg->findHeader("MissingHeader"), nullptr);
    EXPECT_EQ(reg->findHeader("BrokenJson"), nullptr);

    fs::remove_all(dir);
}

// Drive the full Evaluator with pass_decision("DataLayoutPass") against a
// freshly loaded registry. This is the end-to-end shape the CLI takes:
// parseQuery → loadFromDirectory → evaluate.
TEST(PassReportsLoaderTest, EvaluatorPassDecisionResolvesAgainstRegistry) {
    auto dir = makeTempDir("eval");
    writeJsonFile(dir / "DataLayoutPass.json",
                  sidecarFor("DataLayoutPass", "auto_soa"));
    writeJsonFile(dir / "TopoFlattenPass.json",
                  sidecarFor("TopoFlattenPass", "no_targets"));

    std::string err;
    auto regOpt = PassReportsRegistry::loadFromDirectory(dir, &err);
    ASSERT_TRUE(regOpt.has_value()) << err;
    PassReportsRegistry registry = std::move(*regOpt);

    Environment env;
    env.passReports = &registry;

    {
        std::string parseErr;
        auto expr = parseQuery("pass_decision(\"DataLayoutPass\")", parseErr);
        ASSERT_TRUE(expr) << parseErr;
        auto res = evaluate(*expr, env);
        ASSERT_TRUE(res.ok) << res.error;
        ASSERT_EQ(res.value.kind, ValueKind::String);
        EXPECT_EQ(res.value.strVal, "auto_soa");
    }

    {
        std::string parseErr;
        auto expr = parseQuery("pass_decision(\"TopoFlattenPass\")", parseErr);
        ASSERT_TRUE(expr) << parseErr;
        auto res = evaluate(*expr, env);
        ASSERT_TRUE(res.ok) << res.error;
        EXPECT_EQ(res.value.strVal, "no_targets");
    }

    // Unknown pass → evaluator error (cli_exit=3).
    {
        std::string parseErr;
        auto expr = parseQuery("pass_decision(\"GhostPass\")", parseErr);
        ASSERT_TRUE(expr) << parseErr;
        auto res = evaluate(*expr, env);
        EXPECT_FALSE(res.ok);
        EXPECT_NE(res.error.find("unknown pass"), std::string::npos);
    }

    fs::remove_all(dir);
}

// When the Environment is left with the default-init passReports==nullptr,
// the call surfaces a clear error rather than crashing. This is the
// "build produced no sidecar dir" path.
TEST(PassReportsLoaderTest, EvaluatorPassDecisionWithNullRegistryErrors) {
    Environment env;  // env.passReports stays nullptr by default
    std::string parseErr;
    auto expr = parseQuery("pass_decision(\"DataLayoutPass\")", parseErr);
    ASSERT_TRUE(expr) << parseErr;
    auto res = evaluate(*expr, env);
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.error.find("no pass-reports registry available"),
              std::string::npos);
}

// Existing tests/fixtures never set env.passReports — verify the default
// path is unaffected by the new field (no implicit nullptr deref or
// unintended behaviour change on the frame-reduction builtins).
TEST(PassReportsLoaderTest, ExistingBuiltinsUnaffectedByDefault) {
    Environment env;
    std::string parseErr;
    auto expr = parseQuery("1 + 2", parseErr);
    ASSERT_TRUE(expr) << parseErr;
    auto res = evaluate(*expr, env);
    ASSERT_TRUE(res.ok) << res.error;
    ASSERT_EQ(res.value.kind, ValueKind::Int);
    EXPECT_EQ(res.value.intVal, 3);
}

// --- Extension: pass_fired / pass_reason / pass_count
//
// Each new builtin returns a different scalar field from header. They share
// pass_decision's null-registry + unknown-pass error semantics; we cover
// happy-path here and the null-registry path in the combined test below.
// Value has no Bool kind, so pass_fired returns int 0/1 — see Evaluator.cpp
// for the rationale.

TEST(PassReportsLoaderTest, EvaluatorPassFiredReturnsIntOneWhenFired) {
    auto dir = makeTempDir("fired");
    writeJsonFile(dir / "DataLayoutPass.json",
                  sidecarFor("DataLayoutPass", "auto_soa", "OPT", /*fired=*/true));
    writeJsonFile(dir / "QuietPass.json",
                  sidecarFor("QuietPass", "no_candidates", "OPT", /*fired=*/false));

    std::string err;
    auto regOpt = PassReportsRegistry::loadFromDirectory(dir, &err);
    ASSERT_TRUE(regOpt.has_value()) << err;
    PassReportsRegistry registry = std::move(*regOpt);

    Environment env;
    env.passReports = &registry;

    {
        std::string parseErr;
        auto expr = parseQuery("pass_fired(\"DataLayoutPass\")", parseErr);
        ASSERT_TRUE(expr) << parseErr;
        auto res = evaluate(*expr, env);
        ASSERT_TRUE(res.ok) << res.error;
        ASSERT_EQ(res.value.kind, ValueKind::Int);
        EXPECT_EQ(res.value.intVal, 1);
    }

    {
        std::string parseErr;
        auto expr = parseQuery("pass_fired(\"QuietPass\")", parseErr);
        ASSERT_TRUE(expr) << parseErr;
        auto res = evaluate(*expr, env);
        ASSERT_TRUE(res.ok) << res.error;
        ASSERT_EQ(res.value.kind, ValueKind::Int);
        EXPECT_EQ(res.value.intVal, 0);
    }

    fs::remove_all(dir);
}

TEST(PassReportsLoaderTest, EvaluatorPassReasonReturnsHeaderReasonString) {
    auto dir = makeTempDir("reason");
    writeJsonFile(dir / "DataLayoutPass.json",
                  sidecarFor("DataLayoutPass", "auto_soa"));

    std::string err;
    auto regOpt = PassReportsRegistry::loadFromDirectory(dir, &err);
    ASSERT_TRUE(regOpt.has_value()) << err;
    PassReportsRegistry registry = std::move(*regOpt);

    Environment env;
    env.passReports = &registry;

    std::string parseErr;
    auto expr = parseQuery("pass_reason(\"DataLayoutPass\")", parseErr);
    ASSERT_TRUE(expr) << parseErr;
    auto res = evaluate(*expr, env);
    ASSERT_TRUE(res.ok) << res.error;
    ASSERT_EQ(res.value.kind, ValueKind::String);
    // sidecarFor() hardcodes reason = "test fixture".
    EXPECT_EQ(res.value.strVal, "test fixture");

    fs::remove_all(dir);
}

TEST(PassReportsLoaderTest, EvaluatorPassCountReturnsHeaderFiredCount) {
    auto dir = makeTempDir("count");
    writeJsonFile(dir / "DataLayoutPass.json",
                  sidecarFor("DataLayoutPass", "auto_soa", "OPT", /*fired=*/true));
    writeJsonFile(dir / "QuietPass.json",
                  sidecarFor("QuietPass", "no_candidates", "OPT", /*fired=*/false));

    std::string err;
    auto regOpt = PassReportsRegistry::loadFromDirectory(dir, &err);
    ASSERT_TRUE(regOpt.has_value()) << err;
    PassReportsRegistry registry = std::move(*regOpt);

    Environment env;
    env.passReports = &registry;

    // sidecarFor() sets fired_count = fired ? 1 : 0.
    {
        std::string parseErr;
        auto expr = parseQuery("pass_count(\"DataLayoutPass\")", parseErr);
        ASSERT_TRUE(expr) << parseErr;
        auto res = evaluate(*expr, env);
        ASSERT_TRUE(res.ok) << res.error;
        ASSERT_EQ(res.value.kind, ValueKind::Int);
        EXPECT_EQ(res.value.intVal, 1);
    }
    {
        std::string parseErr;
        auto expr = parseQuery("pass_count(\"QuietPass\")", parseErr);
        ASSERT_TRUE(expr) << parseErr;
        auto res = evaluate(*expr, env);
        ASSERT_TRUE(res.ok) << res.error;
        ASSERT_EQ(res.value.kind, ValueKind::Int);
        EXPECT_EQ(res.value.intVal, 0);
    }

    fs::remove_all(dir);
}

// All three new builtins must surface the same "no registry" error as
// pass_decision when env.passReports stays nullptr.
TEST(PassReportsLoaderTest, NewBuiltinsErrorWhenRegistryNull) {
    Environment env;  // env.passReports stays nullptr by default
    for (const char* fn : {"pass_fired", "pass_reason", "pass_count"}) {
        std::string query = std::string(fn) + "(\"DataLayoutPass\")";
        std::string parseErr;
        auto expr = parseQuery(query, parseErr);
        ASSERT_TRUE(expr) << "parse failed for " << query << ": " << parseErr;
        auto res = evaluate(*expr, env);
        EXPECT_FALSE(res.ok) << fn << " unexpectedly succeeded";
        EXPECT_NE(res.error.find("no pass-reports registry available"),
                  std::string::npos)
            << fn << " error message did not mention missing registry: " << res.error;
    }
}
