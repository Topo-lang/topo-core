#include "topo/Analysis/VisibilityPolicy.h"
#include "topo/AST/ASTNode.h"
#include "topo/Sema/VisibilityCollector.h"
#include <gtest/gtest.h>

using namespace topo;
using namespace topo::analysis;

// VisibilityPolicy maps (Visibility, PolicyOutputType, TargetPlatform, debug)
// to a platform-independent VisibilityAction. The core contract:
//   * Public + SharedLibrary -> isExport
//   * Protected/Private      -> internalLinkage + inlineHint
//   * Internal               -> internalLinkage + alwaysInline + stripDebug (unless debugInternal)
//   * const / paramConsts are forwarded verbatim from VisibilityEntry

static VisibilityEntry makeEntry(const std::string& qn, Visibility vis) {
    VisibilityEntry e;
    e.qualifiedName = qn;
    e.visibility = vis;
    return e;
}

// --- Public ---

TEST(VisibilityPolicy, PublicInSharedLibraryIsExported) {
    auto entry = makeEntry("app::api", Visibility::Public);

    auto action = computeVisibilityAction(entry, PolicyOutputType::SharedLibrary, TargetPlatform::ELF);
    EXPECT_TRUE(action.isExport);
    EXPECT_FALSE(action.internalLinkage);
    EXPECT_FALSE(action.inlineHint);
    EXPECT_FALSE(action.alwaysInline);
}

TEST(VisibilityPolicy, PublicInExecutableIsNotExported) {
    auto entry = makeEntry("app::api", Visibility::Public);
    auto action = computeVisibilityAction(entry, PolicyOutputType::Executable, TargetPlatform::ELF);
    // Executable: public stays public but isn't force-exported.
    EXPECT_FALSE(action.isExport);
    EXPECT_FALSE(action.internalLinkage);
}

TEST(VisibilityPolicy, PublicOnCOFFSharedStillExported) {
    auto entry = makeEntry("app::api", Visibility::Public);
    auto action = computeVisibilityAction(entry, PolicyOutputType::SharedLibrary, TargetPlatform::COFF);
    EXPECT_TRUE(action.isExport);
}

TEST(VisibilityPolicy, PublicOnMachOSharedExported) {
    auto entry = makeEntry("app::api", Visibility::Public);
    auto action = computeVisibilityAction(entry, PolicyOutputType::SharedLibrary, TargetPlatform::MachO);
    EXPECT_TRUE(action.isExport);
}

// --- Protected / Private ---

TEST(VisibilityPolicy, ProtectedGetsInternalLinkageAndInlineHint) {
    auto entry = makeEntry("app::helper", Visibility::Protected);
    auto action = computeVisibilityAction(entry, PolicyOutputType::SharedLibrary, TargetPlatform::ELF);
    EXPECT_TRUE(action.internalLinkage);
    EXPECT_TRUE(action.inlineHint);
    EXPECT_FALSE(action.alwaysInline);
    EXPECT_FALSE(action.isExport);
}

TEST(VisibilityPolicy, PrivateGetsInternalLinkageAndInlineHint) {
    auto entry = makeEntry("app::secret", Visibility::Private);
    auto action = computeVisibilityAction(entry, PolicyOutputType::Executable, TargetPlatform::COFF);
    EXPECT_TRUE(action.internalLinkage);
    EXPECT_TRUE(action.inlineHint);
    EXPECT_FALSE(action.alwaysInline);
    EXPECT_FALSE(action.isExport);
}

// --- Internal ---

TEST(VisibilityPolicy, InternalForcesAlwaysInlineAndStripsDebugInRelease) {
    auto entry = makeEntry("app::inlined", Visibility::Internal);
    auto action =
        computeVisibilityAction(entry, PolicyOutputType::SharedLibrary, TargetPlatform::ELF, /*debugInternal=*/false);
    EXPECT_TRUE(action.internalLinkage);
    EXPECT_TRUE(action.alwaysInline);
    EXPECT_TRUE(action.stripDebug);
    EXPECT_FALSE(action.isExport);
}

TEST(VisibilityPolicy, InternalKeepsDebugInfoWhenDebugInternalIsTrue) {
    auto entry = makeEntry("app::inlined", Visibility::Internal);
    auto action =
        computeVisibilityAction(entry, PolicyOutputType::SharedLibrary, TargetPlatform::ELF, /*debugInternal=*/true);
    EXPECT_TRUE(action.internalLinkage);
    EXPECT_TRUE(action.alwaysInline);
    EXPECT_FALSE(action.stripDebug);
}

TEST(VisibilityPolicy, InternalOnAllPlatformsBehavesIdentically) {
    auto entry = makeEntry("app::inlined", Visibility::Internal);
    for (auto plat : {TargetPlatform::ELF, TargetPlatform::COFF, TargetPlatform::MachO}) {
        auto action = computeVisibilityAction(entry, PolicyOutputType::SharedLibrary, plat);
        EXPECT_TRUE(action.internalLinkage) << "platform=" << static_cast<int>(plat);
        EXPECT_TRUE(action.alwaysInline) << "platform=" << static_cast<int>(plat);
        EXPECT_TRUE(action.stripDebug) << "platform=" << static_cast<int>(plat);
    }
}

// --- Const + paramConsts propagation ---

TEST(VisibilityPolicy, ConstAndParamConstsForwardedToAction) {
    VisibilityEntry entry;
    entry.qualifiedName = "app::readOnly";
    entry.visibility = Visibility::Public;
    entry.isConst = true;
    ParamConstInfo p0;
    p0.isConst = true;
    p0.modifier = TypeNode::Ref;
    p0.ownership = OwnershipKind::None;
    entry.paramConsts.push_back(p0);
    ParamConstInfo p1;
    p1.isConst = false;
    p1.modifier = TypeNode::Ptr;
    p1.ownership = OwnershipKind::Owned;
    entry.paramConsts.push_back(p1);

    auto action = computeVisibilityAction(entry, PolicyOutputType::SharedLibrary, TargetPlatform::ELF);
    EXPECT_TRUE(action.isConst);
    ASSERT_EQ(action.paramConsts.size(), 2u);
    EXPECT_TRUE(action.paramConsts[0].isConst);
    EXPECT_EQ(action.paramConsts[0].modifier, TypeNode::Ref);
    EXPECT_FALSE(action.paramConsts[1].isConst);
    EXPECT_EQ(action.paramConsts[1].modifier, TypeNode::Ptr);
    EXPECT_EQ(action.paramConsts[1].ownership, OwnershipKind::Owned);
}

// --- Batch API ---

TEST(VisibilityPolicy, BatchApiPreservesOrderAndPerEntrySemantics) {
    std::vector<VisibilityEntry> entries = {
        makeEntry("app::pub", Visibility::Public),
        makeEntry("app::prot", Visibility::Protected),
        makeEntry("app::priv", Visibility::Private),
        makeEntry("app::intern", Visibility::Internal),
    };

    auto actions = computeVisibilityActions(entries, PolicyOutputType::SharedLibrary, TargetPlatform::ELF);
    ASSERT_EQ(actions.size(), entries.size());

    // Public -> exported
    EXPECT_TRUE(actions[0].isExport);
    EXPECT_FALSE(actions[0].internalLinkage);

    // Protected -> internal + inline hint
    EXPECT_TRUE(actions[1].internalLinkage);
    EXPECT_TRUE(actions[1].inlineHint);
    EXPECT_FALSE(actions[1].alwaysInline);

    // Private -> internal + inline hint
    EXPECT_TRUE(actions[2].internalLinkage);
    EXPECT_TRUE(actions[2].inlineHint);
    EXPECT_FALSE(actions[2].alwaysInline);

    // Internal -> internal + always inline + stripDebug
    EXPECT_TRUE(actions[3].internalLinkage);
    EXPECT_TRUE(actions[3].alwaysInline);
    EXPECT_TRUE(actions[3].stripDebug);
}

TEST(VisibilityPolicy, StaticLibraryPublicIsNotExported) {
    // StaticLibrary is not SharedLibrary, so Public should not get isExport.
    auto entry = makeEntry("app::api", Visibility::Public);
    auto action = computeVisibilityAction(entry, PolicyOutputType::StaticLibrary, TargetPlatform::ELF);
    EXPECT_FALSE(action.isExport);
}
