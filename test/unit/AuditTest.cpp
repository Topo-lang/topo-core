#include "topo/Check/Audit.h"
#include "topo/Platform/TempFile.h"
#include "topo/Distribution/Sha256.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using namespace topo::check;
using json = nlohmann::json;

std::error_code createAuditSymlink(const fs::path& target, const fs::path& link, bool directory = false) {
#ifdef _WIN32
    // Some MinGW standard libraries leave create_symlink unimplemented even
    // when the host supports links. Create the real filesystem fixture via Win32.
    constexpr DWORD allowUnprivilegedCreate = 0x2;
    const DWORD kind = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), kind | allowUnprivilegedCreate)) return {};
    auto error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER || error == ERROR_PRIVILEGE_NOT_HELD) {
        if (CreateSymbolicLinkW(link.c_str(), target.c_str(), kind)) return {};
        error = GetLastError();
    }
    return {static_cast<int>(error), std::system_category()};
#else
    std::error_code error;
    if (directory) fs::create_directory_symlink(target, link, error);
    else fs::create_symlink(target, link, error);
    return error;
#endif
}
class AuditTest : public ::testing::Test {
protected:
    topo::platform::TempFile marker{"audit-tests"};
    fs::path root = marker.path().string() + "-project";
    AuditFacts facts;
    AuditPolicy policy;
    void SetUp() override {
        fs::create_directories(root);
        write("Log.java","class Log { void log() {} }\n");
        facts.supported = facts.complete = true;
        facts.provider = "test-semantic-v1";
        facts.buildVariant = "debug";
        facts.identities = {
            {"type-log","app::Log","class","","",(root/"Log.java").string(),1,"project",true},
            {"method-log","app::Log::log","method","()void","type-log",(root/"Log.java").string(),1,"project",true}};
    }
    void TearDown() override {
#ifdef _WIN32
        // Remove native fixture links before MinGW's recursive cleanup, which
        // can misclassify a directory reparse point as an ordinary directory.
        DeleteFileW((root / "selected.java").c_str());
        RemoveDirectoryW((root / "dependency" / "alias").c_str());
        RemoveDirectoryW((root / "left" / "link").c_str());
#endif
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const std::string& p,const std::string& bytes) { std::ofstream(root/p) << bytes; }
    std::error_code symlinkParentInputs() {
        fs::create_directories(root / "left");
        fs::create_directories(root / "right" / "nested");
        write("left/value.h", "left input\n");
        write("right/value.h", "right input\n");
        facts.inputs = {(root / "left/link/../value.h").string(),
                        (root / "left/value.h").string()};
        return createAuditSymlink(root / "right/nested", root / "left/link", true);
    }
    json report(const std::vector<std::string>& external={}) {
        return makeAuditReport(root.string(),facts,policy,external,{(root/"Log.java").string()},{});
    }
    json approval(const json& r) { return {{"reviews",{{r.at("reviewKey").get<std::string>(),{{"accepted",true},{"project",r.at("project")}}}}}}; }
};
TEST_F(AuditTest, ManualClassRequiresAcceptanceAndRevocationTakesEffect) {
    auto r=report({"app::Log"}); EXPECT_FALSE(r["passed"].get<bool>());
    auto trust=approval(r); applyAuditTrust(r,trust); EXPECT_TRUE(r["passed"].get<bool>());
    applyAuditTrust(r,json::object()); EXPECT_FALSE(r["passed"].get<bool>());
}
TEST_F(AuditTest, ChangedSourceInvalidatesApprovalAndSavedReport) {
    auto r=report({"app::Log"}); auto trust=approval(r);
    write("Log.java","class Log { void log() { hidden(); } }\n");
    std::string error; EXPECT_FALSE(verifyAuditSnapshot(r,error));
    auto fresh=report({"app::Log"}); applyAuditTrust(fresh,trust); EXPECT_FALSE(fresh["passed"].get<bool>());
}
TEST_F(AuditTest, ProviderDigestAndFinalVerificationBothRejectSameTimestampReplacement) {
    const auto file = (root / "Log.java").string();
    const auto time = fs::last_write_time(file);
    facts.inputDigests[file] = topo::dist::sha256File(file);
    write("Log.java", "class Log { void log() { hidden(); } }\n");
    fs::last_write_time(file, time);
    auto stale = report();
    EXPECT_FALSE(stale["passed"].get<bool>());
    EXPECT_FALSE(stale["unresolved"].empty());
    facts.inputDigests[file] = topo::dist::sha256File(file);
    auto fresh = report();
    ASSERT_TRUE(fresh["passed"].get<bool>());
    write("Log.java", "class Log { void log() { changed(); } }\n");
    fs::last_write_time(file, time);
    std::string error;
    EXPECT_FALSE(verifyAuditSnapshot(fresh, error));
}
TEST_F(AuditTest, SameSimpleNameCannotSelectOrAuthorizeAnotherClass) {
    EXPECT_FALSE(report({"Log"})["passed"].get<bool>());
    facts.identities.push_back({"fake-log","evil::Log","class","","",(root/"Log.java").string(),1,"project",true});
    facts.references.push_back({"fake-log","dynamic","",1,false,"virtual",false,{}});
    auto r=report({"app::Log"}); applyAuditTrust(r,approval(r)); EXPECT_FALSE(r["passed"].get<bool>());
}
TEST_F(AuditTest, DuplicateActualDefinitionsDoNotResolveToOneBoundary) {
    facts.identities.push_back({"different-log","app::Log","class","","",(root/"Log.java").string(),1,"project",true});
    auto r=report({"app::Log"}); EXPECT_FALSE(r["unresolved"].empty());
}
TEST_F(AuditTest, ManualClassIncludesLexicalNestedMembersButNotFreeFunctions) {
    facts.identities.push_back({"nested","app::Log::Inner","class","","type-log",(root/"Log.java").string(),1,"project",true});
    facts.identities.push_back({"nested-run","app::Log::Inner::run","method","","nested",(root/"Log.java").string(),1,"project",true});
    facts.identities.push_back({"free-run","app::run","function","","",(root/"Log.java").string(),1,"project",true});
    facts.references.push_back({"nested-run","?","",1,false,"virtual",false,{}});
    auto r=report({"app::Log"}); applyAuditTrust(r,approval(r)); EXPECT_TRUE(r["passed"].get<bool>());
    EXPECT_EQ(r["manual"][0]["members"].size(),4u);
    facts.references.push_back({"free-run","?","",1,false,"virtual",false,{}});
    r=report({"app::Log"}); applyAuditTrust(r,approval(r)); EXPECT_FALSE(r["passed"].get<bool>());
}
TEST_F(AuditTest, ThirdPartyIsOpaqueWithoutIndividualApproval) {
    write("lib.jar","binary dependency");
    facts.dependencies.push_back({"library",(root/"lib.jar").string(),"1","",false,"", ""});
    auto r=report(); EXPECT_TRUE(r["passed"].get<bool>());
    EXPECT_EQ(r["dependencies"][0]["analysis"],"dependency-opaque");
}
TEST_F(AuditTest, StrictDependencyApprovalIsScopedAndBytesBound) {
    write("lib.jar","first binary"); facts.dependencies.push_back({"library",(root/"lib.jar").string(),"1","",false,"", ""}); policy.strictDependencies=true;
    auto r=report(); auto key=r["dependencies"][0]["key"].get<std::string>();
    json trust={{"dependencies",{{key,{{"accepted",true},{"project","/another-project"}}}}}};
    applyAuditTrust(r,trust); EXPECT_FALSE(r["passed"].get<bool>());
    trust["dependencies"][key]["project"]=""; applyAuditTrust(r,trust); EXPECT_TRUE(r["passed"].get<bool>());
    write("lib.jar","changed bytes"); r=report(); applyAuditTrust(r,trust); EXPECT_FALSE(r["passed"].get<bool>());
}
TEST_F(AuditTest, DenyWinsOverUserDependencyApproval) {
    write("lib.jar","binary"); facts.dependencies.push_back({"library",(root/"lib.jar").string(),"1","",false,"", ""}); policy.deny={"library"};
    auto r=report(); auto key=r["dependencies"][0]["key"].get<std::string>();
    applyAuditTrust(r,{{"dependencies",{{key,{{"accepted",true},{"project",""}}}}}}); EXPECT_FALSE(r["passed"].get<bool>());
}
TEST_F(AuditTest, DependencyDirectoryAdditionInvalidatesSavedReport) {
    fs::create_directory(root/"classes"); write("classes/A.class","a");
    facts.dependencies.push_back({"classes",(root/"classes").string(),"","",false,"", ""});
    auto r=report(); std::string error; ASSERT_TRUE(verifyAuditSnapshot(r,error)) << error;
    write("classes/Shadow.class","new"); EXPECT_FALSE(verifyAuditSnapshot(r,error));
}
TEST_F(AuditTest, ProviderWithoutSemanticCapabilityNeverPasses) {
    facts.supported=false; EXPECT_FALSE(report()["passed"].get<bool>());
    facts.supported=true; facts.complete=false; EXPECT_FALSE(report()["passed"].get<bool>());
}
TEST_F(AuditTest, VirtualTargetAndMissingDependencyRemainUnknown) {
    facts.references.push_back({"method-log","target","",1,true,"virtual",false,{}});
    EXPECT_FALSE(report()["passed"].get<bool>());
    facts.references[0].runtimeTargetsComplete=true; facts.references[0].dependency="missing";
    EXPECT_FALSE(report()["passed"].get<bool>());
}
TEST_F(AuditTest, PolicyWideningCannotReuseEarlierApproval) {
    auto r=report({"app::Log"}); auto trust=approval(r);
    policy.manual.push_back({"file","","Log.java"});
    auto wide=report({"app::Log"}); applyAuditTrust(wide,trust); EXPECT_FALSE(wide["passed"].get<bool>());
}
TEST_F(AuditTest, SymlinkRetargetingInvalidatesSavedInputs) {
    // Equal bytes isolate the physical-target identity from content changes.
    fs::copy_file(root / "Log.java", root / "alternate.java");
    auto created = createAuditSymlink(root / "Log.java", root / "selected.java");
    ASSERT_FALSE(created) << created.message();
    facts.inputs.push_back((root / "selected.java").string());
    auto r = report();
    std::string error;
    ASSERT_TRUE(verifyAuditSnapshot(r, error)) << error;
    fs::remove(root / "selected.java");
    created = createAuditSymlink(root / "alternate.java", root / "selected.java");
    ASSERT_FALSE(created) << created.message();
    EXPECT_FALSE(verifyAuditSnapshot(r, error));
}
TEST_F(AuditTest, SymlinkParentSegmentsPreserveDistinctInputsAndManualIdentities) {
    const auto created = symlinkParentInputs();
    ASSERT_FALSE(created) << created.message();
    const auto raw = root / "left/link/../value.h";
    const auto direct = root / "left/value.h";
    std::error_code ec;
    const auto target = topo::platform::canonicalPath(raw, ec);
    ASSERT_FALSE(ec) << ec.message();
#ifndef _WIN32
    // Win32 applies its own parent-segment rules. Always bind the native target;
    // POSIX additionally demonstrates why lexical folding changes the file.
    EXPECT_EQ(target, fs::canonical(root / "right/value.h"));
    EXPECT_NE(target, fs::canonical(direct));
#endif
    facts.identities.push_back({"raw-value", "app::RawValue", "class", "", "", raw.string(), 1, "project", true});
    facts.identities.push_back({"direct-value", "app::DirectValue", "class", "", "", direct.string(), 1, "project", true});
    auto r = report({"app::RawValue", "app::DirectValue"});
    json rawInputs = json::array(), directInputs = json::array();
    for (const auto& input : r["inputs"]) {
        if (input["path"] == raw.generic_string()) rawInputs.push_back(input);
        if (input["path"] == direct.generic_string()) directInputs.push_back(input);
    }
    ASSERT_EQ(rawInputs.size(), 1u);
    ASSERT_EQ(directInputs.size(), 1u);
    EXPECT_EQ(rawInputs[0]["resolvedPath"], target.generic_string());
    EXPECT_EQ(rawInputs[0]["sha256"], topo::dist::sha256File(target.string()));
    EXPECT_EQ(directInputs[0]["sha256"], topo::dist::sha256File(direct.string()));
    ASSERT_EQ(r["manual"].size(), 2u);
    EXPECT_EQ(r["manual"][0]["identity"], "raw-value");
    EXPECT_EQ(r["manual"][0]["file"], raw.string());
    EXPECT_EQ(r["manual"][1]["identity"], "direct-value");
    EXPECT_EQ(r["manual"][1]["file"], direct.string());
    std::string error;
    ASSERT_TRUE(verifyAuditSnapshot(r, error)) << error;
    auto trust = approval(r);
    applyAuditTrust(r, trust);
    EXPECT_EQ(r["manualApproval"], "accepted");
    EXPECT_TRUE(r["passed"].get<bool>());

    // Rewriting the recorded lookup spelling must also invalidate the review.
    for (auto& input : r["inputs"])
        if (input["path"] == raw.generic_string()) input["path"] = direct.generic_string();
    EXPECT_FALSE(verifyAuditSnapshot(r, error));
    applyAuditTrust(r, trust);
    EXPECT_EQ(r["manualApproval"], "invalid");
    EXPECT_FALSE(r["passed"].get<bool>());
}
TEST_F(AuditTest, ProviderDigestsRemainBoundToUnfoldedInputPaths) {
    const auto created = symlinkParentInputs();
    ASSERT_FALSE(created) << created.message();
    for (const auto& file : facts.inputs)
        facts.inputDigests[file] = topo::dist::sha256File(file);
    auto r = report();
    EXPECT_TRUE(r["unresolved"].empty()) << r["unresolved"].dump();
    EXPECT_TRUE(r["passed"].get<bool>());
    std::string error;
    EXPECT_TRUE(verifyAuditSnapshot(r, error)) << error;

    const auto raw = facts.inputs.front();
    facts.inputDigests[raw] = topo::dist::sha256Hex("stale semantic input");
    auto stale = report();
    EXPECT_EQ(stale["unresolved"], json::array({"input changed during semantic analysis: " + raw}));
    EXPECT_FALSE(stale["passed"].get<bool>());
}
TEST_F(AuditTest, SymlinkParentInputChangesInvalidateSavedReview) {
    const auto created = symlinkParentInputs();
    ASSERT_FALSE(created) << created.message();
    const auto raw = facts.inputs.front();
    facts.inputDigests[raw] = topo::dist::sha256File(raw);
    auto r = report({"app::Log"});
    std::string error;
    ASSERT_TRUE(verifyAuditSnapshot(r, error)) << error;
    auto trust = approval(r);
    applyAuditTrust(r, trust);
    ASSERT_TRUE(r["passed"].get<bool>());

    const auto time = fs::last_write_time(raw);
    std::ofstream(raw) << "changed semantic input\n";
    fs::last_write_time(raw, time);
    EXPECT_FALSE(verifyAuditSnapshot(r, error));
    auto stale = report({"app::Log"});
    EXPECT_EQ(stale["unresolved"], json::array({"input changed during semantic analysis: " + raw}));
    applyAuditTrust(stale, trust);
    EXPECT_FALSE(stale["passed"].get<bool>());

    facts.inputDigests[raw] = topo::dist::sha256File(raw);
    auto fresh = report({"app::Log"});
    ASSERT_TRUE(verifyAuditSnapshot(fresh, error)) << error;
    EXPECT_TRUE(fresh["unresolved"].empty());
    EXPECT_NE(fresh["reviewKey"], r["reviewKey"]);
    applyAuditTrust(fresh, trust);
    EXPECT_EQ(fresh["manualApproval"], "pending");
    EXPECT_FALSE(fresh["passed"].get<bool>());
    applyAuditTrust(fresh, approval(fresh));
    EXPECT_TRUE(fresh["passed"].get<bool>());
}
TEST_F(AuditTest, DependencyDirectoryLinksRemainUnresolvedWithoutFollowing) {
    fs::create_directory(root / "dependency");
    fs::create_directory(root / "outside");
    write("outside/Hidden.class", "not an approved dependency input");
    auto created = createAuditSymlink(root / "outside", root / "dependency" / "alias", true);
    ASSERT_FALSE(created) << created.message();
    facts.dependencies.push_back({"library", (root / "dependency").string(), "1", "", false, "", ""});
    auto r = report();
    EXPECT_FALSE(r["passed"].get<bool>());
    EXPECT_FALSE(r["unresolved"].empty());
    ASSERT_EQ(r["directories"].size(), 1u);
    EXPECT_TRUE(r["directories"][0]["entries"].empty());
    std::string error;
    EXPECT_FALSE(verifyAuditSnapshot(r, error));
}
TEST_F(AuditTest, PhysicalInputFailureDoesNotHashAsEmpty) {
    facts.inputs.push_back((root/"missing").string()); EXPECT_THROW(report(),std::runtime_error);
}
TEST_F(AuditTest, DependencyJudgementsMustMatchBoundIdentities) {
    write("lib.jar","library"); facts.dependencies.push_back({"lib",(root/"lib.jar").string(),"1","",false,"external","test:lib"});
    policy.strictDependencies=true;
    auto original=report(); std::string error;
    for (const auto* field:{"project","key","analysis"}) {
        auto changed=original;
        if (std::string(field)=="project") changed["dependencies"][0][field]=true;
        else changed["dependencies"][0][field]="changed";
        EXPECT_FALSE(verifyAuditSnapshot(changed,error));
        applyAuditTrust(changed,json::object()); EXPECT_FALSE(changed["passed"].get<bool>());
    }
}
TEST_F(AuditTest, CoverageVerdictIsPartOfReviewIdentity) {
    facts.complete=false; auto original=report(); std::string error;
    auto changed=original; changed["analysisComplete"]=true; changed["unresolved"]=json::array();
    EXPECT_FALSE(verifyAuditSnapshot(changed,error)); applyAuditTrust(changed,json::object());
    EXPECT_FALSE(changed["passed"].get<bool>());
}
} // namespace
