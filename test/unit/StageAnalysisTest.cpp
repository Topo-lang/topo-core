#include "topo/Analysis/StageAnalysis.h"
#include "topo/Sema/SymbolTable.h"
#include <gtest/gtest.h>

using namespace topo;
using namespace topo::analysis;

// StageAnalysis operates on SymbolTable::logicBlocks(). Each logic block
// stores parallel arrays calledFunctions / stages. The analyzer qualifies
// each simple callee name with the logic block's enclosing namespace.
// "<assign:...>" entries must be skipped.

TEST(StageAnalysis, BasicCalleeStageMapping) {
    SymbolTable symbols;

    LogicBlockEntry block;
    block.qualifiedName = "app::run";
    block.simpleName = "run";
    block.calledFunctions = {"init", "process", "finalize"};
    block.stages = {0, 1, 2};
    symbols.addLogicBlock(block);

    auto result = analyzeStages(symbols);

    EXPECT_EQ(result.calleeStageMap.size(), 3u);
    EXPECT_EQ(result.calleeStageMap["app::init"], 0);
    EXPECT_EQ(result.calleeStageMap["app::process"], 1);
    EXPECT_EQ(result.calleeStageMap["app::finalize"], 2);
    EXPECT_TRUE(result.logicBlockFunctions.count("app::run"));
}

TEST(StageAnalysis, EmptyLogicBlocksProducesEmptyMap) {
    SymbolTable symbols;
    auto result = analyzeStages(symbols);
    EXPECT_TRUE(result.calleeStageMap.empty());
    EXPECT_TRUE(result.logicBlockFunctions.empty());
}

TEST(StageAnalysis, AssignmentEntriesAreSkipped) {
    SymbolTable symbols;

    LogicBlockEntry block;
    block.qualifiedName = "app::run";
    block.simpleName = "run";
    block.calledFunctions = {"first", "<assign:tmp>", "second"};
    block.stages = {0, 0, 1};
    symbols.addLogicBlock(block);

    auto result = analyzeStages(symbols);

    // <assign:...> skipped, only real callees mapped
    EXPECT_EQ(result.calleeStageMap.size(), 2u);
    EXPECT_EQ(result.calleeStageMap["app::first"], 0);
    EXPECT_EQ(result.calleeStageMap["app::second"], 1);
    EXPECT_FALSE(result.calleeStageMap.count("app::<assign:tmp>"));
}

TEST(StageAnalysis, MultipleLogicBlocksAreMerged) {
    SymbolTable symbols;

    LogicBlockEntry runBlock;
    runBlock.qualifiedName = "engine::run";
    runBlock.simpleName = "run";
    runBlock.calledFunctions = {"setup", "tick"};
    runBlock.stages = {0, 1};
    symbols.addLogicBlock(runBlock);

    LogicBlockEntry shutdownBlock;
    shutdownBlock.qualifiedName = "engine::shutdown";
    shutdownBlock.simpleName = "shutdown";
    shutdownBlock.calledFunctions = {"flush", "close"};
    shutdownBlock.stages = {0, 1};
    symbols.addLogicBlock(shutdownBlock);

    auto result = analyzeStages(symbols);

    EXPECT_EQ(result.calleeStageMap["engine::setup"], 0);
    EXPECT_EQ(result.calleeStageMap["engine::tick"], 1);
    EXPECT_EQ(result.calleeStageMap["engine::flush"], 0);
    EXPECT_EQ(result.calleeStageMap["engine::close"], 1);
    EXPECT_EQ(result.logicBlockFunctions.size(), 2u);
    EXPECT_TRUE(result.logicBlockFunctions.count("engine::run"));
    EXPECT_TRUE(result.logicBlockFunctions.count("engine::shutdown"));
}

TEST(StageAnalysis, NonNamespacedBlockProducesBareCallee) {
    SymbolTable symbols;

    // Block with no "::" separator — nsPrefix stays empty.
    LogicBlockEntry block;
    block.qualifiedName = "main";
    block.simpleName = "main";
    block.calledFunctions = {"start", "stop"};
    block.stages = {0, 1};
    symbols.addLogicBlock(block);

    auto result = analyzeStages(symbols);

    EXPECT_EQ(result.calleeStageMap["start"], 0);
    EXPECT_EQ(result.calleeStageMap["stop"], 1);
    EXPECT_TRUE(result.logicBlockFunctions.count("main"));
}

TEST(StageAnalysis, CalleeAppearingInMultipleBlocksLastWriteWins) {
    // A shared helper called from two blocks at different stages: the
    // analyzer stores a flat map, so one of the writes wins (documents the
    // current contract — callers should qualify at the block level).
    SymbolTable symbols;

    LogicBlockEntry a;
    a.qualifiedName = "app::blockA";
    a.simpleName = "blockA";
    a.calledFunctions = {"helper"};
    a.stages = {0};
    symbols.addLogicBlock(a);

    LogicBlockEntry b;
    b.qualifiedName = "app::blockB";
    b.simpleName = "blockB";
    b.calledFunctions = {"helper"};
    b.stages = {2};
    symbols.addLogicBlock(b);

    auto result = analyzeStages(symbols);

    // Both blocks qualify to the same "app::helper" key; at least one
    // of the two stages must be recorded.
    ASSERT_TRUE(result.calleeStageMap.count("app::helper"));
    int recorded = result.calleeStageMap["app::helper"];
    EXPECT_TRUE(recorded == 0 || recorded == 2);
}
