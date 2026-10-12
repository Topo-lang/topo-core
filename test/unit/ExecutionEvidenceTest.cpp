#include "topo/Build/ExecutionEvidence.h"
#include "topo/Platform/ToolResolution.h"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
namespace ev = topo::build::evidence;

void createObservedDirectoryLink(const fs::path& target, const fs::path& link, std::error_code& ec) {
#ifdef _WIN32
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2)) {
        ec.clear(); return;
    }
    auto error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER || error == ERROR_PRIVILEGE_NOT_HELD) {
        if (CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY)) {
            ec.clear(); return;
        }
        error = GetLastError();
    }
    ec = std::error_code(static_cast<int>(error), std::system_category());
#else
    fs::create_directory_symlink(target, link, ec);
#endif
}
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
    void TearDown() override {
#ifdef _WIN32
        const auto alias = dir / "outputs" / "alias";
        const auto attributes = GetFileAttributesW(alias.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            RemoveDirectoryW(alias.c_str());
#endif
        std::error_code ec; fs::remove_all(dir, ec);
    }
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
    createObservedDirectoryLink(dir / "inputs", dir / "outputs" / "alias", ec);
    ASSERT_FALSE(ec) << "native symlink fixture creation failed: " << ec.message();
    ev::ExecutionRecorder recorder({{"project", dir / "outputs"}}, dir / "evidence.json", "test");
    ASSERT_EQ(recorder.run("noop", cmake, {"-E", "true"}, {}, {(dir / "outputs").string()}).exitCode, 0);
    ASSERT_EQ(recorder.manifest().nodes.size(), 1u);
    EXPECT_TRUE(recorder.manifest().nodes.front().outputs.empty());
    EXPECT_FALSE(recorder.manifest().nodes.front().complete);
    ASSERT_FALSE(recorder.diagnostics().empty());
#ifdef _WIN32
    EXPECT_NE(recorder.diagnostics().front().find("reparse-point observation is unsupported"), std::string::npos);
#else
    EXPECT_NE(recorder.diagnostics().front().find("symlink directory"), std::string::npos);
#endif
}
TEST_F(ExecutionEvidenceTest, ExplicitDirectoryLinkAndDescendantStayOutsideApprovedRoot) {
    const auto alias = dir / "outputs" / "alias";
    std::error_code ec; createObservedDirectoryLink(dir / "inputs", alias, ec);
    ASSERT_FALSE(ec) << "native symlink fixture creation failed: " << ec.message();
    ev::ExecutionRecorder recorder({{"project", dir / "outputs"}}, dir / "evidence.json", "test");
    ASSERT_EQ(recorder.run("noop", cmake, {"-E", "true"}, {},
        {alias.string(), (alias / "one").string()}).exitCode, 0);
    auto manifest = recorder.manifest(); ASSERT_EQ(manifest.nodes.size(), 1u);
    EXPECT_TRUE(manifest.nodes.front().outputs.empty());
    EXPECT_FALSE(manifest.nodes.front().complete);
    EXPECT_FALSE(recorder.diagnostics().empty());
    EXPECT_FALSE(ev::verifyManifest(manifest, {{"project", dir / "outputs"}}).buildEvidence);
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
