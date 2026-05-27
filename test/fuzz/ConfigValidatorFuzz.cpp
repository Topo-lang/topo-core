// Fuzz target for validateConfig().
//
// Strategy:
//   1. Parse fuzzer input as TOML (toml++ header-only, no-exception mode).
//   2. If TOML parse fails, return 0 -- garbage-in is expected.
//   3. Otherwise populate a BuildConfig by pulling recognized keys out of
//      the TOML table (mirroring a minimal subset of loadTopoToml, enough
//      to drive validateConfig's decision tree).
//   4. Call validateConfig(cfg). It must not crash regardless of the
//      combination of (possibly nonsensical) values.
//
// The validator is pure logic over BuildConfig fields -- its only failure
// modes are missing required fields or out-of-range numbers. We exercise
// the full surface by feeding it structurally-valid but randomly-shaped
// TOML, which is a superset of the behavior the loader itself might
// produce on weird Topo.toml files.

#include "topo/Basic/BuildTypes.h"
#include "topo/Basic/HostLanguage.h"
#include "topo/Build/BuildConfig.h"
#include "topo/Build/ConfigValidator.h"

// toml++ header-only, no exceptions (matches how topo-build uses it).
// No-op TOML_ASSERT: toml++ 3.4.0 asserts inside parse_key / parse_value on
// several malformed inputs (NUL byte, "[..", deeply nested "[[[[", etc.) that
// libFuzzer readily synthesizes. The asserts guard internal precondition
// bugs, but in release paths toml++ still funnels such inputs into its
// normal parse_error return value — so silencing the asserts lets the fuzzer
// reach our target (validateConfig) instead of dying in the library.
// Remove this override once vcpkg bumps toml++ past 3.4.0 with upstream fix.
#define TOML_HEADER_ONLY 1
#define TOML_EXCEPTIONS 0
#define TOML_ASSERT(expr) static_cast<void>(0)
#include <toml++/toml.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

// Pull a string value from a nested path in the TOML table. Returns empty
// string if the path does not exist or the value is not a string.
std::string getString(const toml::table& tbl, std::string_view section, std::string_view key) {
    if (auto* sec = tbl[section].as_table()) {
        if (auto v = (*sec)[key].value<std::string>()) return *v;
    }
    return {};
}

bool getBool(const toml::table& tbl, std::string_view section, std::string_view key, bool dflt = false) {
    if (auto* sec = tbl[section].as_table()) {
        if (auto v = (*sec)[key].value<bool>()) return *v;
    }
    return dflt;
}

int64_t getInt(const toml::table& tbl, std::string_view section, std::string_view key, int64_t dflt = 0) {
    if (auto* sec = tbl[section].as_table()) {
        if (auto v = (*sec)[key].value<int64_t>()) return *v;
    }
    return dflt;
}

topo::FeatureMode parseMode(const std::string& s) {
    if (s == "off") return topo::FeatureMode::Off;
    if (s == "force") return topo::FeatureMode::Force;
    if (s == "auto") return topo::FeatureMode::Auto;
    return topo::FeatureMode::Off;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 64 * 1024) size = 64 * 1024;
    std::string_view input(reinterpret_cast<const char*>(data), size);

    // 1. Parse as TOML. On parse failure, bail -- validator takes a
    //    populated BuildConfig, not raw TOML.
    auto result = toml::parse(input);
    if (!result) return 0;
    const toml::table& tbl = result.table();

    // 2. Populate a BuildConfig from recognized keys. Match the loader's
    //    contract: every field the validator inspects is derivable here.
    topo::build::BuildConfig cfg;

    // [build]
    cfg.rawLanguage = getString(tbl, "build", "language");
    cfg.rawOutputType = getString(tbl, "build", "output_type");
    cfg.embedIR = getBool(tbl, "build", "embed_ir");
    if (auto* buildSec = tbl["build"].as_table()) {
        if (auto* links = (*buildSec)["link_libs"].as_array()) {
            for (const auto& e : *links) {
                if (auto s = e.value<std::string>()) cfg.linkLibs.push_back(*s);
            }
        }
    }

    // [builder]
    cfg.rawBuilderMode = getString(tbl, "builder", "mode");
    cfg.rawObfuscation = getString(tbl, "builder", "obfuscation");

    // [parallel]
    // Note: `min_tasks_to_parallelize` was removed — Topo passes do
    // not gate on workload-side cost heuristics.
    cfg.parallelCfg.mode = parseMode(getString(tbl, "parallel", "mode"));
    cfg.parallelCfg.instrument = getBool(tbl, "parallel", "instrument");

    // [adaptive]
    cfg.adaptiveCfg.mode = parseMode(getString(tbl, "adaptive", "mode"));
    cfg.adaptiveCfg.min_trigger_ns =
        static_cast<uint64_t>(getInt(tbl, "adaptive", "min_trigger_ns", 0));

    // [optimize.data-layout]
    if (auto* optSec = tbl["optimize"].as_table()) {
        if (auto* dl = (*optSec)["data-layout"].as_table()) {
            if (auto s = (*dl)["mode"].value<std::string>()) cfg.dataLayoutCfg.mode = parseMode(*s);
            // Note: `min_array_size` was removed — see fuzzed
            // [parallel] section above for rationale.
        }
        if (auto* ind = (*optSec)["indirection"].as_table()) {
            if (auto s = (*ind)["mode"].value<std::string>()) cfg.indirectionCfg.mode = parseMode(*s);
        }
        if (auto* pf = (*optSec)["prefetch"].as_table()) {
            if (auto s = (*pf)["mode"].value<std::string>()) cfg.prefetchCfg.mode = parseMode(*s);
            if (auto v = (*pf)["distance"].value<int64_t>())
                cfg.prefetchCfg.distance = static_cast<int>(*v);
        }
    }

    // [observability]
    cfg.observabilityCfg.mode = parseMode(getString(tbl, "observability", "mode"));
    if (auto s = getString(tbl, "observability", "exporter"); !s.empty()) {
        cfg.observabilityCfg.exporter = s;
    }
    if (auto* obSec = tbl["observability"].as_table()) {
        if (auto v = (*obSec)["sampling_rate"].value<double>())
            cfg.observabilityCfg.samplingRate = *v;
    }

    // [lifetime]
    cfg.lifetimeCfg.mode = parseMode(getString(tbl, "lifetime", "mode"));
    cfg.lifetimeCfg.defaultArenaSize =
        static_cast<size_t>(getInt(tbl, "lifetime", "default_arena_size", 0));

    // [loop_parallel]
    // Note: `min_trip_count` was removed — see fuzzed [parallel]
    // section above for rationale.
    cfg.loopParallelCfg.mode = parseMode(getString(tbl, "loop_parallel", "mode"));
    cfg.loopParallelCfg.partitionEnabled = getBool(tbl, "loop_parallel", "partition");
    if (auto v = getInt(tbl, "loop_parallel", "chunk_size", -1); v >= 0) {
        cfg.loopParallelCfg.chunkSize = static_cast<int>(v);
    }

    // [containment]
    cfg.containmentCfg.mode = parseMode(getString(tbl, "containment", "mode"));

    // Resolve language from raw string so language-sensitive checks fire.
    if (!cfg.rawLanguage.empty()) cfg.language = topo::parseHostLanguage(cfg.rawLanguage);

    // 3. Invoke the validator. It must not crash on any combination of
    //    fields. Warnings / errors in the result are expected and ignored.
    auto vr = topo::validateConfig(cfg);
    (void)vr;

    return 0;
}
