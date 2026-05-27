// Unit tests for the minimal mustache-subset engine.

#include "topo/Debug/TemplateEngine.h"

#include <gtest/gtest.h>

using topo::debug::renderTemplate;
using topo::debug::htmlEscape;
using nlohmann::json;

TEST(TemplateEngineTest, PlainInterpolationEscapesHtml) {
    json d = {{"name", "<b>&\"'"}};
    auto r = renderTemplate("hi {{name}}", d);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.output, "hi &lt;b&gt;&amp;&quot;&#39;");
}

TEST(TemplateEngineTest, TripleBraceIsRaw) {
    json d = {{"html", "<i>x</i>"}};
    auto r = renderTemplate("{{{html}}}", d);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.output, "<i>x</i>");
}

TEST(TemplateEngineTest, MissingKeyRendersEmptyNotError) {
    json d = json::object();
    auto r = renderTemplate("[{{nope}}]", d);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.output, "[]");
}

TEST(TemplateEngineTest, DottedPathWalksObjects) {
    json d = {{"view", {{"rank", {{"sum", 42}}}}}};
    auto r = renderTemplate("sum={{view.rank.sum}}", d);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.output, "sum=42");
}

TEST(TemplateEngineTest, IfTruthyAndFalsy) {
    json d = {{"a", true}, {"b", false}, {"c", 0}, {"d", "x"}};
    auto r = renderTemplate(
        "{{#if a}}A{{/if}}{{#if b}}B{{/if}}{{#if c}}C{{/if}}{{#if d}}D{{/if}}",
        d);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.output, "AD");
}

TEST(TemplateEngineTest, EachIteratesScalarsWithDot) {
    json d = {{"xs", json::array({1, 2, 3})}};
    auto r = renderTemplate("{{#each xs}}[{{.}}]{{/each}}", d);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.output, "[1][2][3]");
}

TEST(TemplateEngineTest, EachIteratesObjects) {
    json d = {{"rows", json::array({{{"k", "a"}}, {{"k", "b"}}})}};
    auto r = renderTemplate("{{#each rows}}<{{k}}>{{/each}}", d);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.output, "<a><b>");
}

TEST(TemplateEngineTest, NestedIfInsideEach) {
    json d = {{"rows", json::array({{{"on", true}, {"v", 1}},
                                    {{"on", false}, {"v", 2}}})}};
    auto r = renderTemplate(
        "{{#each rows}}{{#if on}}{{v}}{{/if}}{{/each}}", d);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.output, "1");
}

TEST(TemplateEngineTest, CommentDropped) {
    auto r = renderTemplate("a{{! ignore me }}b", json::object());
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.output, "ab");
}

TEST(TemplateEngineTest, UnterminatedTagIsError) {
    auto r = renderTemplate("oops {{name", json::object());
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("unterminated"), std::string::npos);
}

TEST(TemplateEngineTest, UnbalancedBlockIsError) {
    auto r = renderTemplate("{{#if a}}no close", json{{"a", true}});
    EXPECT_FALSE(r.ok);
}

TEST(TemplateEngineTest, StrayCloseIsError) {
    auto r = renderTemplate("text {{/if}}", json::object());
    EXPECT_FALSE(r.ok);
}

TEST(TemplateEngineTest, HtmlEscapeHelper) {
    EXPECT_EQ(htmlEscape("<a href=\"x\">&'</a>"),
              "&lt;a href=&quot;x&quot;&gt;&amp;&#39;&lt;/a&gt;");
}
