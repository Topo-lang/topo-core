# sampling converter for `topo profile trace --mode=sampling`.
# JFR-NDJSON converter sibling for the JVM backend.
# speedscope JSON converter sibling for py-spy / Chromium
# Performance / Firefox profiler exports (Python backend default, but
# host-agnostic).
# V8 `.cpuprofile` converter for the TypeScript / V8
# backend (Chrome DevTools / `node --cpu-prof` output).
# cProfile-NDJSON converter sibling for the
# Python stdlib deterministic profiler (NDJSON intermediate emitted by the
# `topo_profile_python.cprofile_harness` Python helper).
#
# Five sampling-mode source-format converters live here:
#   - SamplingConverter:        stackcollapse-folded text (perf-script, xctrace export)
#   - JfrNdjsonConverter:       NDJSON JFR-event intermediate (JVM async-profiler /
#                               a future `topo-profile-jvm-jfr.jar` shim emit this shape)
#   - SpeedscopeConverter:      speedscope file-format-schema.json (py-spy default
#                               dump; Chromium / Firefox profiler exports)
#   - CpuProfileConverter:      V8 `.cpuprofile` JSON (Chrome DevTools / Node
#                               `--cpu-prof`; per-isolate single-thread)
#   - CProfileNdjsonConverter:  cProfile NDJSON intermediate (Python stdlib
#                               deterministic profiler; per-call edge events)
#
# All five write the same shared sampling sub-schema — `source_format`
# is the discriminator. Library is host-agnostic and zero-LLVM.

# `topo-prof` core absorbed here. ProfileEngine.cpp holds the
# analyze / profile / hints subcommand orchestration, samples.json parsing,
# runtime aggregation and hints comparison logic, byte-identical to the
# legacy topo-llvm/tools/topo-prof/main.cpp. The single LLVM-bound data
# source (static TTI from build/*.ll) stays out of topo-core behind the
# topo::profile::TTIProvider seam declared in ProfileEngine.h; the concrete
# LLVM provider lives in topo-llvm/lib/Profile/Llvm. This library remains
# strictly zero-LLVM.

add_library(TopoProfile
    SamplingConverter.cpp
    JfrNdjsonConverter.cpp
    AsyncProfilerConverter.cpp
    SpeedscopeConverter.cpp
    CpuProfileConverter.cpp
    CProfileNdjsonConverter.cpp
    SysMonitoringNdjsonConverter.cpp
    ProfileEngine.cpp
)
add_library(topo::core::TopoProfile ALIAS TopoProfile)
target_include_directories(TopoProfile PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# toml++ is header-only (Topo.toml parsing in ProfileEngine analyze/hints);
# TOMLPLUSPLUS_INCLUDE_DIR is the project-wide cache var set in the root
# CMakeLists. PRIVATE — implementation detail, not part of the API.
target_include_directories(TopoProfile PRIVATE ${TOMLPLUSPLUS_INCLUDE_DIR})
target_link_libraries(TopoProfile
    PUBLIC nlohmann_json::nlohmann_json
    # TopoSema → ImportResolver / SemanticAnalyzer / SymbolTable;
    # TopoAnalysis → groupNodesByStage. Both are zero-LLVM topo-core libs,
    # so TopoProfile gains NO LLVM dependency.
    PRIVATE TopoSema TopoAnalysis
)
topo_set_compiler_flags(TopoProfile)

topo_apply_std_pch(TopoProfile)
# Ship under topo-core-check — like TopoDebug, this is no-LLVM frontend
# tooling, and the install component split mirrors the Debug sibling.
install(TARGETS TopoProfile
    EXPORT TopoCoreTargets
    COMPONENT topo-core-check
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Profile
    DESTINATION include/topo
    COMPONENT topo-core-check
    FILES_MATCHING PATTERN "*.h"
)
