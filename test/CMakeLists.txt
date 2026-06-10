# Standalone topo-core test tree.
#
# The test executables here split into two layers:
#  - frontend / analysis / debug / distribution / transform — built
#    against the libs that always exist in topo-core (zero LLVM, zero
#    language-plugin dependency).
#  - Build / Check / Transpile / per-language transpile — only present
#    when find_package(topo-lang) succeeded at top-level configure
#    (TOPO_CORE_WITH_LANG=ON). When topo-lang is absent we emit a single
#    informational status line per skipped block and proceed.
#
# Sibling test trees that historically lived under topo-core/test/{build,
# init,check,language}/ used to be added from the monorepo root; we wire
# them in here so a standalone configure picks them up. Each guards on
# the targets / source paths it actually needs.

# topo-core unit tests — built only when the plugin-aware libs they link
# against (TopoBuildLib, TopoCppTranspile, ...) are available.
if(TOPO_CORE_WITH_LANG)
    # Per-language plugin packages come from add_subdirectory in monorepo
    # or from installed packages in standalone. QUIET so monorepo (where
    # no installed config exists yet — the in-source aliases supply the
    # targets via add_subdirectory) stays silent.
    foreach(_topo_lang IN ITEMS cpp rust java python typescript)
        find_package(topo-lang-${_topo_lang} CONFIG QUIET)
    endforeach()

    # Bridge: monorepo gets bare target names (TopoCppTranspile etc) from
    # add_subdirectory; standalone find_package only imports namespaced
    # (topo::lang-cpp::TopoCppTranspile). The transpile/, fuzz/, check/,
    # init/ subdirectories below historically gate on bare names. Define
    # unprefixed aliases for the imported targets so existing
    # if(TARGET <bare>) checks work uniformly across both build modes.
    foreach(_alias_pair IN ITEMS
        "TopoCppTranspile|topo::lang-cpp::TopoCppTranspile"
        "TopoRustTranspile|topo::lang-rust::TopoRustTranspile"
        "TopoJavaTranspile|topo::lang-java::TopoJavaTranspile"
        "TopoPythonTranspile|topo::lang-python::TopoPythonTranspile"
        "TopoCppPlugin|topo::lang-cpp::TopoCppPlugin"
        "TopoRustPlugin|topo::lang-rust::TopoRustPlugin"
        "TopoJavaPlugin|topo::lang-java::TopoJavaPlugin"
        "TopoPythonPlugin|topo::lang-python::TopoPythonPlugin"
        "TopoTypeScriptPlugin|topo::lang-typescript::TopoTypeScriptPlugin"
        "TopoCppAnalysis|topo::lang-cpp::TopoCppAnalysis"
    )
        string(REPLACE "|" ";" _pair "${_alias_pair}")
        list(GET _pair 0 _bare)
        list(GET _pair 1 _ns)
        if(TARGET ${_ns} AND NOT TARGET ${_bare})
            add_library(${_bare} ALIAS ${_ns})
        endif()
    endforeach()

    # ── stdio helper for the process/platform suites ──
    # PipedProcessTest + ProcessSpawnTest spawn this tiny binary (cat /
    # echo-lines / echo-args / exit / sleep modes) instead of /bin/cat, so
    # those suites run — without skips — on Windows too, where PipedProcess /
    # runProcess use the raw-Win32 backend. It is spawned at test runtime,
    # not linked, hence the add_dependencies below.
    add_executable(topo-process-test-helper helpers/ProcessTestHelper.cpp)
    topo_set_compiler_flags(topo-process-test-helper)

    add_executable(topo-core-tests
        unit/LexerTest.cpp
        unit/ParserTest.cpp
        unit/ParserSafetyTest.cpp
        unit/SemaTest.cpp
        unit/SmartStructMatcherTest.cpp
        unit/PriorityAnalysisTest.cpp
        unit/LifetimeAnalysisTest.cpp
        unit/StageAnalysisTest.cpp
        unit/PipelineAnalysisTest.cpp
        unit/VisibilityPolicyTest.cpp
        unit/ImportPathCheckTest.cpp
        unit/PipedProcessTest.cpp
        unit/ProcessSpawnTest.cpp
        unit/TempFileTest.cpp
        unit/SanitizeTest.cpp
        unit/FrontendPerfTest.cpp
        unit/VerificationOrchestratorTest.cpp
        unit/PruningOrchestratorTest.cpp
        unit/StageIsolationCheckTest.cpp
        unit/VisibilityCheckTest.cpp
        unit/PurityCheckTest.cpp
        unit/CompletenessCheckTest.cpp
        unit/ContainmentCheckTest.cpp
        unit/TopoProfTest.cpp
        unit/JfrNdjsonMergeTest.cpp
        unit/TransformTest.cpp
        unit/TranspileTest.cpp
        unit/TypeRegistryTest.cpp
        unit/TranspileNewConstructsTest.cpp
        unit/TranspileVerificationTest.cpp
        unit/TopoSourceTranspileTest.cpp
        unit/ModelOptimizerTest.cpp
        unit/CapabilityCatalogTest.cpp
        unit/SpecSemanticStructureTest.cpp
        unit/DebugEmitterTest.cpp
        unit/DebugIpcTest.cpp
        unit/WebSocketOriginTest.cpp
        unit/BuildCheckModeTest.cpp
        unit/TemplateEngineTest.cpp
        unit/PassReportsLoaderTest.cpp
        unit/EvaluatorStructTest.cpp
        unit/FormatterTest.cpp
        unit/DistributionTest.cpp
        unit/LSPBridgeTest.cpp
        build/PassCategoryRegistryTest.cpp
    )
    # TopoBuildLib PUBLIC-propagates TopoLang → TopoCheck; each per-language
    # transpile lib (TopoCppTranspile / ...) PUBLIC-depends on TopoTranspile.
    # Explicit TopoCheck and TopoTranspile were the duplicates.
    target_link_libraries(topo-core-tests PRIVATE
        TopoTransform TopoDebug TopoFormat
        topo::lang-cpp::TopoCppTranspile
        topo::lang-rust::TopoRustTranspile
        topo::lang-java::TopoJavaTranspile
        topo::lang-python::TopoPythonTranspile
        TopoBuildLib TopoDistribution
        TopoLSPBridge
        TopoProfile
        nlohmann_json::nlohmann_json
        GTest::gtest_main
    )
    target_compile_definitions(topo-core-tests PRIVATE
        TOPO_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        TOPO_SPEC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/language/spec"
        TOPO_PROCESS_TEST_HELPER="$<TARGET_FILE:topo-process-test-helper>"
    )
    add_dependencies(topo-core-tests topo-process-test-helper)
    topo_set_compiler_flags(topo-core-tests)
    topo_apply_std_pch(topo-core-tests)
    # DISCOVERY_TIMEOUT 30: the CMake default is 5s, too tight for a heavily
    # linked binary when post-link discovery competes with concurrent link
    # jobs on a loaded machine — under-budgeting it causes flaky
    # gtest-discovery timeouts at test-registration time.
    gtest_discover_tests(topo-core-tests
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "toolchain\;core" TIMEOUT 120)

    # Smoke tests: fast subset for sanity checking (<30s total)
    # These cover parser, sema, and check fundamentals — if these fail, nothing works.
    add_test(NAME smoke.core.parser
        COMMAND topo-core-tests --gtest_filter="ParserTest.*:ParserSafetyTest.*")
    set_tests_properties(smoke.core.parser PROPERTIES
        LABELS "smoke;core" TIMEOUT 30)

    add_test(NAME smoke.core.sema
        COMMAND topo-core-tests --gtest_filter="SemaTest.*")
    set_tests_properties(smoke.core.sema PROPERTIES
        LABELS "smoke;core" TIMEOUT 30)

    add_test(NAME smoke.core.check
        COMMAND topo-core-tests --gtest_filter="CompletenessCheckTest.*:VisibilityCheckTest.*:StageIsolationCheckTest.*:PurityCheckTest.*:ContainmentCheckTest.*")
    set_tests_properties(smoke.core.check PROPERTIES
        LABELS "smoke;core" TIMEOUT 30)

    # Transpile equivalence tests (compile + run emitted code in 4 languages)
    add_subdirectory(transpile)
else()
    message(STATUS "topo-core tests: topo-core-tests + transpile-equivalence "
        "skipped (topo-lang not found — set CMAKE_PREFIX_PATH to a topo-lang "
        "install prefix to enable them).")
endif()

# --- topo-transpile CLI: post-transpile verification flag surface --------
# The topo-transpile CLI lives in topo-cli, not topo-core. These tests only
# fire when a topo-cli build adds topo-core as a subdirectory and provides
# the target (or in any future configuration where TARGET topo-transpile is
# defined). In standalone topo-core mode these self-skip.
if(TARGET topo-transpile)
    # Deterministic, extractor-free coverage of the new --verify / --verify-strict
    # argument surface. The unsupported-gate decision itself (success=false iff
    # N>k + precise error) is exhaustively covered at driver level by
    # TranspileVerificationTest (same applyVerificationGate code path run() uses);
    # these ctests prove the CLI parses and advertises the flags correctly.
    set(_TOPO_TRANSPILE_BIN "$<TARGET_FILE:topo-transpile>")

    add_test(NAME e2e.topo-transpile.help_lists_verify_flags
        COMMAND ${_TOPO_TRANSPILE_BIN} --help)
    set_tests_properties(e2e.topo-transpile.help_lists_verify_flags PROPERTIES
        PASS_REGULAR_EXPRESSION "--verify\\[=N\\].*--verify-strict"
        LABELS "e2e;transpile;toolchain"
        TIMEOUT 30)

    # Invalid --verify value is rejected before any extraction is attempted.
    add_test(NAME e2e.topo-transpile.verify_rejects_non_integer
        COMMAND ${_TOPO_TRANSPILE_BIN} --verify=abc --from cpp --to rust
                --sources x.cpp api.topo)
    set_tests_properties(e2e.topo-transpile.verify_rejects_non_integer PROPERTIES
        PASS_REGULAR_EXPRESSION "--verify expects a non-negative integer"
        LABELS "e2e;transpile;toolchain"
        TIMEOUT 30)

    add_test(NAME e2e.topo-transpile.verify_rejects_negative
        COMMAND ${_TOPO_TRANSPILE_BIN} --verify=-1 --from cpp --to rust
                --sources x.cpp api.topo)
    set_tests_properties(e2e.topo-transpile.verify_rejects_negative PROPERTIES
        WILL_FAIL TRUE
        LABELS "e2e;transpile;toolchain"
        TIMEOUT 30)
endif()

# Fuzz targets (conditional on TOPO_BUILD_FUZZ; the subdir's own option
# gate handles the off-by-default case before it touches any plugin lib).
add_subdirectory(fuzz)

# minimal-dist smoke ctest (label: minimal-dist)
add_subdirectory(dist-smoke)

# LLVM toolchain relocation ctest (label: relocate)
add_subdirectory(relocate)

# Sibling test subdirectories that used to be wired from the monorepo
# root. Each guards on the targets / sources it actually depends on, so
# they are safe to register unconditionally.
add_subdirectory(language)
add_subdirectory(check)
add_subdirectory(build)
add_subdirectory(init)

# Audit gates (label: audit-gate). Open-source release portability
# checks. Each script is a self-contained shell command that exits
# non-zero on a finding. WIN32 is excluded because the gate scripts
# use /bin/sh; the gate's *content* still applies on Windows, but the
# runner only fires on POSIX CI machines.
if(NOT WIN32)
    # Helper: register a single audit-gate script as a CTest entry if
    # the script file exists on disk. Keeps the gate registration block
    # short as gates are added.
    function(topo_register_audit_gate gate_name relative_path)
        # CMAKE_SOURCE_DIR (top-level), not PROJECT_SOURCE_DIR: the audit
        # scripts live at the meta root (scripts/audit/), but this test tree is
        # processed in topo-core's own project() scope where PROJECT_SOURCE_DIR
        # is the topo-core clone, not the meta root — so PROJECT_SOURCE_DIR
        # pointed at a non-existent topo-core/scripts/audit/ and every gate
        # self-skipped in the meta union build. CMAKE_SOURCE_DIR resolves to the
        # meta root (restoring the pre-split monorepo behavior), and each script
        # self-locates its REPO_ROOT from $0, so it scans the right tree. A
        # standalone topo-core configure has no scripts/audit/ and cleanly skips.
        set(_AUDIT_GATE_SH "${CMAKE_SOURCE_DIR}/${relative_path}")
        if(EXISTS "${_AUDIT_GATE_SH}")
            add_test(NAME audit-gate.${gate_name}
                COMMAND bash "${_AUDIT_GATE_SH}")
            set_tests_properties(audit-gate.${gate_name} PROPERTIES
                LABELS "audit-gate;toolchain"
                TIMEOUT 30)
        endif()
    endfunction()

    topo_register_audit_gate(no-hardcoded-tmpdir
        "scripts/audit/no-hardcoded-tmpdir.sh")
    topo_register_audit_gate(no-missing-cstdint
        "scripts/audit/no-missing-cstdint.sh")
    topo_register_audit_gate(no-missing-cstring
        "scripts/audit/no-missing-cstring.sh")
    topo_register_audit_gate(no-bare-home-getenv
        "scripts/audit/no-bare-home-getenv.sh")
endif()

# topo-debug + mock adapter E2E (label: topo-debug)
# Defined here because the root only calls enable_testing() AFTER
# add_subdirectory(topo-core), so add_test() in tool dirs is dropped.
set(_TOPO_DEBUG_BIN "$<TARGET_FILE:topo-debug>")
set(_TOPO_DEBUG_MOCK "$<TARGET_FILE:topo-debug-mock-adapter>")

function(topo_add_debug_e2e name fixture expr_arg expected_regex)
    add_test(NAME e2e.topo-debug.${name}
        COMMAND ${_TOPO_DEBUG_BIN} query ${expr_arg}
            --break t.c:1
            --mock-fixture ${fixture}
            --adapter ${_TOPO_DEBUG_MOCK})
    set_tests_properties(e2e.topo-debug.${name} PROPERTIES
        PASS_REGULAR_EXPRESSION "${expected_regex}"
        LABELS "e2e;topo-debug;toolchain"
        TIMEOUT 30)
endfunction()

# Fixture × representative query — bytes-native CLI path, no real debugger.
topo_add_debug_e2e(sum_one_hot      16x16_int_one_hot   "sum(matrix)"   "^42\n")
topo_add_debug_e2e(shape_one_hot    16x16_int_one_hot   "shape(matrix)" "^\\[16, 16\\]\n")
topo_add_debug_e2e(count_one_hot    16x16_int_one_hot   "count(matrix)" "^256\n")
topo_add_debug_e2e(min_one_hot      16x16_int_one_hot   "min(matrix)"   "^0\n")
topo_add_debug_e2e(max_one_hot      16x16_int_one_hot   "max(matrix)"   "^42\n")
topo_add_debug_e2e(dtype_one_hot    16x16_int_one_hot   "dtype(matrix)" "^i32\n")
topo_add_debug_e2e(shape_f32_ramp   1024x1024_f32_ramp  "shape(matrix)" "^\\[1024, 1024\\]\n")
topo_add_debug_e2e(dtype_f32_ramp   1024x1024_f32_ramp  "dtype(matrix)" "^f32\n")
topo_add_debug_e2e(sum_large_16MiB  large_16MiB_f32     "sum(matrix)"   "^4194304\n")
topo_add_debug_e2e(count_empty      empty               "count(matrix)" "^0\n")
topo_add_debug_e2e(shape_empty      empty               "shape(matrix)" "^\\[0\\]\n")

# arithmetic ops in queries (`+ - * /` + unary `-` + parens).
# Mock fixture `16x16_int_one_hot` has matrix[5][7]=42, all others 0 → sum=42,
# count=256, max=42, min=0. Verify the parser precedence / promotion rules and
# the divide-by-zero error path. Each topo_add_debug_e2e is one CTest entry
# with PASS_REGULAR_EXPRESSION anchored on the rendered text.
topo_add_debug_e2e(arith_sum_div_count
                  16x16_int_one_hot  "sum(matrix) / count(matrix)" "^0\\.16")
topo_add_debug_e2e(arith_max_minus_min
                  16x16_int_one_hot  "max(matrix) - min(matrix)"   "^42\n")
topo_add_debug_e2e(arith_precedence
                  16x16_int_one_hot  "sum(matrix) + 2 * 4"         "^50\n")
topo_add_debug_e2e(arith_parens
                  16x16_int_one_hot  "(sum(matrix) + 8) * 2"       "^100\n")
topo_add_debug_e2e(arith_unary_neg
                  16x16_int_one_hot  "-sum(matrix)"                "^-42\n")

# Error: divide by zero. WILL_FAIL flips exit-3 into a passing test.
add_test(NAME e2e.topo-debug.arith_div_by_zero
    COMMAND ${_TOPO_DEBUG_BIN} query "sum(matrix) / 0" --break t.c:1
        --mock-fixture 16x16_int_one_hot --adapter ${_TOPO_DEBUG_MOCK})
set_tests_properties(e2e.topo-debug.arith_div_by_zero PROPERTIES
    WILL_FAIL TRUE LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)


# Error paths: exit 3 expected.
add_test(NAME e2e.topo-debug.mean_empty_errors
    COMMAND ${_TOPO_DEBUG_BIN} query "mean(matrix)" --break t.c:1
        --mock-fixture empty --adapter ${_TOPO_DEBUG_MOCK})
set_tests_properties(e2e.topo-debug.mean_empty_errors PROPERTIES
    WILL_FAIL TRUE LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

add_test(NAME e2e.topo-debug.unknown_var_errors
    COMMAND ${_TOPO_DEBUG_BIN} query "sum(nope)" --break t.c:1
        --mock-fixture 16x16_int_one_hot --adapter ${_TOPO_DEBUG_MOCK})
set_tests_properties(e2e.topo-debug.unknown_var_errors PROPERTIES
    WILL_FAIL TRUE LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

# JSON-line output format
add_test(NAME e2e.topo-debug.jsonl_format
    COMMAND ${_TOPO_DEBUG_BIN} query "shape(matrix)" --break t.c:1
        --mock-fixture 16x16_int_one_hot --adapter ${_TOPO_DEBUG_MOCK}
        --format jsonl)
set_tests_properties(e2e.topo-debug.jsonl_format PROPERTIES
    PASS_REGULAR_EXPRESSION "\"result\":\\[16,16\\]"
    LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

# static (sidecar-only) query mode against
# checked-in JVM / V8-shaped VisibilityPass.json fixtures. No adapter,
# no --break: the CLI loads the sibling <target>.topo-passes/ directory
# and resolves pass_decision / pass_fired / pass_reason / pass_count
# against the 7-field pass-event header. Validates host-agnostic
# consumption — LLVM (existing tests), JVM, and V8 emit the same shape;
# the consumer sees just JSON. Each fixture's `decision` differs
# ("applied" vs "forced_applied") so regex anchors prove which sidecar
# was actually read.
set(_PASSES_FIXTURE_BASE
    "${CMAKE_CURRENT_SOURCE_DIR}/topo-passes-sidecar/fixtures")

add_test(NAME e2e.topo-debug.pass_decision_jvm_style
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_decision(\"VisibilityPass\")"
        --target "${_PASSES_FIXTURE_BASE}/jvm")
set_tests_properties(e2e.topo-debug.pass_decision_jvm_style PROPERTIES
    PASS_REGULAR_EXPRESSION "^applied\n"
    LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

add_test(NAME e2e.topo-debug.pass_decision_v8_style
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_decision(\"VisibilityPass\")"
        --target "${_PASSES_FIXTURE_BASE}/v8")
set_tests_properties(e2e.topo-debug.pass_decision_v8_style PROPERTIES
    PASS_REGULAR_EXPRESSION "^forced_applied\n"
    LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

add_test(NAME e2e.topo-debug.pass_fired_jvm_style
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_fired(\"VisibilityPass\")"
        --target "${_PASSES_FIXTURE_BASE}/jvm")
set_tests_properties(e2e.topo-debug.pass_fired_jvm_style PROPERTIES
    PASS_REGULAR_EXPRESSION "^1\n"
    LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

add_test(NAME e2e.topo-debug.pass_count_v8_style
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_count(\"VisibilityPass\")"
        --target "${_PASSES_FIXTURE_BASE}/v8")
set_tests_properties(e2e.topo-debug.pass_count_v8_style PROPERTIES
    PASS_REGULAR_EXPRESSION "^1\n"
    LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

# Static path negative: unknown pass surfaces a clean error (exit 3).
add_test(NAME e2e.topo-debug.pass_decision_unknown_errors
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_decision(\"NotAPass\")"
        --target "${_PASSES_FIXTURE_BASE}/jvm")
set_tests_properties(e2e.topo-debug.pass_decision_unknown_errors PROPERTIES
    WILL_FAIL TRUE LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

# Static path validation: no --debug-meta and no --target → exit 1 (CLI usage).
add_test(NAME e2e.topo-debug.static_query_requires_sidecar_dir
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_decision(\"VisibilityPass\")")
set_tests_properties(e2e.topo-debug.static_query_requires_sidecar_dir PROPERTIES
    WILL_FAIL TRUE LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

# Static path + jsonl format — proves the format switch carries through the
# no-adapter path too (frame field is absent because no breakpoint was hit).
add_test(NAME e2e.topo-debug.pass_decision_jsonl
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_decision(\"VisibilityPass\")"
        --target "${_PASSES_FIXTURE_BASE}/v8" --format jsonl)
set_tests_properties(e2e.topo-debug.pass_decision_jsonl PROPERTIES
    PASS_REGULAR_EXPRESSION "\"result\":\"forced_applied\""
    LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

# detail-field builtin e2e.
#   pass_field_rename(pass, topo_field)       → host-side name (JVM DataLayoutPass)
#   pass_eliminated_fields(pass, host_fn)     → list[int] of dropped struct indices
#   pass_intercepted_callees(pass, caller)    → list[string] of guarded callees
# Each fixture lives under topo-passes-sidecar/fixtures/<scenario>.topo-passes/
# and is read via the same static-mode path as the existing pass_decision
# tests. Missing detail / unknown row returns the literal `null` (text form)
# rather than erroring, so consumers can distinguish "Pass didn't touch this
# symbol" from "query syntax broken".
add_test(NAME e2e.topo-debug.pass_field_rename_jvm
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_field_rename(\"DataLayoutPass\",\"x\")"
        --target "${_PASSES_FIXTURE_BASE}/datalayout-jvm")
set_tests_properties(e2e.topo-debug.pass_field_rename_jvm PROPERTIES
    PASS_REGULAR_EXPRESSION "^Mesh\\$x_arr\n"
    LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

add_test(NAME e2e.topo-debug.pass_eliminated_fields_llvm
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_eliminated_fields(\"ReturnSpecializationPass\",\"compute()V\")"
        --target "${_PASSES_FIXTURE_BASE}/returnspec-llvm")
set_tests_properties(e2e.topo-debug.pass_eliminated_fields_llvm PROPERTIES
    PASS_REGULAR_EXPRESSION "^\\[0, 2\\]\n"
    LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

add_test(NAME e2e.topo-debug.pass_intercepted_callees_llvm
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_intercepted_callees(\"ContainmentInterceptionPass\",\"main\")"
        --target "${_PASSES_FIXTURE_BASE}/containment-llvm")
set_tests_properties(e2e.topo-debug.pass_intercepted_callees_llvm PROPERTIES
    # JSON-style list with quoted strings; the regex escapes the
    # leading bracket and the inner colon-bearing callee names.
    PASS_REGULAR_EXPRESSION "\\[\"std::vector::push_back\",\"std::cout::operator<<\"\\]"
    LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

# Negative — unknown pass returns the literal `null` (None value), not an
# error. Distinct from pass_decision_unknown_errors above which DOES error;
# the detail-field builtins are diagnostic and tolerate missing data.
add_test(NAME e2e.topo-debug.pass_unknown_pass_returns_null
    COMMAND ${_TOPO_DEBUG_BIN} query "pass_field_rename(\"NoSuchPass\",\"x\")"
        --target "${_PASSES_FIXTURE_BASE}/datalayout-jvm")
set_tests_properties(e2e.topo-debug.pass_unknown_pass_returns_null PROPERTIES
    PASS_REGULAR_EXPRESSION "^null\n"
    LABELS "e2e;topo-debug;toolchain" TIMEOUT 30)

# `topo debug serve` HTTP smoke test (label: topo-debug-serve).
# Drives the embedded SPA + every documented route against the mock adapter.
# POSIX-only; Windows is out of scope for serve mode.
if(NOT WIN32)
    set(_SERVE_SMOKE "${CMAKE_CURRENT_SOURCE_DIR}/debug-serve/serve_smoke.sh")
    set(_SERVE_META
        "${CMAKE_CURRENT_SOURCE_DIR}/language/spec/debug/golden/basic_view_and_summary.topo-dbg.json")
    add_test(NAME e2e.topo-debug-serve.smoke
        COMMAND bash "${_SERVE_SMOKE}")
    set_tests_properties(e2e.topo-debug-serve.smoke PROPERTIES
        ENVIRONMENT
            "TOPO_DEBUG_BIN=$<TARGET_FILE:topo-debug>;TOPO_DEBUG_MOCK=$<TARGET_FILE:topo-debug-mock-adapter>;DEBUG_META=${_SERVE_META};PORT=18762"
        LABELS "e2e;topo-debug-serve;toolchain"
        TIMEOUT 30)

    # --ai-export + GET /templates/<name> + --once
    # serve smoke (label: topo-debug-serve). Mock-adapter backed; no LLVM.
    set(_SERVE_EXPORT_TEMPLATES "${CMAKE_CURRENT_SOURCE_DIR}/debug-serve/serve_export_templates_smoke.sh")
    add_test(NAME e2e.topo-debug-serve.export-templates
        COMMAND bash "${_SERVE_EXPORT_TEMPLATES}")
    set_tests_properties(e2e.topo-debug-serve.export-templates PROPERTIES
        ENVIRONMENT
            "TOPO_DEBUG_BIN=$<TARGET_FILE:topo-debug>;TOPO_DEBUG_MOCK=$<TARGET_FILE:topo-debug-mock-adapter>;DEBUG_META=${_SERVE_META};PORT=18764;AI_PORT=18765"
        LABELS "e2e;topo-debug-serve;toolchain"
        # 180s headroom: the json_ok helper now uses a cheap brace check
        # (no python3 fork), so local wall-clock is ~5s. CI runners can
        # still approach a 60s timeout — process spawning, many localhost
        # curls, and server-up polls add up — so keep 180s until the slow
        # path is diagnosed.
        TIMEOUT 180)
endif()

# topo-debug real-adapter + topo-profile end-to-end blocks live in
# debug-profile-e2e.cmake: they gate on per-language targets (topo-debug-*,
# tiny_*, spans_demo_*, topo-profile) that sibling packages define AFTER
# topo-core in the meta union build, and resolve fixtures via
# ${PROJECT_SOURCE_DIR}/topo-lang-* (the meta root). A standalone topo-core
# configure include()s them here — the gates self-skip because the lang
# targets are absent by design. The meta union build sets
# TOPO_META_DRIVING_TESTS and instead drives the module from a late hook,
# after every sibling package is added, so the gates see real targets and
# PROJECT_SOURCE_DIR is the meta root (see meta CMakeLists.txt).
if(NOT TOPO_META_DRIVING_TESTS)
    include(${CMAKE_CURRENT_LIST_DIR}/debug-profile-e2e.cmake)
endif()

# Note: the "Python3 found but topo-debug-python target missing" verification
# gap previously enforced here as e2e.topo-debug-python.MISSING was moved to
# topo-lang-python/test/CMakeLists.txt — the contract holder for "if you
# import me, I must provide topo-debug-python".

# Feature dimension labels
set_property(DIRECTORY APPEND PROPERTY
    TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/FeatureMap.cmake")
