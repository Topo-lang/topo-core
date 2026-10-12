#include "topo/Check/Audit.h"
#include "topo/Platform/TempFile.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace {
namespace fs = std::filesystem;
using namespace topo::check;
using json = nlohmann::json;
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
    void TearDown() override { std::error_code ec; fs::remove_all(root,ec); }
    void write(const std::string& p,const std::string& bytes) { std::ofstream(root/p) << bytes; }
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
    facts.dependencies.push_back({"library",(root/"lib.jar").string(),"1","",false});
    auto r=report(); EXPECT_TRUE(r["passed"].get<bool>());
    EXPECT_EQ(r["dependencies"][0]["analysis"],"dependency-opaque");
}
TEST_F(AuditTest, StrictDependencyApprovalIsScopedAndBytesBound) {
    write("lib.jar","first binary"); facts.dependencies.push_back({"library",(root/"lib.jar").string(),"1","",false}); policy.strictDependencies=true;
    auto r=report(); auto key=r["dependencies"][0]["key"].get<std::string>();
    json trust={{"dependencies",{{key,{{"accepted",true},{"project","/another-project"}}}}}};
    applyAuditTrust(r,trust); EXPECT_FALSE(r["passed"].get<bool>());
    trust["dependencies"][key]["project"]=""; applyAuditTrust(r,trust); EXPECT_TRUE(r["passed"].get<bool>());
    write("lib.jar","changed bytes"); r=report(); applyAuditTrust(r,trust); EXPECT_FALSE(r["passed"].get<bool>());
}
TEST_F(AuditTest, DenyWinsOverUserDependencyApproval) {
    write("lib.jar","binary"); facts.dependencies.push_back({"library",(root/"lib.jar").string(),"1","",false}); policy.deny={"library"};
    auto r=report(); auto key=r["dependencies"][0]["key"].get<std::string>();
    applyAuditTrust(r,{{"dependencies",{{key,{{"accepted",true},{"project",""}}}}}}); EXPECT_FALSE(r["passed"].get<bool>());
}
TEST_F(AuditTest, DependencyDirectoryAdditionInvalidatesSavedReport) {
    fs::create_directory(root/"classes"); write("classes/A.class","a");
    facts.dependencies.push_back({"classes",(root/"classes").string(),"","",false});
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
    write("alternate.java","different"); fs::create_symlink(root/"Log.java",root/"selected.java"); facts.inputs.push_back((root/"selected.java").string());
    auto r=report(); fs::remove(root/"selected.java"); fs::create_symlink(root/"alternate.java",root/"selected.java");
    std::string error; EXPECT_FALSE(verifyAuditSnapshot(r,error));
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
