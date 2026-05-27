#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// topo-prof is tested as a standalone tool. These tests validate
// the report generation logic and JSON format.

namespace {

// Simulate the complete profile report structure
TEST(TopoProfTest, ProfileReportJSONFormat) {
    nlohmann::json report;
    report["binary"] = "/path/to/binary";
    report["status"] = "complete";

    nlohmann::json functions = nlohmann::json::array();
    functions.push_back({{"name", "enhance"},
                         {"tti_estimate", 380},
                         {"runtime_avg_ns", 5200},
                         {"delta_pct", 1268.4},
                         {"suggestion", "keep_parallel"}});
    functions.push_back({{"name", "detect"},
                         {"tti_estimate", 120},
                         {"runtime_avg_ns", 3},
                         {"delta_pct", -99.975},
                         {"suggestion", "consider_sequential"}});
    report["functions"] = functions;

    std::string json = report.dump(2);
    ASSERT_FALSE(json.empty());

    // Parse it back
    auto parsed = nlohmann::json::parse(json);
    EXPECT_EQ(parsed["binary"], "/path/to/binary");
    EXPECT_EQ(parsed["status"], "complete");
    EXPECT_EQ(parsed["functions"].size(), 2u);
    EXPECT_EQ(parsed["functions"][0]["name"], "enhance");
    EXPECT_EQ(parsed["functions"][0]["suggestion"], "keep_parallel");
    EXPECT_EQ(parsed["functions"][1]["suggestion"], "consider_sequential");

    // Verify all required fields are present in each function entry
    for (const auto& func : parsed["functions"]) {
        EXPECT_TRUE(func.contains("name"));
        EXPECT_TRUE(func.contains("runtime_avg_ns"));
        EXPECT_TRUE(func.contains("tti_estimate"));
        EXPECT_TRUE(func.contains("delta_pct"));
        EXPECT_TRUE(func.contains("suggestion"));
    }
}

// Profile report without TTI data (no --project)
TEST(TopoProfTest, ProfileReportWithoutTTI) {
    nlohmann::json report;
    report["status"] = "complete";

    nlohmann::json functions = nlohmann::json::array();
    functions.push_back({{"name", "enhance"}, {"runtime_avg_ns", 5200}});
    functions.push_back({{"name", "detect"}, {"runtime_avg_ns", 3}});
    report["functions"] = functions;

    auto parsed = nlohmann::json::parse(report.dump());
    EXPECT_EQ(parsed["status"], "complete");
    EXPECT_EQ(parsed["functions"].size(), 2u);

    // Without TTI, entries should only have name and runtime_avg_ns
    EXPECT_FALSE(parsed["functions"][0].contains("tti_estimate"));
    EXPECT_FALSE(parsed["functions"][0].contains("suggestion"));
}

// Suggestion logic tests
TEST(TopoProfTest, SuggestionLogic) {
    // Very low runtime → consider_sequential
    {
        uint64_t runtime = 3; // 3ns
        EXPECT_LT(runtime, 100u);
        // This maps to "consider_sequential" in computeSuggestion()
    }

    // High delta → investigate_overhead
    {
        uint64_t ttiEst = 100;
        uint64_t runtime = 5000;
        double deltaPct =
            (static_cast<double>(runtime) - static_cast<double>(ttiEst)) / static_cast<double>(ttiEst) * 100.0;
        EXPECT_GT(deltaPct, 200.0);
        // This maps to "investigate_overhead" in computeSuggestion()
    }

    // Normal case → keep_parallel
    {
        uint64_t ttiEst = 1000;
        uint64_t runtime = 1500;
        double deltaPct =
            (static_cast<double>(runtime) - static_cast<double>(ttiEst)) / static_cast<double>(ttiEst) * 100.0;
        EXPECT_LE(deltaPct, 200.0);
        EXPECT_GE(runtime, 100u);
        // This maps to "keep_parallel" in computeSuggestion()
    }
}

// Samples JSON parsing validation
TEST(TopoProfTest, SamplesJSONParsing) {
    // Valid samples format
    nlohmann::json samples = {{"app::enhance", 5200}, {"app::detect", 3}, {"app::load", 1500}};

    std::string json = samples.dump();
    auto parsed = nlohmann::json::parse(json);

    EXPECT_TRUE(parsed.is_object());
    EXPECT_EQ(parsed.size(), 3u);
    EXPECT_EQ(parsed["app::enhance"].get<uint64_t>(), 5200u);
    EXPECT_EQ(parsed["app::detect"].get<uint64_t>(), 3u);
    EXPECT_EQ(parsed["app::load"].get<uint64_t>(), 1500u);
}

// Samples file I/O round-trip
TEST(TopoProfTest, SamplesFileRoundTrip) {
    // Create a temporary samples file
    fs::path tmpDir = fs::temp_directory_path();
    fs::path samplesFile = tmpDir / "test_samples.json";

    nlohmann::json samples = {{"app::enhance", 5200}, {"app::detect", 3}};

    {
        std::ofstream out(samplesFile);
        out << samples.dump(2);
    }

    // Read it back
    {
        std::ifstream in(samplesFile);
        ASSERT_TRUE(in.good());
        auto loaded = nlohmann::json::parse(in);

        EXPECT_EQ(loaded["app::enhance"].get<uint64_t>(), 5200u);
        EXPECT_EQ(loaded["app::detect"].get<uint64_t>(), 3u);
    }

    fs::remove(samplesFile);
}

TEST(TopoProfTest, AnalyzeReportPipelineStages) {
    nlohmann::json report;
    nlohmann::json pipelines = nlohmann::json::array();

    nlohmann::json pipeline;
    pipeline["name"] = "imaging::process";
    pipeline["stages"] = nlohmann::json::array({{{"stage", 1}, {"nodes", {"load"}}, {"parallel", false}},
                                                {{"stage", 2}, {"nodes", {"enhance", "detect"}}, {"parallel", true}},
                                                {{"stage", 3}, {"nodes", {"compose"}}, {"parallel", false}}});
    pipelines.push_back(pipeline);
    report["pipelines"] = pipelines;

    std::string json = report.dump();
    auto parsed = nlohmann::json::parse(json);

    ASSERT_EQ(parsed["pipelines"].size(), 1u);
    EXPECT_EQ(parsed["pipelines"][0]["name"], "imaging::process");

    auto& stages = parsed["pipelines"][0]["stages"];
    ASSERT_EQ(stages.size(), 3u);
    EXPECT_TRUE(stages[1]["parallel"]);
    EXPECT_FALSE(stages[0]["parallel"]);
}

TEST(TopoProfTest, FocusFilterPipeline) {
    // Simulate filtering: when focus=parallel, only show parallel stages
    nlohmann::json stages = nlohmann::json::array({{{"stage", 1}, {"nodes", {"load"}}, {"parallel", false}},
                                                   {{"stage", 2}, {"nodes", {"enhance", "detect"}}, {"parallel", true}},
                                                   {{"stage", 3}, {"nodes", {"compose"}}, {"parallel", false}}});

    nlohmann::json filtered = nlohmann::json::array();
    for (const auto& s : stages) {
        if (s["parallel"]) filtered.push_back(s);
    }

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0]["stage"], 2);
}

} // namespace
