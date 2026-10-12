#include "topo/Build/ExecutionEvidence.h"
#include "topo/Platform/ToolResolution.h"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>

namespace {
namespace fs = std::filesystem;
namespace ev = topo::build::evidence;
class ExecutionEvidenceTest : public ::testing::Test {
protected:
    fs::path dir;
    std::string cmake;
    void SetUp() override {
        dir = fs::temp_directory_path() / ("topo-command-evidence-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(fs::create_directory(dir));
        fs::create_directory(dir / "inputs"); fs::create_directory(dir / "outputs");
        std::ofstream(dir / "inputs" / "one") << "actual input";
        cmake = topo::platform::findSpawnableOnPath("cmake");
        ASSERT_FALSE(cmake.empty());
    }
    void TearDown() override { std::error_code ec; fs::remove_all(dir, ec); }
};
TEST_F(ExecutionEvidenceTest, ActualCommandAndDynamicDirectoryOutputsAreBound) {
    ev::ExecutionRecorder recorder({{"project", dir}}, dir / "evidence.json", "test");
    auto result = recorder.run("copy", cmake, {"-E", "copy_directory", (dir / "inputs").string(),
        (dir / "outputs").string()}, {(dir / "inputs").string()}, {(dir / "outputs").string()});
    ASSERT_EQ(result.exitCode, 0);
    auto manifest = recorder.manifest(); ASSERT_EQ(manifest.nodes.size(), 1u);
    const auto& record = manifest.nodes.front();
    ASSERT_EQ(record.action.inputs.size(), 1u); ASSERT_EQ(record.outputs.size(), 1u);
    EXPECT_EQ(record.action.inputs.front().sha256, record.outputs.front().sha256);
    EXPECT_FALSE(record.complete); EXPECT_EQ(record.producer.at("isolation"), "none");
    EXPECT_TRUE(recorder.diagnostics().empty());
    std::string error; ASSERT_TRUE(recorder.flush(error)) << error;
    auto verification = ev::verifyManifest(manifest, {{"project", dir}});
    EXPECT_TRUE(verification.integrity); EXPECT_FALSE(verification.buildEvidence);
}
TEST_F(ExecutionEvidenceTest, FailedCommandAndMissingOutputRemainObservedFailures) {
    ev::ExecutionRecorder recorder({{"project", dir}}, dir / "evidence.json", "test");
    auto result = recorder.run("copy", cmake, {"-E", "copy", (dir / "absent").string(),
        (dir / "outputs" / "file").string()}, {}, {(dir / "outputs" / "file").string()});
    EXPECT_NE(result.exitCode, 0); ASSERT_EQ(recorder.manifest().nodes.size(), 1u);
    EXPECT_EQ(recorder.manifest().nodes.front().producer.at("exitCode"), std::to_string(result.exitCode));
    EXPECT_FALSE(recorder.diagnostics().empty());
}
TEST_F(ExecutionEvidenceTest, DirectorySymlinkNeverExpandsOutsideApprovedInputs) {
    std::error_code ec;
    fs::create_directory_symlink(dir / "inputs", dir / "outputs" / "alias", ec);
    if (ec) GTEST_SKIP() << "symlink creation unavailable: " << ec.message();
    ev::ExecutionRecorder recorder({{"project", dir}}, dir / "evidence.json", "test");
    ASSERT_EQ(recorder.run("noop", cmake, {"-E", "true"}, {}, {(dir / "outputs").string()}).exitCode, 0);
    EXPECT_TRUE(recorder.manifest().nodes.front().outputs.empty());
    ASSERT_FALSE(recorder.diagnostics().empty());
    EXPECT_NE(recorder.diagnostics().front().find("symlink directory"), std::string::npos);
}
TEST_F(ExecutionEvidenceTest, CompletedCommandsKeepTheirParentObservation) {
    ev::ExecutionRecorder recorder({{"project", dir}}, dir / "evidence.json", "test");
    ASSERT_EQ(recorder.run("first", cmake, {"-E", "true"}, {}, {}).exitCode, 0);
    ASSERT_EQ(recorder.run("second", cmake, {"-E", "true"}, {}, {}).exitCode, 0);
    auto manifest = recorder.manifest(); ASSERT_EQ(manifest.nodes.size(), 2u);
    EXPECT_EQ(manifest.nodes.back().parents, std::vector<std::string>{manifest.nodes.front().id});
    EXPECT_TRUE(ev::verifyManifest(manifest, {{"project", dir}}).integrity);
}
}
