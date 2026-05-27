#include "topo/Analysis/PipelineAnalysis.h"
#include "topo/Sema/SymbolTable.h"
#include <gtest/gtest.h>

#include <algorithm>

using namespace topo;
using namespace topo::analysis;

// The four free functions in PipelineAnalysis.h are:
//   groupNodesByStage  — sort nodes by stage from a PipelineAnalysis struct
//   findUpstreamNodes  — scan PipelineEdge vector for edges ending at target
//   isSourceNode       — check membership in sourceNodes vector
//   resolveNodeCallee  — match node name (simple/last/suffix) to fully
//                        qualified name in calledFunctions

TEST(PipelineAnalysis, GroupNodesByStage_Linear) {
    ::topo::PipelineAnalysis dag;
    dag.stages["A"] = 0;
    dag.stages["B"] = 1;
    dag.stages["C"] = 2;

    auto groups = groupNodesByStage(dag);

    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].first, 0);
    EXPECT_EQ(groups[1].first, 1);
    EXPECT_EQ(groups[2].first, 2);
    ASSERT_EQ(groups[0].second.size(), 1u);
    EXPECT_EQ(groups[0].second[0], "A");
    ASSERT_EQ(groups[1].second.size(), 1u);
    EXPECT_EQ(groups[1].second[0], "B");
    ASSERT_EQ(groups[2].second.size(), 1u);
    EXPECT_EQ(groups[2].second[0], "C");
}

TEST(PipelineAnalysis, GroupNodesByStage_Diamond) {
    // A (stage 0) -> B, C (stage 1) -> D (stage 2)
    ::topo::PipelineAnalysis dag;
    dag.stages["A"] = 0;
    dag.stages["B"] = 1;
    dag.stages["C"] = 1;
    dag.stages["D"] = 2;

    auto groups = groupNodesByStage(dag);

    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].first, 0);
    EXPECT_EQ(groups[1].first, 1);
    EXPECT_EQ(groups[2].first, 2);
    EXPECT_EQ(groups[0].second.size(), 1u);
    EXPECT_EQ(groups[1].second.size(), 2u);
    // Diamond middle holds B and C in some order — both must be present.
    auto contains = [](const std::vector<std::string>& v, const std::string& s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    EXPECT_TRUE(contains(groups[1].second, "B"));
    EXPECT_TRUE(contains(groups[1].second, "C"));
    EXPECT_EQ(groups[2].second.size(), 1u);
    EXPECT_EQ(groups[2].second[0], "D");
}

TEST(PipelineAnalysis, GroupNodesByStage_NestedDiamondHasStableStageOrdering) {
    // Two nested diamonds: A -> {B,C} -> D -> {E,F} -> G
    ::topo::PipelineAnalysis dag;
    dag.stages["A"] = 0;
    dag.stages["B"] = 1;
    dag.stages["C"] = 1;
    dag.stages["D"] = 2;
    dag.stages["E"] = 3;
    dag.stages["F"] = 3;
    dag.stages["G"] = 4;

    auto groups = groupNodesByStage(dag);

    ASSERT_EQ(groups.size(), 5u);
    // Stages must be sorted ascending (std::map guarantees).
    for (size_t i = 0; i + 1 < groups.size(); ++i) {
        EXPECT_LT(groups[i].first, groups[i + 1].first);
    }
    // Each middle diamond has 2 nodes, endpoints have 1
    EXPECT_EQ(groups[0].second.size(), 1u);
    EXPECT_EQ(groups[1].second.size(), 2u);
    EXPECT_EQ(groups[2].second.size(), 1u);
    EXPECT_EQ(groups[3].second.size(), 2u);
    EXPECT_EQ(groups[4].second.size(), 1u);
}

TEST(PipelineAnalysis, GroupNodesByStage_Empty) {
    ::topo::PipelineAnalysis dag;
    auto groups = groupNodesByStage(dag);
    EXPECT_TRUE(groups.empty());
}

TEST(PipelineAnalysis, FindUpstreamNodes_MultiFanIn) {
    // B, C, D all feed into E
    std::vector<PipelineEdge> edges;
    PipelineEdge e1;
    e1.source = "B";
    e1.target = "E";
    edges.push_back(e1);
    PipelineEdge e2;
    e2.source = "C";
    e2.target = "E";
    edges.push_back(e2);
    PipelineEdge e3;
    e3.source = "D";
    e3.target = "E";
    edges.push_back(e3);
    // Extra unrelated edge
    PipelineEdge e4;
    e4.source = "X";
    e4.target = "Y";
    edges.push_back(e4);

    auto upstream = findUpstreamNodes("E", edges);
    ASSERT_EQ(upstream.size(), 3u);
    auto has = [&](const std::string& s) { return std::find(upstream.begin(), upstream.end(), s) != upstream.end(); };
    EXPECT_TRUE(has("B"));
    EXPECT_TRUE(has("C"));
    EXPECT_TRUE(has("D"));
}

TEST(PipelineAnalysis, FindUpstreamNodes_MultiFanOutFromSource) {
    // A fans out to B, C, D — asking for A's upstream must return empty.
    std::vector<PipelineEdge> edges;
    PipelineEdge e1;
    e1.source = "A";
    e1.target = "B";
    edges.push_back(e1);
    PipelineEdge e2;
    e2.source = "A";
    e2.target = "C";
    edges.push_back(e2);
    PipelineEdge e3;
    e3.source = "A";
    e3.target = "D";
    edges.push_back(e3);

    EXPECT_TRUE(findUpstreamNodes("A", edges).empty());
    // And each downstream sees A as its only upstream.
    EXPECT_EQ(findUpstreamNodes("B", edges), std::vector<std::string>{"A"});
    EXPECT_EQ(findUpstreamNodes("C", edges), std::vector<std::string>{"A"});
    EXPECT_EQ(findUpstreamNodes("D", edges), std::vector<std::string>{"A"});
}

TEST(PipelineAnalysis, FindUpstreamNodes_EmptyEdges) {
    std::vector<PipelineEdge> edges;
    EXPECT_TRUE(findUpstreamNodes("whatever", edges).empty());
}

TEST(PipelineAnalysis, IsSourceNode_PositiveAndNegative) {
    std::vector<std::string> sources = {"A", "X", "Y"};
    EXPECT_TRUE(isSourceNode("A", sources));
    EXPECT_TRUE(isSourceNode("X", sources));
    EXPECT_FALSE(isSourceNode("B", sources));
    EXPECT_FALSE(isSourceNode("", sources));
}

TEST(PipelineAnalysis, ResolveNodeCallee_ExactAndSimpleAndSuffix) {
    std::vector<std::string> called = {"engine::core::load", "engine::core::compute", "util::log"};

    // Exact match
    EXPECT_EQ(resolveNodeCallee("engine::core::load", called), "engine::core::load");
    // Last-component match (simple name match)
    EXPECT_EQ(resolveNodeCallee("compute", called), "engine::core::compute");
    // Suffix match: nodeName is "core::load"; calledFunc ends with "::core::load"
    EXPECT_EQ(resolveNodeCallee("core::load", called), "engine::core::load");
    // Not found
    EXPECT_EQ(resolveNodeCallee("missing", called), "");
}
