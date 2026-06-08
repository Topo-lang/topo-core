// JfrNdjsonMergeTest.cpp — focused unit coverage for convertJfrNdjsonStream's
// pass_events handling, specifically the hybrid-mode MERGE (vs clobber).
//
// In `topo-profile --mode hybrid` with a JFR sampling input, the trace object
// already carries span-collected `pass_events` (LLVM-side routePassEvent) when
// the JFR converter runs. The converter must MERGE its JFR-side pass events into
// that map per-pass, not overwrite it. A regression here silently drops every
// span-collected pass event.

#include "topo/Profile/JfrNdjsonConverter.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

using nlohmann::json;

namespace {

// A minimal JFR-NDJSON stream: one ordinary execution sample plus one
// `topo.pass.*` pass event (which the converter routes into pass_events).
std::string sampleStreamWithPassEvent() {
    return
        // an ordinary sampling event (must populate outJson["sampling"])
        R"({"event_type":"jdk.ExecutionSample","ts_ns":1000,"thread":{"id":1},)"
        R"("stack":[{"class":"app.Main","method":"run"}]})"
        "\n"
        // a JFR-side pass event for ArenaPass
        R"({"event_type":"topo.pass.ArenaPass","ts_ns":2000,"thread":{"id":1},)"
        R"("fields":{"region":"jfr-region"}})"
        "\n";
}

} // namespace

// When outJson has NO pre-existing pass_events, the JFR pass events become the
// pass_events map verbatim (baseline / sample-only behavior preserved).
TEST(JfrNdjsonMerge, PopulatesPassEventsWhenAbsent) {
    json out = json::object();
    std::istringstream in(sampleStreamWithPassEvent());
    std::string err;

    ASSERT_TRUE(topo::profile::convertJfrNdjsonStream(in, out, err)) << err;
    ASSERT_TRUE(out.contains("pass_events"));
    ASSERT_TRUE(out["pass_events"].contains("ArenaPass"));
    ASSERT_EQ(out["pass_events"]["ArenaPass"].size(), 1u);
    EXPECT_EQ(out["pass_events"]["ArenaPass"][0]["fields"]["region"].get<std::string>(),
              "jfr-region");
    // Sampling segment still produced.
    ASSERT_TRUE(out.contains("sampling"));
    EXPECT_EQ(out["sampling"]["summary"]["total_samples"].get<std::int64_t>(), 1);
}

// HYBRID regression: outJson already carries span-collected pass_events. The JFR
// conversion must MERGE — both the pre-existing span events AND the JFR events
// must survive, appended per-pass — instead of clobbering the span events.
TEST(JfrNdjsonMerge, MergesIntoExistingSpanPassEvents) {
    // Pre-populate as collectSpans would: a span-side ArenaPass event plus a
    // ParallelPass event that JFR never touches.
    json out = json::object();
    out["spans"] = json::array(); // mimic the hybrid out object shape
    out["pass_events"]["ArenaPass"] = json::array();
    out["pass_events"]["ArenaPass"].push_back(
        json{{"ts_ns", 500}, {"tid", 1}, {"fields", {{"region", "span-region"}}}});
    out["pass_events"]["ParallelPass"] = json::array();
    out["pass_events"]["ParallelPass"].push_back(
        json{{"ts_ns", 600}, {"tid", 1}, {"fields", {{"lanes", 4}}}});

    std::istringstream in(sampleStreamWithPassEvent());
    std::string err;
    ASSERT_TRUE(topo::profile::convertJfrNdjsonStream(in, out, err)) << err;

    ASSERT_TRUE(out.contains("pass_events"));
    const auto& pe = out["pass_events"];

    // ArenaPass must now contain BOTH the span event and the JFR event.
    ASSERT_TRUE(pe.contains("ArenaPass"));
    ASSERT_EQ(pe["ArenaPass"].size(), 2u)
        << "JFR pass events must append to, not clobber, span-collected ones";
    // Order: pre-existing span event first, JFR-merged event second.
    EXPECT_EQ(pe["ArenaPass"][0]["fields"]["region"].get<std::string>(), "span-region");
    EXPECT_EQ(pe["ArenaPass"][1]["fields"]["region"].get<std::string>(), "jfr-region");

    // ParallelPass (span-only, untouched by JFR) must survive intact.
    ASSERT_TRUE(pe.contains("ParallelPass"));
    ASSERT_EQ(pe["ParallelPass"].size(), 1u)
        << "a span-only pass must not be dropped by the JFR conversion";
    EXPECT_EQ(pe["ParallelPass"][0]["fields"]["lanes"].get<int>(), 4);

    // And the span object must be preserved alongside.
    EXPECT_TRUE(out.contains("spans"));
}
