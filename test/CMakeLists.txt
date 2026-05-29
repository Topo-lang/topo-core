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
        unit/TemplateEngineTest.cpp
        unit/PassReportsLoaderTest.cpp
        unit/EvaluatorStructTest.cpp
        unit/DistributionTest.cpp
        build/PassCategoryRegistryTest.cpp
    )
    # TopoBuildLib PUBLIC-propagates TopoLang → TopoCheck; each per-language
    # transpile lib (TopoCppTranspile / ...) PUBLIC-depends on TopoTranspile.
    # Explicit TopoCheck and TopoTranspile were the duplicates.
    target_link_libraries(topo-core-tests PRIVATE
        TopoTransform TopoDebug
        topo::lang-cpp::TopoCppTranspile
        topo::lang-rust::TopoRustTranspile
        topo::lang-java::TopoJavaTranspile
        topo::lang-python::TopoPythonTranspile
        TopoBuildLib TopoDistribution
        nlohmann_json::nlohmann_json
        GTest::gtest_main
    )
    target_compile_definitions(topo-core-tests PRIVATE
        TOPO_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        TOPO_SPEC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/language/spec"
    )
    topo_set_compiler_flags(topo-core-tests)
    topo_apply_std_pch(topo-core-tests)
    # DISCOVERY_TIMEOUT 30: the CMake default is 5s, too tight for a heavily
    # linked binary when post-link discovery competes with concurrent link
    # jobs on a loaded machine (issue
    # topo-lsp-server-tests-gtest-discovery-timeout-flake — project-wide).
    gtest_discover_tests(topo-core-tests
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "toolchain;core" TIMEOUT 120)

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
        set(_AUDIT_GATE_SH "${PROJECT_SOURCE_DIR}/${relative_path}")
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

# topo-debug-cpp real lldb adapter (label: topo-debug-cpp).
# Gated on TOPO_ENABLE_LLVM + non-Windows (lldb on Windows is shipped by
# Microsoft separately; the bundled LLVM dylib path covers macOS/Linux only).
# `tiny_matrix.cpp` defines `int matrix[16][16]` and sets matrix[5][7]=42;
# the breakpoint on the `int sentinel = 0;` line fires after that assignment
# completes, so the adapter reads matrix == one-hot.
if(TOPO_ENABLE_LLVM AND NOT WIN32 AND TARGET topo-debug-cpp)
    set(_TOPO_DEBUG_CPP "$<TARGET_FILE:topo-debug-cpp>")
    set(_TINY_MATRIX_FIXTURE "$<TARGET_FILE:tiny_matrix>")
    # Line 6 of tiny_matrix.cpp: `int sentinel = 0;`. Bumping the source
    # requires updating this number — kept here (not as a CMake variable) so
    # the test list reads as a literal demo invocation.
    add_test(NAME e2e.topo-debug-cpp.sum_one_hot
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(matrix)"
            --break tiny_matrix.cpp:6
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_MATRIX_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.sum_one_hot PROPERTIES
        PASS_REGULAR_EXPRESSION "^42\n"
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 90)

    add_test(NAME e2e.topo-debug-cpp.shape_one_hot
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(matrix)"
            --break tiny_matrix.cpp:6
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_MATRIX_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.shape_one_hot PROPERTIES
        PASS_REGULAR_EXPRESSION "^\\[16, 16\\]\n"
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 90)

    add_test(NAME e2e.topo-debug-cpp.dtype_one_hot
        COMMAND ${_TOPO_DEBUG_BIN} query "dtype(matrix)"
            --break tiny_matrix.cpp:6
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_MATRIX_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.dtype_one_hot PROPERTIES
        PASS_REGULAR_EXPRESSION "^i32\n"
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 90)

    # variable-name parameterisation via `--var`.
    # `tiny_vector.cpp` declares `double vec[8] = {0.5, 1.5, ..., 7.5}` and
    # the breakpoint on the `int sentinel = 0;` line (line 7 — the multi-line
    # header pushes sentinel one row past tiny_matrix.cpp's layout) fires once
    # the array is initialised. The CLI extracts `vec` from the query AST and
    # passes it to the adapter — no user flag duplication.
    set(_TINY_VECTOR_FIXTURE "$<TARGET_FILE:tiny_vector>")
    add_test(NAME e2e.topo-debug-cpp.var_named_vec_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(vec)"
            --break tiny_vector.cpp:7
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_VECTOR_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.var_named_vec_sum PROPERTIES
        # 0.5+1.5+...+7.5 = 32.0
        PASS_REGULAR_EXPRESSION "^32(\\.0+)?\n"
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 90)

    add_test(NAME e2e.topo-debug-cpp.var_named_vec_shape
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(vec)"
            --break tiny_vector.cpp:7
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_VECTOR_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.var_named_vec_shape PROPERTIES
        PASS_REGULAR_EXPRESSION "^\\[8\\]\n"
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 90)

    add_test(NAME e2e.topo-debug-cpp.var_named_vec_dtype
        COMMAND ${_TOPO_DEBUG_BIN} query "dtype(vec)"
            --break tiny_vector.cpp:7
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_VECTOR_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.var_named_vec_dtype PROPERTIES
        PASS_REGULAR_EXPRESSION "^f64\n"
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 90)

    # multi-variable adapter protocol. `tiny_multi.cpp`
    # has `int a[4] = {1..4}` (sum=10) and `int b[4] = {10..40 step 10}`
    # (sum=100) in the same frame. The CLI joins the AST's two distinct
    # identifiers, the adapter loops FindVariable twice, drains 2 pairs.
    # Line 10 is `int sentinel = 0;` — after both arrays are initialised.
    set(_TINY_MULTI_FIXTURE "$<TARGET_FILE:tiny_multi>")
    add_test(NAME e2e.topo-debug-cpp.multivar_sum_a_plus_sum_b
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(a) + sum(b)"
            --break tiny_multi.cpp:11
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_MULTI_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.multivar_sum_a_plus_sum_b PROPERTIES
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-multivar"
        TIMEOUT 90)

    add_test(NAME e2e.topo-debug-cpp.multivar_max_b_minus_max_a
        COMMAND ${_TOPO_DEBUG_BIN} query "max(b) - max(a)"
            --break tiny_multi.cpp:11
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_MULTI_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.multivar_max_b_minus_max_a PROPERTIES
        # 40 - 4 = 36
        PASS_REGULAR_EXPRESSION "^36\n"
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-multivar"
        TIMEOUT 90)

    add_test(NAME e2e.topo-debug-cpp.multivar_division
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(b) / sum(a)"
            --break tiny_multi.cpp:11
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_MULTI_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.multivar_division PROPERTIES
        # 100 / 10 = 10.0 (i/i in `/` always promotes to float)
        PASS_REGULAR_EXPRESSION "^10(\\.0+)?\n"
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-multivar"
        TIMEOUT 90)

    # Multi-var error path: reference a name not in the frame. Adapter
    # returns runtime exit (3), CLI bubbles it up.
    add_test(NAME e2e.topo-debug-cpp.multivar_unknown_var
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(a) + sum(zzz)"
            --break tiny_multi.cpp:11
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_MULTI_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.multivar_unknown_var PROPERTIES
        WILL_FAIL TRUE
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-multivar"
        TIMEOUT 90)

    # aggregate-leaf adapter emission.
    # `tiny_struct.cpp` declares `Particle p[4]` of `{float x,y,z}`; breakpoint
    # on `int sentinel = 0;` (line 15, after `volatile keep = ...`) fires once
    # the array is fully initialised. Built at -O2 -g so DWARF must retain
    # field offsets under optimization (day-1 smoke confirmed this on darwin).
    #
    # The Compute side does not yet parse v2 `struct` blocks for arithmetic
    # reductions, but `dtype(p)` and `shape(p)` only read the layout (no byte
    # interpretation), so they pass through cleanly and let us assert the
    # adapter's v2 wire output without needing a layout-only dump mode.
    set(_TINY_STRUCT_FIXTURE "$<TARGET_FILE:tiny_struct>")
    add_test(NAME e2e.topo-debug-cpp.struct_leaf_dtype
        COMMAND ${_TOPO_DEBUG_BIN} query "dtype(p)"
            --break tiny_struct.cpp:15
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_STRUCT_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.struct_leaf_dtype PROPERTIES
        PASS_REGULAR_EXPRESSION "^struct\n"
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-struct"
        TIMEOUT 90)

    add_test(NAME e2e.topo-debug-cpp.struct_leaf_shape
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(p)"
            --break tiny_struct.cpp:15
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_STRUCT_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.struct_leaf_shape PROPERTIES
        PASS_REGULAR_EXPRESSION "^\\[4\\]\n"
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-struct"
        TIMEOUT 90)

    # struct field reduction now works.
    # `sum(p.x)` projects the `x` column of `Particle p[4]` and folds the
    # 4 floats {1,4,7,10} via the strided fold path → 22.
    add_test(NAME e2e.topo-debug-cpp.struct_leaf_sum_x
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(p.x)"
            --break tiny_struct.cpp:15
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_STRUCT_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.struct_leaf_sum_x PROPERTIES
        PASS_REGULAR_EXPRESSION "^22\n"
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-struct"
        TIMEOUT 90)

    # `sum(p)` without a field selector still fails — reducing over a struct
    # array directly has no defined arithmetic. Day-2a pinned this as
    # "unsupported dtype 'struct'"; day-2b sharpened the error to name the
    # required path forward (`sum(p.<field>)`). The test stays a WILL_FAIL
    # so behaviour change here lands deliberately.
    add_test(NAME e2e.topo-debug-cpp.struct_leaf_sum_no_field
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(p)"
            --break tiny_struct.cpp:15
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_TINY_STRUCT_FIXTURE})
    set_tests_properties(e2e.topo-debug-cpp.struct_leaf_sum_no_field PROPERTIES
        WILL_FAIL TRUE
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-struct"
        TIMEOUT 90)

    # project-level end-to-end:
    #   1. CTest setup fixture runs `topo-build` in the copied project dir,
    #      producing both a debuggable `main` binary and `main.topo-dbg.json`
    #      (the view registry derived from main.topo).
    #   2. Three query tests depend on that fixture. Each calls
    #      `topo-debug query` against `main`; `topo-debug` auto-discovers
    #      `main.topo-dbg.json` next to the binary and rewrites view names
    #      `first_half` / `second_half` into the underlying `data[0..8]` /
    #      `data[8..16]` slices before driving the adapter.
    #
    # Line 13 of main.cpp is `int sentinel = 0;`, fired after data[16] is
    # populated 1..16. Sums: first_half 1..8 = 36, second_half 9..16 = 100,
    # full data 1..16 = 136. Shape of first_half = [8].
    set(_PROJECT_SIMPLE_DIR ${TOPO_DEBUG_CPP_PROJECT_SIMPLE_DIR})
    set(_TOPO_BUILD_BIN "$<TARGET_FILE:topo-build>")

    add_test(NAME e2e.topo-debug-cpp.project_simple.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_DIR})
    set_tests_properties(e2e.topo-debug-cpp.project_simple.build PROPERTIES
        FIXTURES_SETUP project_simple_built
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-cpp.project_simple.view_first_half_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(first_half)"
            --break main.cpp:13
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_PROJECT_SIMPLE_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_DIR})
    set_tests_properties(e2e.topo-debug-cpp.project_simple.view_first_half_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_built
        PASS_REGULAR_EXPRESSION "^36\n"
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 90)

    add_test(NAME e2e.topo-debug-cpp.project_simple.view_second_half_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(second_half)"
            --break main.cpp:13
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_PROJECT_SIMPLE_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_DIR})
    set_tests_properties(e2e.topo-debug-cpp.project_simple.view_second_half_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_built
        # 9+10+...+16 = 100
        PASS_REGULAR_EXPRESSION "^100\n"
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 90)

    add_test(NAME e2e.topo-debug-cpp.project_simple.view_first_half_shape
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(first_half)"
            --break main.cpp:13
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_PROJECT_SIMPLE_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_DIR})
    set_tests_properties(e2e.topo-debug-cpp.project_simple.view_first_half_shape PROPERTIES
        FIXTURES_REQUIRED project_simple_built
        PASS_REGULAR_EXPRESSION "^\\[8\\]\n"
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 90)

    # Back-compat: raw variable name (no view rewrite) still works against
    # the same binary so the auto-discovery code path doesn't break the
    # tiny_matrix/tiny_vector contract for projects that happen to have
    # both a .topo-dbg.json and an unrelated raw variable in scope.
    add_test(NAME e2e.topo-debug-cpp.project_simple.raw_data_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data)"
            --break main.cpp:13
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_PROJECT_SIMPLE_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_DIR})
    set_tests_properties(e2e.topo-debug-cpp.project_simple.raw_data_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_built
        PASS_REGULAR_EXPRESSION "^136\n"
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 90)

    # `summary` subcommand renders `summary_template`
    # against a live frame. After the batched-summary
    # refactor, all distinct placeholder vars resolve in a single adapter
    # spawn (was 4 — ~4x faster end-to-end).
    # At main.cpp:13, expected:  data sum=136 (halves 36 + 100; count=16)
    add_test(NAME e2e.topo-debug-cpp.project_simple.summary
        COMMAND ${_TOPO_DEBUG_BIN} summary "Buf"
            --break main.cpp:13
            --debug-meta ${_PROJECT_SIMPLE_DIR}/main.topo-dbg.json
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_PROJECT_SIMPLE_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_DIR})
    set_tests_properties(e2e.topo-debug-cpp.project_simple.summary PROPERTIES
        FIXTURES_REQUIRED project_simple_built
        PASS_REGULAR_EXPRESSION "^data sum=136 \\(halves 36 \\+ 100; count=16\\)\n"
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-summary"
        TIMEOUT 90)

    # Per-Pass sidecar verification (principle 19).
    #
    # The project_simple build above produces main.topo-passes/ alongside the
    # binary. This test sanity-checks the directory contents: every judging
    # Pass present with the required header keys + Pass-specific detail
    # fields; mechanical Passes correctly absent. Detail fields are mostly
    # empty here because the fixture is a no-op project — those richer
    # assertions live with the benchmark fixtures that actually exercise
    # each Pass's auto/force decision path.
    set(_PASS_SIDECAR_SCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/topo-passes-sidecar/verify_sidecar.sh")
    add_test(NAME e2e.topo-passes.project_simple.sidecar
        COMMAND bash "${_PASS_SIDECAR_SCRIPT}"
            "${_PROJECT_SIMPLE_DIR}/main.topo-passes"
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_DIR})
    set_tests_properties(e2e.topo-passes.project_simple.sidecar PROPERTIES
        FIXTURES_REQUIRED project_simple_built
        LABELS "e2e;topo-passes;toolchain"
        TIMEOUT 30)

    # `snapshot` capture + `query --replay` end-to-end.
    # Step A captures a TOPO_SNAP_V1 file from the live target using the
    # `Buf` symbol's distinct view containers (here: just `data`). Step B
    # replays the snapshot and evaluates `sum(data)` against the cached
    # bytes — no adapter spawn — and asserts the same `136` answer as the
    # adjacent project_simple.raw_data_sum test. Wired as fixtures so the
    # snapshot file is materialised once before the replay test runs.
    set(_PROJECT_SIMPLE_SNAP ${CMAKE_CURRENT_BINARY_DIR}/project_simple_buf.snap)
    add_test(NAME e2e.topo-debug-cpp.project_simple.snapshot_capture
        COMMAND ${_TOPO_DEBUG_BIN} snapshot "Buf"
            --break main.cpp:13
            --debug-meta ${_PROJECT_SIMPLE_DIR}/main.topo-dbg.json
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_PROJECT_SIMPLE_DIR}/main
            --out ${_PROJECT_SIMPLE_SNAP}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_DIR})
    set_tests_properties(e2e.topo-debug-cpp.project_simple.snapshot_capture PROPERTIES
        FIXTURES_REQUIRED project_simple_built
        FIXTURES_SETUP project_simple_snapshot
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-snapshot"
        TIMEOUT 90)

    add_test(NAME e2e.topo-debug-cpp.project_simple.snapshot_then_replay
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data)"
            --replay ${_PROJECT_SIMPLE_SNAP})
    set_tests_properties(e2e.topo-debug-cpp.project_simple.snapshot_then_replay PROPERTIES
        FIXTURES_REQUIRED "project_simple_built;project_simple_snapshot"
        PASS_REGULAR_EXPRESSION "^136\n"
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-snapshot"
        TIMEOUT 90)

    # `topo debug serve --target ... --adapter ...` end-to-end
    # against the real lldb adapter. The smoke test already covers the mock
    # path; this exercises the POSIX HTTP layer talking to a live debugger
    # for a complete user-facing serve flow. `--port 0` lets the OS pick a
    # free ephemeral port (announced in the listening line) so parallel
    # ctest runs don't collide.
    set(_SERVE_REAL ${CMAKE_CURRENT_SOURCE_DIR}/debug-serve/serve_real_target.sh)
    add_test(NAME e2e.topo-debug-serve.real_target_cpp
        COMMAND bash "${_SERVE_REAL}"
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_DIR})
    set_tests_properties(e2e.topo-debug-serve.real_target_cpp PROPERTIES
        FIXTURES_REQUIRED project_simple_built
        ENVIRONMENT
            "TOPO_DEBUG_BIN=$<TARGET_FILE:topo-debug>;ADAPTER=${_TOPO_DEBUG_CPP};TARGET_BIN=${_PROJECT_SIMPLE_DIR}/main;DEBUG_META=${_PROJECT_SIMPLE_DIR}/main.topo-dbg.json;QUERY_EXPR=sum(data);QUERY_SITE=main.cpp:13;EXPECT_RESULT=136"
        LABELS "e2e;topo-debug-serve;topo-debug-cpp;toolchain"
        TIMEOUT 60)

    # WebSocket-backed /query over /ws. Same real-target setup
    # as the POST counterpart above; the python3 stdlib client (no
    # `websockets` install) drives one masked frame and verifies the
    # server's unmasked reply.
    set(_SERVE_WS ${CMAKE_CURRENT_SOURCE_DIR}/debug-serve/serve_ws_query.sh)
    add_test(NAME e2e.topo-debug-serve.ws_query_cpp
        COMMAND bash "${_SERVE_WS}"
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_DIR})
    set_tests_properties(e2e.topo-debug-serve.ws_query_cpp PROPERTIES
        FIXTURES_REQUIRED project_simple_built
        ENVIRONMENT
            "TOPO_DEBUG_BIN=$<TARGET_FILE:topo-debug>;ADAPTER=${_TOPO_DEBUG_CPP};TARGET_BIN=${_PROJECT_SIMPLE_DIR}/main;DEBUG_META=${_PROJECT_SIMPLE_DIR}/main.topo-dbg.json;QUERY_EXPR=sum(data);QUERY_SITE=main.cpp:13;EXPECT_RESULT=136"
        LABELS "e2e;topo-debug-serve;topo-debug-cpp;toolchain"
        TIMEOUT 60)

    # project-level *multi-host-var*
    # summary. Symbol TwoArrays has two distinct host variables (`a` and
    # `b`) and a summary template referencing both: `{sum(a)}`, `{sum(b)}`,
    # `{sum(a) + sum(b)}`, `{max(b) - max(a)}`. Resolving it spawns the
    # adapter exactly once with `--var a,b`; previously this would have
    # been impossible (the earlier single-var adapter) or required 4 spawns.
    # At main.cpp:17, expected: a=10, b=100, total=110, span=36
    set(_PROJECT_MULTI_DIR ${TOPO_DEBUG_CPP_PROJECT_MULTI_DIR})
    add_test(NAME e2e.topo-debug-cpp.project_multi.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_MULTI_DIR})
    set_tests_properties(e2e.topo-debug-cpp.project_multi.build PROPERTIES
        FIXTURES_SETUP project_multi_built
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-cpp.project_multi.summary
        COMMAND ${_TOPO_DEBUG_BIN} summary "TwoArrays"
            --break main.cpp:17
            --debug-meta ${_PROJECT_MULTI_DIR}/main.topo-dbg.json
            --adapter ${_TOPO_DEBUG_CPP}
            --target ${_PROJECT_MULTI_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_MULTI_DIR})
    set_tests_properties(e2e.topo-debug-cpp.project_multi.summary PROPERTIES
        FIXTURES_REQUIRED project_multi_built
        PASS_REGULAR_EXPRESSION "^a=10, b=100, total=110, span=36\n"
        LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-multivar;topo-debug-summary"
        TIMEOUT 90)

    # project_chart fixture — debug-view build smoke. `topo build` parses
    # main.topo (a ChartData debug entry with two sliced parallel views +
    # a summary template) and emits main.topo-dbg.json. This fixture
    # once drove a render-fn dlopen + POST /render path; that test, its
    # render_chart_smoke.sh script, and the render-fns/ build infra were
    # all removed because web-rendering is deliberately unimplemented
    # (SemanticAnalyzer rejects `render` blocks).
    set(_PROJECT_CHART_DIR ${TOPO_DEBUG_CPP_PROJECT_CHART_DIR})
    add_test(NAME e2e.topo-debug-cpp.project_chart.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_CHART_DIR})
    set_tests_properties(e2e.topo-debug-cpp.project_chart.build PROPERTIES
        LABELS "e2e;topo-debug-cpp;toolchain"
        TIMEOUT 60)
endif()

# ===========================================================================
# native-lldb formatter — the zero-install courtesy formatter.
#
# Two distinct verifications:
#   * formatter_unit — the formatter's pure-Python core (dbg.json discovery,
#     symbol matching, summary rendering, graceful degradation) exercised
#     with stub SB* objects. Runs WITHOUT lldb, so it stays green in this
#     environment and is the load-bearing regression guard for the logic.
#   * formatter_e2e  — the in-debugger acceptance: `lldb -b -o 'frame
#     variable mesh'` with vs. without the formatter `.lldbinit`. Requires
#     an lldb built with a Python script interpreter. A scripting-enabled
#     lldb is probed at configure time and used when present (the e2e then
#     actually runs and asserts); otherwise it falls back to the
#     script-free bundled topo-llvm/llvm-dev lldb and the driver
#     self-detects + emits `SKIPPED: ...` (honest skip, not a phantom
#     pass) — the bundled lldb has no Python scripting linked in.
# ===========================================================================
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND AND NOT WIN32)
    set(_FMT_UNIT
        "${PROJECT_SOURCE_DIR}/topo-lang-cpp/topo-debug/test/formatter_unit_test.py")
    set(_FMT_E2E
        "${PROJECT_SOURCE_DIR}/topo-lang-cpp/topo-debug/test/formatter_e2e_driver.py")
    set(_FMT_CPP_PY
        "${PROJECT_SOURCE_DIR}/topo-lang-cpp/topo-debug/lldb_formatter.py")
    set(_FMT_RUST_PY
        "${PROJECT_SOURCE_DIR}/topo-lang-rust/topo-debug/lldb_formatter.py")

    # Unit tests need a real *.topo-dbg.json to parse. Build the
    # project_formatter fixture once as a setup fixture so both the C++ and
    # Rust unit runs (the formatter core is host-agnostic) read the same
    # canonical artifact emitted by `topo build`.
    # TOPO_DEBUG_CPP_PROJECT_FORMATTER_DIR is only populated when
    # TOPO_ENABLE_LLVM=ON (topo-lang-cpp/topo-debug returns early
    # otherwise), so the fixture-build test would fail at run-time with
    # "no Topo.toml found" in LLVM-off builds. Gate the registration.
    if(TARGET topo-build AND TOPO_ENABLE_LLVM)
        set(_FMT_DIR ${TOPO_DEBUG_CPP_PROJECT_FORMATTER_DIR})
        add_test(NAME e2e.topo-debug-cpp.formatter_fixture_build
            COMMAND $<TARGET_FILE:topo-build>
            WORKING_DIRECTORY ${_FMT_DIR})
        set_tests_properties(e2e.topo-debug-cpp.formatter_fixture_build
            PROPERTIES
                FIXTURES_SETUP formatter_fixture_built
                LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-formatter"
                TIMEOUT 60)

        add_test(NAME e2e.topo-debug-cpp.formatter_unit
            COMMAND ${Python3_EXECUTABLE} ${_FMT_UNIT}
                    ${_FMT_CPP_PY} ${_FMT_DIR}/main.topo-dbg.json)
        set_tests_properties(e2e.topo-debug-cpp.formatter_unit PROPERTIES
            FIXTURES_REQUIRED formatter_fixture_built
            LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-formatter"
            TIMEOUT 90)

        add_test(NAME e2e.topo-debug-rust.formatter_unit
            COMMAND ${Python3_EXECUTABLE} ${_FMT_UNIT}
                    ${_FMT_RUST_PY} ${_FMT_DIR}/main.topo-dbg.json)
        set_tests_properties(e2e.topo-debug-rust.formatter_unit PROPERTIES
            FIXTURES_REQUIRED formatter_fixture_built
            LABELS "e2e;topo-debug-rust;toolchain;topo-debug-formatter"
            TIMEOUT 30)
    endif()

    # Acceptance criterion 2 — `topo init` for C++ AND Rust emits a
    # `.lldbinit` that imports the scaffold's lldb_formatter.py. Verified
    # against the real topo-init binary (the providers' generateLldbInit /
    # lldbFormatterScript flow end-to-end).
    if(TARGET topo-init)
        set(_INIT_LLDB_DRIVER
            "${PROJECT_SOURCE_DIR}/topo-lang-cpp/topo-debug/test/init_lldbinit_driver.py")
        add_test(NAME e2e.topo-debug-cpp.init_lldbinit
            COMMAND ${Python3_EXECUTABLE} ${_INIT_LLDB_DRIVER}
                    $<TARGET_FILE:topo-init> cpp main.cpp)
        set_tests_properties(e2e.topo-debug-cpp.init_lldbinit PROPERTIES
            LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-formatter"
            TIMEOUT 90)
        add_test(NAME e2e.topo-debug-rust.init_lldbinit
            COMMAND ${Python3_EXECUTABLE} ${_INIT_LLDB_DRIVER}
                    $<TARGET_FILE:topo-init> rust main.rs)
        set_tests_properties(e2e.topo-debug-rust.init_lldbinit PROPERTIES
            LABELS "e2e;topo-debug-rust;toolchain;topo-debug-formatter"
            TIMEOUT 30)
    endif()

    # In-debugger e2e — gated exactly like the rest of the topo-debug-cpp
    # adapter suite (TOPO_ENABLE_LLVM + non-Windows + the adapter target,
    # which implies a bundled liblldb). The driver additionally skips
    # cleanly when that lldb has no Python script interpreter.
    if(TOPO_ENABLE_LLVM AND NOT WIN32 AND TARGET topo-debug-cpp AND TARGET topo-build)
        # The bundled topo-llvm/llvm-dev lldb is script-free, so the e2e
        # could only ever skip. Prefer a Python-scripting-enabled lldb
        # (probed at configure time) so this acceptance actually runs
        # where one exists; fall back to the bundled lldb otherwise — the
        # driver still self-skips cleanly there (honest skip, never a
        # phantom pass). See bundled-lldb-no-python-scripting issue.
        set(_BUNDLED_LLDB "${PROJECT_SOURCE_DIR}/topo-llvm/llvm-dev/bin/lldb")
        set(_FMT_LLDB "${_BUNDLED_LLDB}")
        find_program(_SYS_LLDB NAMES lldb)
        if(_SYS_LLDB)
            execute_process(
                COMMAND "${_SYS_LLDB}" -b -o "script 1+1"
                OUTPUT_VARIABLE _SYS_LLDB_OUT
                ERROR_VARIABLE  _SYS_LLDB_ERR
                RESULT_VARIABLE _SYS_LLDB_RC
                TIMEOUT 30)
            if(_SYS_LLDB_RC EQUAL 0
               AND NOT _SYS_LLDB_OUT MATCHES "unavailable"
               AND NOT _SYS_LLDB_ERR MATCHES "unavailable")
                set(_FMT_LLDB "${_SYS_LLDB}")
                message(STATUS
                    "formatter_e2e: using scripting-enabled lldb ${_SYS_LLDB}")
            else()
                message(STATUS
                    "formatter_e2e: ${_SYS_LLDB} has no Python scripting; "
                    "using bundled lldb (driver self-skips)")
            endif()
        else()
            message(STATUS
                "formatter_e2e: no system lldb found; using bundled lldb "
                "(driver self-skips)")
        endif()
        add_test(NAME e2e.topo-debug-cpp.formatter_e2e
            COMMAND ${Python3_EXECUTABLE} ${_FMT_E2E}
                    ${_FMT_LLDB}
                    $<TARGET_FILE:topo-build>
                    ${PROJECT_SOURCE_DIR}/topo-lang-cpp/topo-debug/test/fixtures/project_formatter
                    ${_FMT_CPP_PY}
                    mesh
                    25
                    "<Mesh: 6 verts, 9 tris>")
        set_tests_properties(e2e.topo-debug-cpp.formatter_e2e PROPERTIES
            SKIP_REGULAR_EXPRESSION "SKIPPED:"
            LABELS "e2e;topo-debug-cpp;toolchain;topo-debug-formatter"
            TIMEOUT 120)
    endif()
else()
    message(STATUS
        "formatter tests: skipped (Python3 interpreter not found "
        "or Windows). The formatter scripts still ship; only the CTest "
        "coverage is gated.")
endif()

# topo-debug-java JDWP/JDI adapter against Java fixtures
# (label: topo-debug-java). Java parallel of the C++ adapter above: the same
# wire protocol, the same CLI shape, but the host debugger is JDI (jdk.jdi
# module, ships with every JDK) so there's no LLVM gate — only a working JDK
# (TOPO_GRADLE_JAVA_HOME). The fixture is `TinyMatrix.java` with
# `int[][] matrix = new int[16][16]; matrix[5][7] = 42;` and the breakpoint
# fires at the `int sentinel = 0;` line after both assignments complete.
if(TARGET topo-debug-java AND NOT WIN32)
    set(_TOPO_DEBUG_JAVA ${TOPO_DEBUG_JAVA_LAUNCHER})
    set(_TINY_MATRIX_JAVA_LAUNCHER ${TOPO_DEBUG_JAVA_TINY_MATRIX_LAUNCHER})

    # `--target` is a shell launcher that exec's the JVM with JDWP listening
    # on a random port; the adapter parses the port from the launcher's stderr
    # ("Listening for transport ... at: <port>") then JDI-attaches.
    #
    # Line 6 of TinyMatrix.java: `int sentinel = 0;` — kept literal so the
    # CMake-to-source binding is obvious to a fixture-edit bisect.
    add_test(NAME e2e.topo-debug-java.sum_one_hot
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(matrix)"
            --break TinyMatrix.java:6
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_TINY_MATRIX_JAVA_LAUNCHER})
    set_tests_properties(e2e.topo-debug-java.sum_one_hot PROPERTIES
        PASS_REGULAR_EXPRESSION "^42\n"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-java.shape_one_hot
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(matrix)"
            --break TinyMatrix.java:6
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_TINY_MATRIX_JAVA_LAUNCHER})
    set_tests_properties(e2e.topo-debug-java.shape_one_hot PROPERTIES
        PASS_REGULAR_EXPRESSION "^\\[16, 16\\]\n"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-java.dtype_one_hot
        COMMAND ${_TOPO_DEBUG_BIN} query "dtype(matrix)"
            --break TinyMatrix.java:6
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_TINY_MATRIX_JAVA_LAUNCHER})
    set_tests_properties(e2e.topo-debug-java.dtype_one_hot PROPERTIES
        PASS_REGULAR_EXPRESSION "^i32\n"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 60)
endif()

# Java topo-debug *project-level* fixtures.
#
# Counterpart of the cpp project_simple / project_multi block above + the
# rust D1 fixtures lower in this file. Unlike TinyMatrix (compiled directly
# by the topo-debug-java CMakeLists.txt with javac), these fixtures run the
# full `topo build` → topo-build-jvm-java → javac -g → bytecode transforms
# → JAR pipeline. Each fixture is staged at configure time in
# topo-lang-java/topo-debug/CMakeLists.txt (TOPO_DEBUG_JAVA_PROJECT_*_DIR
# vars). A per-fixture launcher script bakes the JDWP listen flags + the
# `java -jar <output>.jar` invocation; the adapter parses the JVM's
# "Listening for transport ... at: <port>" stderr line and JDI-attaches.
#
# Sentinel-line choice: the breakpoint sits on a literal `int sentinel = 0;`
# line (not `println` / not string concat) to avoid the JDWP invokedynamic
# quirk where `bpEvent.location()` reports the right PC but `thread.frame(0)`
# is still inside `java.lang.invoke.StringConcatFactory`. The adapter walks
# the stack to find the matching frame (see Main.java:691-705) but keeping
# the sentinel clean makes test failure modes obvious.
#
# Runtime JDK gate: the transform JAR (topo-transform.jar) is compiled by
# Gradle's auto-toolchain at JDK 21 (`languageVersion.set(of(21))`), so
# executing the full `topo build` pipeline needs a JRE ≥21.
# topo-debug-java/CMakeLists.txt probes for one and sets
# TOPO_DEBUG_JAVA_RUNTIME_HOME — these tests gate on it.
if(TARGET topo-debug-java AND TOPO_DEBUG_JAVA_RUNTIME_HOME AND NOT WIN32)
    set(_TOPO_DEBUG_JAVA ${TOPO_DEBUG_JAVA_LAUNCHER})
    set(_TOPO_BUILD_BIN "$<TARGET_FILE:topo-build>")

    # project_simple — single int[8] Buf with two literal-bounded halves +
    # summary + snapshot/replay. Sentinel breakpoint is Main.java:27.
    #   sum(first_half)  = 1+2+3+4         = 10
    #   sum(second_half) = 10+20+30+40     = 100
    #   sum(data)        = 110
    #   shape(first_half)                  = [4]
    set(_PROJECT_SIMPLE_JAVA_DIR ${TOPO_DEBUG_JAVA_PROJECT_SIMPLE_DIR})
    set(_PROJECT_SIMPLE_JAVA_LAUNCHER ${TOPO_DEBUG_JAVA_PROJECT_SIMPLE_LAUNCHER})

    # Build via the standard topo-build flow. `JAVA_HOME=${TOPO_GRADLE_JAVA_HOME}`
    # so JavaDriver's `resolveJavaTool("javac", ...)` falls back to env when
    # backendExtras.javaHome is empty (topo-build doesn't populate it).
    add_test(NAME e2e.topo-debug-java.project_simple.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_simple.build PROPERTIES
        FIXTURES_SETUP project_simple_java_built
        ENVIRONMENT "JAVA_HOME=${TOPO_DEBUG_JAVA_RUNTIME_HOME}"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 120)

    # All query/summary/snapshot tests below pass `--debug-meta` explicitly
    # because the produced JAR is `main.jar` while `--target` is a sibling
    # launcher script (`run_project_simple`). Auto-discovery looks for
    # `<target>.topo-dbg.json`, but the actual file is `main.jar.topo-dbg.json`
    # one level removed from the launcher path.
    set(_PROJECT_SIMPLE_JAVA_DBG ${_PROJECT_SIMPLE_JAVA_DIR}/main.jar.topo-dbg.json)

    add_test(NAME e2e.topo-debug-java.project_simple.view_first_half_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(first_half)"
            --break Main.java:27
            --debug-meta ${_PROJECT_SIMPLE_JAVA_DBG}
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_PROJECT_SIMPLE_JAVA_LAUNCHER}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_simple.view_first_half_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_java_built
        PASS_REGULAR_EXPRESSION "^10\n"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-java.project_simple.view_second_half_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(second_half)"
            --break Main.java:27
            --debug-meta ${_PROJECT_SIMPLE_JAVA_DBG}
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_PROJECT_SIMPLE_JAVA_LAUNCHER}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_simple.view_second_half_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_java_built
        PASS_REGULAR_EXPRESSION "^100\n"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-java.project_simple.view_first_half_shape
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(first_half)"
            --break Main.java:27
            --debug-meta ${_PROJECT_SIMPLE_JAVA_DBG}
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_PROJECT_SIMPLE_JAVA_LAUNCHER}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_simple.view_first_half_shape PROPERTIES
        FIXTURES_REQUIRED project_simple_java_built
        PASS_REGULAR_EXPRESSION "^\\[4\\]\n"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 60)

    # Back-compat: raw variable name (no view rewrite) still works against
    # the same binary — the dbg.json + raw-var coexistence doesn't break the
    # TinyMatrix contract for projects that happen to have both.
    add_test(NAME e2e.topo-debug-java.project_simple.raw_data_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data)"
            --break Main.java:27
            --debug-meta ${_PROJECT_SIMPLE_JAVA_DBG}
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_PROJECT_SIMPLE_JAVA_LAUNCHER}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_simple.raw_data_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_java_built
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 60)

    # `summary` subcommand renders `summary_template` against a live frame.
    # Expected: `data sum=110 (halves 10 + 100; count=8)`.
    add_test(NAME e2e.topo-debug-java.project_simple.summary
        COMMAND ${_TOPO_DEBUG_BIN} summary "Buf"
            --break Main.java:27
            --debug-meta ${_PROJECT_SIMPLE_JAVA_DBG}
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_PROJECT_SIMPLE_JAVA_LAUNCHER}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_simple.summary PROPERTIES
        FIXTURES_REQUIRED project_simple_java_built
        PASS_REGULAR_EXPRESSION "^data sum=110 \\(halves 10 \\+ 100; count=8\\)\n"
        LABELS "e2e;topo-debug-java;toolchain;topo-debug-summary"
        TIMEOUT 60)

    # `snapshot` capture + `query --replay` end-to-end. Step A captures a
    # TOPO_SNAP_V1 file from the live target. Step B replays the snapshot
    # (no adapter spawn) and asserts the same `110` answer.
    set(_PROJECT_SIMPLE_JAVA_SNAP
        ${CMAKE_CURRENT_BINARY_DIR}/project_simple_java_buf.snap)
    add_test(NAME e2e.topo-debug-java.project_simple.snapshot_capture
        COMMAND ${_TOPO_DEBUG_BIN} snapshot "Buf"
            --break Main.java:27
            --debug-meta ${_PROJECT_SIMPLE_JAVA_DBG}
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_PROJECT_SIMPLE_JAVA_LAUNCHER}
            --out ${_PROJECT_SIMPLE_JAVA_SNAP}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_simple.snapshot_capture PROPERTIES
        FIXTURES_REQUIRED project_simple_java_built
        FIXTURES_SETUP project_simple_java_snapshot
        LABELS "e2e;topo-debug-java;toolchain;topo-debug-snapshot"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-java.project_simple.snapshot_then_replay
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data)"
            --replay ${_PROJECT_SIMPLE_JAVA_SNAP})
    set_tests_properties(e2e.topo-debug-java.project_simple.snapshot_then_replay PROPERTIES
        FIXTURES_REQUIRED "project_simple_java_built;project_simple_java_snapshot"
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-java;toolchain;topo-debug-snapshot"
        TIMEOUT 30)

    # project_multi — two int[4] locals in scope, summary template references
    # both. Sentinel breakpoint is Main.java:20.
    #   sum(a)              = 10
    #   sum(b)              = 100
    #   sum(a)+sum(b)       = 110
    #   max(b)-max(a)       = 36
    set(_PROJECT_MULTI_JAVA_DIR ${TOPO_DEBUG_JAVA_PROJECT_MULTI_DIR})
    set(_PROJECT_MULTI_JAVA_LAUNCHER ${TOPO_DEBUG_JAVA_PROJECT_MULTI_LAUNCHER})
    set(_PROJECT_MULTI_JAVA_DBG ${_PROJECT_MULTI_JAVA_DIR}/main.jar.topo-dbg.json)

    add_test(NAME e2e.topo-debug-java.project_multi.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_MULTI_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_multi.build PROPERTIES
        FIXTURES_SETUP project_multi_java_built
        ENVIRONMENT "JAVA_HOME=${TOPO_DEBUG_JAVA_RUNTIME_HOME}"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 120)

    add_test(NAME e2e.topo-debug-java.project_multi.sum_a
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(a)"
            --break Main.java:20
            --debug-meta ${_PROJECT_MULTI_JAVA_DBG}
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_PROJECT_MULTI_JAVA_LAUNCHER}
        WORKING_DIRECTORY ${_PROJECT_MULTI_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_multi.sum_a PROPERTIES
        FIXTURES_REQUIRED project_multi_java_built
        PASS_REGULAR_EXPRESSION "^10\n"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-java.project_multi.sum_b
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(b)"
            --break Main.java:20
            --debug-meta ${_PROJECT_MULTI_JAVA_DBG}
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_PROJECT_MULTI_JAVA_LAUNCHER}
        WORKING_DIRECTORY ${_PROJECT_MULTI_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_multi.sum_b PROPERTIES
        FIXTURES_REQUIRED project_multi_java_built
        PASS_REGULAR_EXPRESSION "^100\n"
        LABELS "e2e;topo-debug-java;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-java.project_multi.sum_a_plus_b
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(a) + sum(b)"
            --break Main.java:20
            --debug-meta ${_PROJECT_MULTI_JAVA_DBG}
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_PROJECT_MULTI_JAVA_LAUNCHER}
        WORKING_DIRECTORY ${_PROJECT_MULTI_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_multi.sum_a_plus_b PROPERTIES
        FIXTURES_REQUIRED project_multi_java_built
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-java;toolchain;topo-debug-multivar"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-java.project_multi.summary
        COMMAND ${_TOPO_DEBUG_BIN} summary "TwoArrays"
            --break Main.java:20
            --debug-meta ${_PROJECT_MULTI_JAVA_DBG}
            --adapter ${_TOPO_DEBUG_JAVA}
            --target ${_PROJECT_MULTI_JAVA_LAUNCHER}
        WORKING_DIRECTORY ${_PROJECT_MULTI_JAVA_DIR})
    set_tests_properties(e2e.topo-debug-java.project_multi.summary PROPERTIES
        FIXTURES_REQUIRED project_multi_java_built
        PASS_REGULAR_EXPRESSION "^a=10, b=100, total=110, span=36\n"
        LABELS "e2e;topo-debug-java;toolchain;topo-debug-multivar;topo-debug-summary"
        TIMEOUT 60)
endif()

# topo-debug-rust real lldb adapter against Rust binaries
# (label: topo-debug-rust). Same liblldb adapter source as topo-debug-cpp; the
# target is a small Rust executable built by rustc -g. Gated on
# TOPO_ENABLE_LLVM + non-Windows + the rustc-built fixture + the adapter
# binary. tiny_vector.rs declares `let vec: [f64; 8] = [0.5..7.5]`, calls
# `std::hint::black_box(&vec)` to force eager stack materialisation, and
# the breakpoint on the `let sentinel` line (line 19 — see the fixture's
# header comment for why this is not line 10) fires once `vec` is on the
# stack.
if(TOPO_ENABLE_LLVM AND NOT WIN32 AND TARGET topo-debug-rust AND TARGET tiny_vector_rs)
    set(_TOPO_DEBUG_RUST "$<TARGET_FILE:topo-debug-rust>")
    get_target_property(_TINY_VECTOR_RS_FIXTURE tiny_vector_rs TOPO_FIXTURE_PATH)

    # Line numbers come from topo-lang-rust/topo-debug/test/fixtures/
    # tiny_vector.rs. Line 10 is `let sentinel: i32 = 0;` — after the array
    # initialisation completes. Keep this literal here so the binding between
    # CMake and source is obvious to anyone bisecting a fixture edit.
    add_test(NAME e2e.topo-debug-rust.sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(vec)"
            --break tiny_vector.rs:19
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_TINY_VECTOR_RS_FIXTURE})
    set_tests_properties(e2e.topo-debug-rust.sum PROPERTIES
        # 0.5+1.5+...+7.5 = 32.0
        PASS_REGULAR_EXPRESSION "^32(\\.0+)?\n"
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 30)

    add_test(NAME e2e.topo-debug-rust.shape
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(vec)"
            --break tiny_vector.rs:19
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_TINY_VECTOR_RS_FIXTURE})
    set_tests_properties(e2e.topo-debug-rust.shape PROPERTIES
        PASS_REGULAR_EXPRESSION "^\\[8\\]\n"
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 30)

    add_test(NAME e2e.topo-debug-rust.dtype
        COMMAND ${_TOPO_DEBUG_BIN} query "dtype(vec)"
            --break tiny_vector.rs:19
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_TINY_VECTOR_RS_FIXTURE})
    set_tests_properties(e2e.topo-debug-rust.dtype PROPERTIES
        PASS_REGULAR_EXPRESSION "^f64\n"
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 30)
endif()

# Rust multi-variable adapter parity. Same protocol as
# the cpp multivar_* tests; verifies that the shared liblldb adapter source
# (topo-lang-cpp/topo-debug/adapter.cpp) also handles `--var a,b` against a
# rustc-emitted DWARF binary. Line numbers come from tiny_multi.rs (the
# sentinel `let sentinel: i32 = 0;` line — see the fixture header for why
# black_box() is mandatory before that point).
if(TOPO_ENABLE_LLVM AND NOT WIN32 AND TARGET topo-debug-rust AND TARGET tiny_multi_rs)
    set(_TOPO_DEBUG_RUST "$<TARGET_FILE:topo-debug-rust>")
    get_target_property(_TINY_MULTI_RS_FIXTURE tiny_multi_rs TOPO_FIXTURE_PATH)

    add_test(NAME e2e.topo-debug-rust.multivar_sum_a_plus_sum_b
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(a) + sum(b)"
            --break tiny_multi.rs:22
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_TINY_MULTI_RS_FIXTURE})
    set_tests_properties(e2e.topo-debug-rust.multivar_sum_a_plus_sum_b PROPERTIES
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-rust;toolchain;topo-debug-multivar"
        TIMEOUT 30)

    add_test(NAME e2e.topo-debug-rust.multivar_division
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(b) / sum(a)"
            --break tiny_multi.rs:22
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_TINY_MULTI_RS_FIXTURE})
    set_tests_properties(e2e.topo-debug-rust.multivar_division PROPERTIES
        PASS_REGULAR_EXPRESSION "^10(\\.0+)?\n"
        LABELS "e2e;topo-debug-rust;toolchain;topo-debug-multivar"
        TIMEOUT 30)

    # `topo debug serve` against the rust adapter. Uses the
    # tiny_multi fixture (a=[1..4], b=[10..40]) and asks for `sum(a) + sum(b)`
    # = 110. A near-empty companion debug-meta lives next to the .rs source
    # (rustc fixtures don't run topo-build, so there is no auto-generated
    # *.topo-dbg.json — the serve CLI requires one even when the query uses
    # raw host variables only).
    set(_RUST_FIXTURE_META
        "${PROJECT_SOURCE_DIR}/topo-lang-rust/topo-debug/test/fixtures/tiny_multi.topo-dbg.json")
    add_test(NAME e2e.topo-debug-serve.real_target_rust
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/debug-serve/serve_real_target.sh")
    set_tests_properties(e2e.topo-debug-serve.real_target_rust PROPERTIES
        ENVIRONMENT
            "TOPO_DEBUG_BIN=$<TARGET_FILE:topo-debug>;ADAPTER=${_TOPO_DEBUG_RUST};TARGET_BIN=${_TINY_MULTI_RS_FIXTURE};DEBUG_META=${_RUST_FIXTURE_META};QUERY_EXPR=sum(a) + sum(b);QUERY_SITE=tiny_multi.rs:22;EXPECT_RESULT=110"
        LABELS "e2e;topo-debug-serve;topo-debug-rust;toolchain;topo-debug-multivar"
        TIMEOUT 60)

    # WS parity for the rust path — exercises the same /ws handler against a
    # rustc-emitted DWARF binary so a regression in either lldb plumbing or
    # the WebSocket frame codec lights up on both adapter sources.
    add_test(NAME e2e.topo-debug-serve.ws_query_rust
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/debug-serve/serve_ws_query.sh")
    set_tests_properties(e2e.topo-debug-serve.ws_query_rust PROPERTIES
        ENVIRONMENT
            "TOPO_DEBUG_BIN=$<TARGET_FILE:topo-debug>;ADAPTER=${_TOPO_DEBUG_RUST};TARGET_BIN=${_TINY_MULTI_RS_FIXTURE};DEBUG_META=${_RUST_FIXTURE_META};QUERY_EXPR=sum(a) + sum(b);QUERY_SITE=tiny_multi.rs:22;EXPECT_RESULT=110"
        LABELS "e2e;topo-debug-serve;topo-debug-rust;toolchain;topo-debug-multivar"
        TIMEOUT 60)
endif()

# Project-level Rust e2e fixtures — full topo-build flow + summary +
# snapshot, mirroring the cpp project_simple / project_multi coverage
# above. Each fixture is staged at configure time in
# topo-lang-rust/topo-debug/test/CMakeLists.txt (TOPO_DEBUG_RUST_PROJECT_*_DIR
# vars) and exercises the same declaration-driven path the cpp side does:
# `topo build` writes main.topo-dbg.json (the view registry) and links a
# debuggable binary, then `topo debug` auto-discovers the dbg.json next to
# ./main and rewrites view names into the underlying container slices
# before driving the lldb adapter.
#
# Breakpoint lines target a *read of the host array inside the trailing
# summation loop* rather than a "let sentinel = 0" line because rustc at
# -Copt-level=2 narrows DWARF scope ranges aggressively — between the
# declaration and the next observable use the locals are reported "not
# in scope" even with intervening `black_box(&data)` (cpp doesn't have
# this quirk so its sibling fixtures use a literal sentinel line).
if(TOPO_ENABLE_LLVM AND NOT WIN32 AND TARGET topo-debug-rust)
    set(_TOPO_DEBUG_RUST "$<TARGET_FILE:topo-debug-rust>")
    set(_TOPO_BUILD_BIN "$<TARGET_FILE:topo-build>")

    # project_simple — single-array Buf with two literal-bounded halves +
    # summary + snapshot/replay. Expected at main.rs:31 (`let v = data[i]`):
    #   sum(first_half)  = 1+2+3+4         = 10
    #   sum(second_half) = 10+20+30+40     = 100
    #   sum(data)        = 110
    #   shape(first_half)                  = [4]
    set(_PROJECT_SIMPLE_RS_DIR ${TOPO_DEBUG_RUST_PROJECT_SIMPLE_DIR})

    add_test(NAME e2e.topo-debug-rust.project_simple.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_simple.build PROPERTIES
        FIXTURES_SETUP project_simple_rs_built
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 120)

    add_test(NAME e2e.topo-debug-rust.project_simple.view_first_half_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(first_half)"
            --break main.rs:31
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_PROJECT_SIMPLE_RS_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_simple.view_first_half_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_rs_built
        PASS_REGULAR_EXPRESSION "^10\n"
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 30)

    add_test(NAME e2e.topo-debug-rust.project_simple.view_second_half_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(second_half)"
            --break main.rs:31
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_PROJECT_SIMPLE_RS_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_simple.view_second_half_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_rs_built
        PASS_REGULAR_EXPRESSION "^100\n"
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 30)

    add_test(NAME e2e.topo-debug-rust.project_simple.view_first_half_shape
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(first_half)"
            --break main.rs:31
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_PROJECT_SIMPLE_RS_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_simple.view_first_half_shape PROPERTIES
        FIXTURES_REQUIRED project_simple_rs_built
        PASS_REGULAR_EXPRESSION "^\\[4\\]\n"
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 30)

    # Back-compat: raw variable name (no view rewrite) still works against
    # the same binary so the auto-discovery code path doesn't break the
    # tiny_vector_rs / tiny_multi_rs contract for projects that happen to
    # have both a .topo-dbg.json and an unrelated raw variable in scope.
    add_test(NAME e2e.topo-debug-rust.project_simple.raw_data_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data)"
            --break main.rs:31
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_PROJECT_SIMPLE_RS_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_simple.raw_data_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_rs_built
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 30)

    # `summary` subcommand renders `summary_template` against a live frame.
    # Expected: `data sum=110 (halves 10 + 100; count=8)`.
    add_test(NAME e2e.topo-debug-rust.project_simple.summary
        COMMAND ${_TOPO_DEBUG_BIN} summary "Buf"
            --break main.rs:31
            --debug-meta ${_PROJECT_SIMPLE_RS_DIR}/main.topo-dbg.json
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_PROJECT_SIMPLE_RS_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_simple.summary PROPERTIES
        FIXTURES_REQUIRED project_simple_rs_built
        PASS_REGULAR_EXPRESSION "^data sum=110 \\(halves 10 \\+ 100; count=8\\)\n"
        LABELS "e2e;topo-debug-rust;toolchain;topo-debug-summary"
        TIMEOUT 30)

    # `snapshot` capture + `query --replay` end-to-end. Step A captures a
    # TOPO_SNAP_V1 file from the live target. Step B replays the snapshot
    # and evaluates `sum(data)` against the cached bytes — no adapter
    # spawn — and asserts the same `110` answer as the live raw_data_sum
    # test above.
    set(_PROJECT_SIMPLE_RS_SNAP
        ${CMAKE_CURRENT_BINARY_DIR}/project_simple_rs_buf.snap)
    add_test(NAME e2e.topo-debug-rust.project_simple.snapshot_capture
        COMMAND ${_TOPO_DEBUG_BIN} snapshot "Buf"
            --break main.rs:31
            --debug-meta ${_PROJECT_SIMPLE_RS_DIR}/main.topo-dbg.json
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_PROJECT_SIMPLE_RS_DIR}/main
            --out ${_PROJECT_SIMPLE_RS_SNAP}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_simple.snapshot_capture PROPERTIES
        FIXTURES_REQUIRED project_simple_rs_built
        FIXTURES_SETUP project_simple_rs_snapshot
        LABELS "e2e;topo-debug-rust;toolchain;topo-debug-snapshot"
        TIMEOUT 30)

    add_test(NAME e2e.topo-debug-rust.project_simple.snapshot_then_replay
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data)"
            --replay ${_PROJECT_SIMPLE_RS_SNAP})
    set_tests_properties(e2e.topo-debug-rust.project_simple.snapshot_then_replay PROPERTIES
        FIXTURES_REQUIRED "project_simple_rs_built;project_simple_rs_snapshot"
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-rust;toolchain;topo-debug-snapshot"
        TIMEOUT 30)

    # project_multi — two host arrays in scope, summary template references
    # both. Expected at main.rs:25 (`let pair = (data_a[i], data_b[i]);`):
    #   sum(data_a)              = 10
    #   sum(data_b)              = 100
    #   sum(data_a)+sum(data_b)  = 110
    #   max(data_b)-max(data_a)  = 36
    set(_PROJECT_MULTI_RS_DIR ${TOPO_DEBUG_RUST_PROJECT_MULTI_DIR})

    add_test(NAME e2e.topo-debug-rust.project_multi.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_MULTI_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_multi.build PROPERTIES
        FIXTURES_SETUP project_multi_rs_built
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 120)

    add_test(NAME e2e.topo-debug-rust.project_multi.sum_a
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data_a)"
            --break main.rs:25
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_PROJECT_MULTI_RS_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_MULTI_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_multi.sum_a PROPERTIES
        FIXTURES_REQUIRED project_multi_rs_built
        PASS_REGULAR_EXPRESSION "^10\n"
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 30)

    add_test(NAME e2e.topo-debug-rust.project_multi.sum_b
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data_b)"
            --break main.rs:25
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_PROJECT_MULTI_RS_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_MULTI_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_multi.sum_b PROPERTIES
        FIXTURES_REQUIRED project_multi_rs_built
        PASS_REGULAR_EXPRESSION "^100\n"
        LABELS "e2e;topo-debug-rust;toolchain"
        TIMEOUT 30)

    add_test(NAME e2e.topo-debug-rust.project_multi.sum_a_plus_b
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data_a)+sum(data_b)"
            --break main.rs:25
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_PROJECT_MULTI_RS_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_MULTI_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_multi.sum_a_plus_b PROPERTIES
        FIXTURES_REQUIRED project_multi_rs_built
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-rust;toolchain;topo-debug-multivar"
        TIMEOUT 30)

    add_test(NAME e2e.topo-debug-rust.project_multi.summary
        COMMAND ${_TOPO_DEBUG_BIN} summary "TwoArrays"
            --break main.rs:25
            --debug-meta ${_PROJECT_MULTI_RS_DIR}/main.topo-dbg.json
            --adapter ${_TOPO_DEBUG_RUST}
            --target ${_PROJECT_MULTI_RS_DIR}/main
        WORKING_DIRECTORY ${_PROJECT_MULTI_RS_DIR})
    set_tests_properties(e2e.topo-debug-rust.project_multi.summary PROPERTIES
        FIXTURES_REQUIRED project_multi_rs_built
        PASS_REGULAR_EXPRESSION "^a=10, b=100, total=110, span=36\n"
        LABELS "e2e;topo-debug-rust;toolchain;topo-debug-multivar;topo-debug-summary"
        TIMEOUT 30)
endif()

# topo-debug-typescript CDP adapter against Node.js.
# (label: topo-debug-typescript).
#
# Gated on `TARGET topo-debug-typescript` so the tests appear only when
# Node >= 22 was discovered at configure time. The launcher is a small POSIX
# shell wrapper that execs `node adapter.mjs "$@"`; the adapter drives a
# `node --inspect-brk=0` child of the fixture script via Chrome DevTools
# Protocol over WebSocket, hits a line breakpoint, reads `vec` (Float64Array)
# raw bytes, and emits the debug wire records that the CLI decodes.
#
# `tinyVector.mjs` declares `const vec = new Float64Array([0.5..7.5])` on
# line 2 and `const sentinel = 0;` on line 3 — the breakpoint hits after the
# array initialiser finishes. Sum 0.5+1.5+...+7.5 = 32.0.
#
# TIMEOUT 60 instead of 30: Node cold-start + CDP handshake adds 2-3 s vs
# lldb's near-instant launch, and CI machines amplify the variance.
if(TARGET topo-debug-typescript-stage)
    set(_TOPO_DEBUG_TS "${TOPO_DEBUG_TYPESCRIPT_LAUNCHER}")
    set(_TINY_VECTOR_TS_FIXTURE "${TOPO_DEBUG_TYPESCRIPT_FIXTURE}")

    add_test(NAME e2e.topo-debug-typescript.sum_vec
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(vec)"
            --break tinyVector.mjs:3
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_TINY_VECTOR_TS_FIXTURE})
    set_tests_properties(e2e.topo-debug-typescript.sum_vec PROPERTIES
        # 0.5+1.5+...+7.5 = 32.0
        PASS_REGULAR_EXPRESSION "^32(\\.0+)?\n"
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-typescript.shape_vec
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(vec)"
            --break tinyVector.mjs:3
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_TINY_VECTOR_TS_FIXTURE})
    set_tests_properties(e2e.topo-debug-typescript.shape_vec PROPERTIES
        PASS_REGULAR_EXPRESSION "^\\[8\\]\n"
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-typescript.dtype_vec
        COMMAND ${_TOPO_DEBUG_BIN} query "dtype(vec)"
            --break tinyVector.mjs:3
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_TINY_VECTOR_TS_FIXTURE})
    set_tests_properties(e2e.topo-debug-typescript.dtype_vec PROPERTIES
        PASS_REGULAR_EXPRESSION "^f64\n"
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)
endif()

# TypeScript topo-debug *project-level* fixtures.
#
# Counterpart of the cpp/rust/java/python project_simple / project_multi
# blocks above. Unlike tinyVector.mjs (a single .mjs driven through the
# adapter), these fixtures run the full `topo build` → topo-build-typescript
# (check-only) → main.topo-dbg.json pipeline. Each fixture is staged at
# configure time in topo-lang-typescript/topo-debug/test/CMakeLists.txt
# (TOPO_DEBUG_TYPESCRIPT_PROJECT_*_DIR vars). The `--target` is `main.mjs`
# (NOT the `.ts` source) because V8's CDP `setBreakpointByUrl` requires the
# URL match what Node parsed at runtime — Node 26 strips types from `.ts`
# but the resulting Debugger URL diverges from a `file://...ts` URL we hand
# it, so the breakpoint stays unresolved. Keeping the runtime in `.mjs`
# mirrors the existing `tinyVector.mjs` contract.
#
# CDP handshake quirks (ground truth): Node's `--inspect-brk`
# fires an initial `Break on start` pause before our line breakpoint hits;
# the adapter resumes past it and re-waits. `setBreakpointByUrl` returns
# empty `locations` against a freshly attached, not-yet-parsed script, and
# the breakpoint resolves later via the `breakpointResolved` event. These
# tests exercise that path every spawn.
if(TARGET topo-debug-typescript-stage)
    set(_TOPO_DEBUG_TS "${TOPO_DEBUG_TYPESCRIPT_LAUNCHER}")
    set(_TOPO_BUILD_BIN "$<TARGET_FILE:topo-build>")

    # project_simple — single Int32Array `data` of length 8 with two
    # literal-bounded halves + summary + snapshot/replay. Sentinel
    # breakpoint is main.mjs:31.
    #   sum(first_half)  = 1+2+3+4         = 10
    #   sum(second_half) = 10+20+30+40     = 100
    #   sum(data)        = 110
    #   shape(first_half)                  = [4]
    set(_PROJECT_SIMPLE_TS_DIR ${TOPO_DEBUG_TYPESCRIPT_PROJECT_SIMPLE_DIR})
    set(_PROJECT_SIMPLE_TS_TARGET ${_PROJECT_SIMPLE_TS_DIR}/main.mjs)
    # topo-build-typescript is check-only — it doesn't link a binary, but
    # `topo-build` still writes `<output>.topo-dbg.json` next to where the
    # output would live. With `[build] output = "main"`, the dbg.json lives
    # at `<dir>/main.topo-dbg.json`; pass it explicitly via --debug-meta
    # because auto-discovery looks for `<target>.topo-dbg.json` and our
    # `--target` is `main.mjs` (a different basename).
    set(_PROJECT_SIMPLE_TS_DBG ${_PROJECT_SIMPLE_TS_DIR}/main.topo-dbg.json)

    add_test(NAME e2e.topo-debug-typescript.project_simple.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_simple.build PROPERTIES
        FIXTURES_SETUP project_simple_ts_built
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-typescript.project_simple.view_first_half_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(first_half)"
            --break main.mjs:31
            --debug-meta ${_PROJECT_SIMPLE_TS_DBG}
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_PROJECT_SIMPLE_TS_TARGET}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_simple.view_first_half_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_ts_built
        PASS_REGULAR_EXPRESSION "^10\n"
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-typescript.project_simple.view_second_half_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(second_half)"
            --break main.mjs:31
            --debug-meta ${_PROJECT_SIMPLE_TS_DBG}
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_PROJECT_SIMPLE_TS_TARGET}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_simple.view_second_half_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_ts_built
        PASS_REGULAR_EXPRESSION "^100\n"
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-typescript.project_simple.view_first_half_shape
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(first_half)"
            --break main.mjs:31
            --debug-meta ${_PROJECT_SIMPLE_TS_DBG}
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_PROJECT_SIMPLE_TS_TARGET}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_simple.view_first_half_shape PROPERTIES
        FIXTURES_REQUIRED project_simple_ts_built
        PASS_REGULAR_EXPRESSION "^\\[4\\]\n"
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)

    # Back-compat: raw variable name (no view rewrite) still resolves against
    # the same target — the dbg.json + raw-var coexistence doesn't break the
    # tinyVector contract for projects that happen to have both.
    add_test(NAME e2e.topo-debug-typescript.project_simple.raw_data_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data)"
            --break main.mjs:31
            --debug-meta ${_PROJECT_SIMPLE_TS_DBG}
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_PROJECT_SIMPLE_TS_TARGET}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_simple.raw_data_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_ts_built
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)

    # `summary` subcommand renders `summary_template` against a live frame.
    # Expected: `data sum=110 (halves 10 + 100; count=8)`.
    add_test(NAME e2e.topo-debug-typescript.project_simple.summary
        COMMAND ${_TOPO_DEBUG_BIN} summary "Buf"
            --break main.mjs:31
            --debug-meta ${_PROJECT_SIMPLE_TS_DBG}
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_PROJECT_SIMPLE_TS_TARGET}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_simple.summary PROPERTIES
        FIXTURES_REQUIRED project_simple_ts_built
        PASS_REGULAR_EXPRESSION "^data sum=110 \\(halves 10 \\+ 100; count=8\\)\n"
        LABELS "e2e;topo-debug-typescript;toolchain;topo-debug-summary"
        TIMEOUT 60)

    # `snapshot` capture + `query --replay` end-to-end. Step A captures a
    # TOPO_SNAP_V1 file from the live target. Step B replays the snapshot
    # (no adapter spawn) and asserts the same `110` answer.
    set(_PROJECT_SIMPLE_TS_SNAP
        ${CMAKE_CURRENT_BINARY_DIR}/project_simple_ts_buf.snap)
    add_test(NAME e2e.topo-debug-typescript.project_simple.snapshot_capture
        COMMAND ${_TOPO_DEBUG_BIN} snapshot "Buf"
            --break main.mjs:31
            --debug-meta ${_PROJECT_SIMPLE_TS_DBG}
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_PROJECT_SIMPLE_TS_TARGET}
            --out ${_PROJECT_SIMPLE_TS_SNAP}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_simple.snapshot_capture PROPERTIES
        FIXTURES_REQUIRED project_simple_ts_built
        FIXTURES_SETUP project_simple_ts_snapshot
        LABELS "e2e;topo-debug-typescript;toolchain;topo-debug-snapshot"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-typescript.project_simple.snapshot_then_replay
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data)"
            --replay ${_PROJECT_SIMPLE_TS_SNAP})
    set_tests_properties(e2e.topo-debug-typescript.project_simple.snapshot_then_replay PROPERTIES
        FIXTURES_REQUIRED "project_simple_ts_built;project_simple_ts_snapshot"
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-typescript;toolchain;topo-debug-snapshot"
        TIMEOUT 30)

    # project_multi — two Int32Array locals in scope, summary template
    # references both. Sentinel breakpoint is main.mjs:16.
    #   sum(a)              = 10
    #   sum(b)              = 100
    #   sum(a)+sum(b)       = 110
    #   max(b)-max(a)       = 36
    set(_PROJECT_MULTI_TS_DIR ${TOPO_DEBUG_TYPESCRIPT_PROJECT_MULTI_DIR})
    set(_PROJECT_MULTI_TS_TARGET ${_PROJECT_MULTI_TS_DIR}/main.mjs)
    set(_PROJECT_MULTI_TS_DBG ${_PROJECT_MULTI_TS_DIR}/main.topo-dbg.json)

    add_test(NAME e2e.topo-debug-typescript.project_multi.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_MULTI_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_multi.build PROPERTIES
        FIXTURES_SETUP project_multi_ts_built
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-typescript.project_multi.sum_a
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(a)"
            --break main.mjs:16
            --debug-meta ${_PROJECT_MULTI_TS_DBG}
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_PROJECT_MULTI_TS_TARGET}
        WORKING_DIRECTORY ${_PROJECT_MULTI_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_multi.sum_a PROPERTIES
        FIXTURES_REQUIRED project_multi_ts_built
        PASS_REGULAR_EXPRESSION "^10\n"
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-typescript.project_multi.sum_b
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(b)"
            --break main.mjs:16
            --debug-meta ${_PROJECT_MULTI_TS_DBG}
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_PROJECT_MULTI_TS_TARGET}
        WORKING_DIRECTORY ${_PROJECT_MULTI_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_multi.sum_b PROPERTIES
        FIXTURES_REQUIRED project_multi_ts_built
        PASS_REGULAR_EXPRESSION "^100\n"
        LABELS "e2e;topo-debug-typescript;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-typescript.project_multi.sum_a_plus_b
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(a) + sum(b)"
            --break main.mjs:16
            --debug-meta ${_PROJECT_MULTI_TS_DBG}
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_PROJECT_MULTI_TS_TARGET}
        WORKING_DIRECTORY ${_PROJECT_MULTI_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_multi.sum_a_plus_b PROPERTIES
        FIXTURES_REQUIRED project_multi_ts_built
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-typescript;toolchain;topo-debug-multivar"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-typescript.project_multi.summary
        COMMAND ${_TOPO_DEBUG_BIN} summary "TwoArrays"
            --break main.mjs:16
            --debug-meta ${_PROJECT_MULTI_TS_DBG}
            --adapter ${_TOPO_DEBUG_TS}
            --target ${_PROJECT_MULTI_TS_TARGET}
        WORKING_DIRECTORY ${_PROJECT_MULTI_TS_DIR})
    set_tests_properties(e2e.topo-debug-typescript.project_multi.summary PROPERTIES
        FIXTURES_REQUIRED project_multi_ts_built
        PASS_REGULAR_EXPRESSION "^a=10, b=100, total=110, span=36\n"
        LABELS "e2e;topo-debug-typescript;toolchain;topo-debug-multivar;topo-debug-summary"
        TIMEOUT 60)
endif()

# `topo profile trace <bin>` against libtopo-observe-
# linked fixtures (one C++, one Rust). The fixtures emit three named spans
# `pipeline::demo::stage{0,1,2}`; topo-profile turns them into the
# sampling schema and prints to stdout. The regex below validates the
# language-agnostic claim (same JSON shape regardless of host language).
if(TOPO_ENABLE_LLVM AND TARGET topo-profile AND TARGET spans_demo_cpp)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    set(_SPANS_DEMO_CPP "$<TARGET_FILE:spans_demo_cpp>")

    add_test(NAME e2e.topo-profile.trace_cpp
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_SPANS_DEMO_CPP})
    set_tests_properties(e2e.topo-profile.trace_cpp PROPERTIES
        # Single regex anchors all three stages + pipeline + backend.
        # JSON pretty-print emits keys alphabetically (annotations, backend,
        # end_ns, name, pipeline, stage, ...). Anchoring on `name` in source
        # order is robust to key-ordering changes and to the alphabetical
        # backend-before-stage layout in each span object.
        PASS_REGULAR_EXPRESSION
            "pipeline::demo::stage0.*pipeline::demo::stage1.*pipeline::demo::stage2"
        LABELS "e2e;topo-profile;toolchain"
        TIMEOUT 30)
endif()

# pass-event routing. The fixture links the reusable
# libtopo-pass-event wire and emits a deterministic AOT⇄JIT variant-
# switch sequence (AdaptiveDispatchPass is the first producer). The CLI
# must route those into a top-level `pass_events.AdaptiveDispatchPass[]`
# array. Gated identically to the span e2e (TOPO_ENABLE_LLVM + the
# fixture target + topo-profile).
if(TOPO_ENABLE_LLVM AND TARGET topo-profile AND TARGET pass_events_demo_cpp)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    set(_PASS_EVENTS_DEMO_CPP "$<TARGET_FILE:pass_events_demo_cpp>")

    # spans-mode: pass-event records on the same stream are routed into
    # pass_events; `spans` stays an (empty) array, unaffected.
    add_test(NAME e2e.topo-profile.pass_events_cpp
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_PASS_EVENTS_DEMO_CPP})
    set_tests_properties(e2e.topo-profile.pass_events_cpp PROPERTIES
        # CTest regex `.` does not span newlines. Anchor on the routed
        # array key plus both switch directions; pretty-print emits keys
        # alphabetically (from, subject, to, ts_ns) so "aot".*"jit" then
        # "jit".*"aot" appears across the two event objects in order.
        PASS_REGULAR_EXPRESSION
            "\"pass_events\".*\"AdaptiveDispatchPass\".*\"aot\".*\"jit\".*\"jit\".*\"aot\""
        LABELS "e2e;topo-profile;topo-profile-pass-events;toolchain"
        TIMEOUT 30)

    # The span-only invariant: pass-event output still carries the
    # top-level `spans` key (as an array), so existing consumers that
    # only read `spans` keep working unchanged.
    add_test(NAME e2e.topo-profile.pass_events_cpp_spans_key_present
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_PASS_EVENTS_DEMO_CPP})
    set_tests_properties(e2e.topo-profile.pass_events_cpp_spans_key_present PROPERTIES
        PASS_REGULAR_EXPRESSION "\"spans\": \\[\\]"
        LABELS "e2e;topo-profile;topo-profile-pass-events;toolchain"
        TIMEOUT 30)
endif()

# LifetimeArenaPass arena open/close routing. The
# fixture links the same reusable libtopo-pass-event wire and emits a
# deterministic heap->arena / arena->freed sequence carrying the new
# optional `bytes` size field. The CLI must route those into a top-level
# `pass_events.LifetimeArenaPass[]` array with `bytes` surfaced, while
# the existing AdaptiveDispatch routing and the `spans` key stay
# unaffected. Gated identically to the AdaptiveDispatch pass-event e2e.
if(TOPO_ENABLE_LLVM AND TARGET topo-profile AND TARGET arena_events_demo_cpp)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    set(_ARENA_EVENTS_DEMO_CPP "$<TARGET_FILE:arena_events_demo_cpp>")
    set(_SAMPLING_DEMO_FOLDED
        "${PROJECT_SOURCE_DIR}/topo-lang-cpp/topo-profile/test/fixtures/sampling_demo.folded")

    # spans-mode (default): arena pass-events route into
    # pass_events.LifetimeArenaPass[]; both open (heap->arena) and close
    # (arena->freed) appear in order, each carrying the routed `bytes`.
    # Pretty-print emits object keys alphabetically (bytes, from,
    # subject, to, ts_ns) so within the open object "4096" precedes
    # "heap"; the close object's "1280" then "arena" follows.
    add_test(NAME e2e.topo-profile.arena_events_cpp
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_ARENA_EVENTS_DEMO_CPP})
    set_tests_properties(e2e.topo-profile.arena_events_cpp PROPERTIES
        PASS_REGULAR_EXPRESSION
            "\"pass_events\".*\"LifetimeArenaPass\".*4096.*\"heap\".*\"arena\".*1280.*\"arena\".*\"freed\""
        LABELS "e2e;topo-profile;topo-profile-pass-events;toolchain"
        TIMEOUT 30)

    # hybrid-mode: hybrid traces must also carry pass_events alongside
    # spans + the merged sampling segment (the CLI surfaces pass_events
    # in both single-mode and the hybrid spans+sampling merge — same
    # collectSpans()/routePassEvent() code path). The `bytes` payload
    # must survive the hybrid path identically. Invoked exactly like the
    # hybrid_cpp positive: a spawned binary for boundaries + the
    # checked-in folded fixture for the sampling segment.
    add_test(NAME e2e.topo-profile.arena_events_cpp_hybrid
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_ARENA_EVENTS_DEMO_CPP}
                --mode hybrid
                --sampling-input ${_SAMPLING_DEMO_FOLDED}
                --backend cpp)
    set_tests_properties(e2e.topo-profile.arena_events_cpp_hybrid PROPERTIES
        PASS_REGULAR_EXPRESSION
            "\"pass_events\".*\"LifetimeArenaPass\".*\"bytes\".*4096.*\"bytes\".*1280"
        LABELS "e2e;topo-profile;topo-profile-pass-events;toolchain"
        TIMEOUT 30)

    # Back-compat invariant: the span-only `spans` key is still present
    # (as an array) so existing consumers reading only `spans` keep
    # working unchanged even when the new sized field is in play.
    add_test(NAME e2e.topo-profile.arena_events_cpp_spans_key_present
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_ARENA_EVENTS_DEMO_CPP})
    set_tests_properties(e2e.topo-profile.arena_events_cpp_spans_key_present PROPERTIES
        PASS_REGULAR_EXPRESSION "\"spans\": \\[\\]"
        LABELS "e2e;topo-profile;topo-profile-pass-events;toolchain"
        TIMEOUT 30)
endif()

# TopoParallelPass task spawn/join routing. The
# fixture links the same reusable libtopo-pass-event wire and emits a
# deterministic serial->spawned / spawned->joined sequence using the
# 4-arg (no-`bytes`) form. The CLI must route those into a top-level
# `pass_events.TopoParallelPass[]` array, while the existing
# AdaptiveDispatch / LifetimeArena routing and the `spans` key stay
# unaffected. Gated identically to the AdaptiveDispatch/LifetimeArena
# pass-event e2e.
if(TOPO_ENABLE_LLVM AND TARGET topo-profile AND TARGET parallel_events_demo_cpp)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    set(_PARALLEL_EVENTS_DEMO_CPP "$<TARGET_FILE:parallel_events_demo_cpp>")
    set(_SAMPLING_DEMO_FOLDED
        "${PROJECT_SOURCE_DIR}/topo-lang-cpp/topo-profile/test/fixtures/sampling_demo.folded")

    # spans-mode (default): parallel pass-events route into
    # pass_events.TopoParallelPass[]; both spawn (serial->spawned) and
    # join (spawned->joined) appear in order, subject=stage_pipeline.
    # Pretty-print emits object keys alphabetically (from, subject, to,
    # ts_ns) and the array preserves emission order, so the spawn
    # object's "serial" precedes "spawned", then the join object's
    # "spawned" precedes "joined". No `bytes` key is emitted (4-arg
    # form), proving the size field stays absent for this producer.
    add_test(NAME e2e.topo-profile.parallel_events_cpp
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_PARALLEL_EVENTS_DEMO_CPP})
    set_tests_properties(e2e.topo-profile.parallel_events_cpp PROPERTIES
        PASS_REGULAR_EXPRESSION
            "\"pass_events\".*\"TopoParallelPass\".*\"serial\".*\"stage_pipeline\".*\"spawned\".*\"spawned\".*\"stage_pipeline\".*\"joined\""
        LABELS "e2e;topo-profile;topo-profile-pass-events;toolchain"
        TIMEOUT 30)

    # hybrid-mode: hybrid traces must also carry pass_events alongside
    # spans + the merged sampling segment (same collectSpans()/
    # routePassEvent() code path the arena hybrid test exercises).
    add_test(NAME e2e.topo-profile.parallel_events_cpp_hybrid
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_PARALLEL_EVENTS_DEMO_CPP}
                --mode hybrid
                --sampling-input ${_SAMPLING_DEMO_FOLDED}
                --backend cpp)
    set_tests_properties(e2e.topo-profile.parallel_events_cpp_hybrid PROPERTIES
        PASS_REGULAR_EXPRESSION
            "\"pass_events\".*\"TopoParallelPass\".*\"serial\".*\"spawned\".*\"joined\""
        LABELS "e2e;topo-profile;topo-profile-pass-events;toolchain"
        TIMEOUT 30)

    # Back-compat invariant: the span-only `spans` key is still present
    # (as an array) so existing consumers reading only `spans` keep
    # working unchanged when a third pass-event producer is in play.
    add_test(NAME e2e.topo-profile.parallel_events_cpp_spans_key_present
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_PARALLEL_EVENTS_DEMO_CPP})
    set_tests_properties(e2e.topo-profile.parallel_events_cpp_spans_key_present PROPERTIES
        PASS_REGULAR_EXPRESSION "\"spans\": \\[\\]"
        LABELS "e2e;topo-profile;topo-profile-pass-events;toolchain"
        TIMEOUT 30)
endif()

if(TOPO_ENABLE_LLVM AND NOT WIN32 AND TARGET topo-profile AND TARGET spans_demo_rs)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    get_target_property(_SPANS_DEMO_RS_FIXTURE spans_demo_rs TOPO_FIXTURE_PATH)

    add_test(NAME e2e.topo-profile.trace_rust
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_SPANS_DEMO_RS_FIXTURE})
    set_tests_properties(e2e.topo-profile.trace_rust PROPERTIES
        # JSON pretty-print emits keys alphabetically (annotations, backend,
        # end_ns, name, pipeline, stage, ...). Anchoring on `name` in source
        # order is robust to key-ordering changes and to the alphabetical
        # backend-before-stage layout in each span object.
        PASS_REGULAR_EXPRESSION
            "pipeline::demo::stage0.*pipeline::demo::stage1.*pipeline::demo::stage2"
        LABELS "e2e;topo-profile;toolchain"
        TIMEOUT 30)
endif()

# Java span fixture against the host-agnostic
# topo-profile CLI. The fixture emits the same `pipeline::demo::stage{0,1,2}`
# NDJSON shape as libtopo-observe; topo-profile re-emits with `backend: jvm`.
# Gated on the spans_demo_java custom target (itself gated on
# TOPO_GRADLE_JAVA_HOME + javac availability).
if(NOT WIN32 AND TARGET topo-profile AND TARGET spans_demo_java)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    get_target_property(_SPANS_DEMO_JAVA_FIXTURE spans_demo_java TOPO_FIXTURE_PATH)

    add_test(NAME e2e.topo-profile.trace_java
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_SPANS_DEMO_JAVA_FIXTURE}
                --backend jvm)
    set_tests_properties(e2e.topo-profile.trace_java PROPERTIES
        PASS_REGULAR_EXPRESSION
            "pipeline::demo::stage0.*pipeline::demo::stage1.*pipeline::demo::stage2"
        LABELS "e2e;topo-profile;topo-profile-java;toolchain"
        TIMEOUT 60)
endif()

# Python span fixture. Same wire shape, `backend: python`.
# Gated on spans_demo_py custom target (Python3 found).
if(NOT WIN32 AND TARGET topo-profile AND TARGET spans_demo_py)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    get_target_property(_SPANS_DEMO_PY_FIXTURE spans_demo_py TOPO_FIXTURE_PATH)

    add_test(NAME e2e.topo-profile.trace_python
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_SPANS_DEMO_PY_FIXTURE}
                --backend python)
    set_tests_properties(e2e.topo-profile.trace_python PROPERTIES
        PASS_REGULAR_EXPRESSION
            "pipeline::demo::stage0.*pipeline::demo::stage1.*pipeline::demo::stage2"
        LABELS "e2e;topo-profile;topo-profile-python;toolchain"
        TIMEOUT 30)
endif()

# TypeScript / Node span fixture. Same wire shape,
# `backend: v8`. Gated on spans_demo_ts custom target (node on PATH).
if(NOT WIN32 AND TARGET topo-profile AND TARGET spans_demo_ts)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    get_target_property(_SPANS_DEMO_TS_FIXTURE spans_demo_ts TOPO_FIXTURE_PATH)

    add_test(NAME e2e.topo-profile.trace_typescript
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_SPANS_DEMO_TS_FIXTURE}
                --backend v8)
    set_tests_properties(e2e.topo-profile.trace_typescript PROPERTIES
        PASS_REGULAR_EXPRESSION
            "pipeline::demo::stage0.*pipeline::demo::stage1.*pipeline::demo::stage2"
        LABELS "e2e;topo-profile;topo-profile-typescript;toolchain"
        TIMEOUT 30)
endif()

# `topo profile trace --mode=sampling --sampling-input`
# stackcollapse-folded converter e2e tests. The fixture is host-agnostic
# text (no compiler / runtime), so the rust e2e simply re-uses the cpp
# fixture: --backend toggles a tag in the output but the parser is shared.
#
# Fixture stack tally (5 unique stacks, 20 total samples; `compute`
# appears in 8 + 5 + 4 = 17 events):
#   main;init;parse_config                 1
#   main;compute;kernel;inner_loop         8
#   main;compute;kernel;reduce             5
#   main;compute;materialize               4
#   main;io_flush;syscall_write            2
if(TARGET topo-profile)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    set(_SAMPLING_DEMO_FOLDED
        "${PROJECT_SOURCE_DIR}/topo-lang-cpp/topo-profile/test/fixtures/sampling_demo.folded")

    # 1. cpp backend converts the fixture; regex checks the summary
    # counts and that `compute` shows up somewhere in the events array.
    add_test(NAME e2e.topo-profile.sampling_cpp_folded_input
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --sampling-input ${_SAMPLING_DEMO_FOLDED}
                --backend cpp)
    set_tests_properties(e2e.topo-profile.sampling_cpp_folded_input PROPERTIES
        # CTest regex `.` does not span newlines. `"unique_stacks": 5`
        # is a unique success anchor — only the fully-parsed 5-stack
        # fixture produces it. Stack content (incl. `compute`) is
        # exercised by the same code path, so a single anchor suffices.
        PASS_REGULAR_EXPRESSION "\"unique_stacks\": 5"
        LABELS "e2e;topo-profile;topo-profile-sampling;toolchain"
        TIMEOUT 30)

    # 2. rust backend over the same shared fixture — the converter is
    # host-agnostic, so this only verifies the CLI path & backend tag.
    add_test(NAME e2e.topo-profile.sampling_rust_folded_input
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --sampling-input ${_SAMPLING_DEMO_FOLDED}
                --backend rust)
    set_tests_properties(e2e.topo-profile.sampling_rust_folded_input PROPERTIES
        # Same regex-newline limitation as the cpp case above.
        # `"backend": "rust"` is the unique success anchor that
        # confirms both CLI dispatch and backend tag wiring.
        PASS_REGULAR_EXPRESSION "\"backend\": \"rust\""
        LABELS "e2e;topo-profile;topo-profile-sampling;toolchain"
        TIMEOUT 30)

    # 3. Negative: `--mode=sampling` without `--sampling-input` must
    # exit non-zero (specifically 2) with a clear diagnostic — no silent
    # fallback to spans. WILL_FAIL inverts CTest's pass/fail so a
    # non-zero exit code is the expected outcome.
    add_test(NAME e2e.topo-profile.sampling_missing_input_errors
        COMMAND ${_TOPO_PROFILE_BIN} trace --mode sampling)
    set_tests_properties(e2e.topo-profile.sampling_missing_input_errors PROPERTIES
        WILL_FAIL TRUE
        LABELS "e2e;topo-profile;topo-profile-sampling;toolchain"
        TIMEOUT 10)

    # Java JFR-NDJSON sampling path. Same converter
    # library, different source-format. The fixture is host-agnostic
    # text (no JVM at test time — the NDJSON intermediate exists
    # precisely so CTest doesn't need to spawn one).
    #
    # Fixture content (12 events across 2 threads, 5 unique stacks;
    # `geom.Mesh.compute` appears in 9 / 12 events):
    #   thread main(id=1)   : main.App.run -> geom.Mesh.compute             x4
    #                         main.App.run -> geom.Mesh.compute -> Mesh.normalize x2
    #                         main.App.run -> io.Writer.flush               x2
    #   thread worker(id=42): main.Pool.work -> geom.Mesh.compute           x3
    #                         main.Pool.work -> io.Writer.flush             x1
    set(_JFR_SAMPLE_NDJSON
        "${PROJECT_SOURCE_DIR}/topo-lang-java/topo-profile/test/fixtures/jfr_sample.ndjson")

    # 4. java backend converts the JFR-NDJSON fixture. CTest regex `.`
    # does not span newlines (so we cannot match three separated fields
    # in one pattern), and `PASS_REGULAR_EXPRESSION` is OR-semantics
    # across its list entries (not AND), so a single anchor is the
    # right choice. We use the most distinctive line: the literal
    # `"source_format": "jdk_jfr_ndjson"`, which only this code path
    # emits and which is only reachable after the converter has
    # processed every line (it's written at the tail of the JSON
    # object, after the events array). Frame flattening + count
    # accuracy are exercised by the unit-test coverage of
    # JfrNdjsonConverter (TopoProfile lib), so the e2e is verifying
    # the CLI dispatch + backend tagging + happy path end-to-end.
    add_test(NAME e2e.topo-profile.sampling_java_jfr_input
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --jfr-input ${_JFR_SAMPLE_NDJSON}
                --backend java)
    set_tests_properties(e2e.topo-profile.sampling_java_jfr_input PROPERTIES
        PASS_REGULAR_EXPRESSION "\"source_format\": \"jdk_jfr_ndjson\""
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
        TIMEOUT 30)

    # 5. Mutually-exclusive input flags: passing both --jfr-input and
    # --sampling-input must exit non-zero with a clear diagnostic.
    # WILL_FAIL flips the pass criterion so the expected exit-2 reads
    # as a CTest pass.
    add_test(NAME e2e.topo-profile.sampling_jfr_and_folded_conflict_errors
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --jfr-input ${_JFR_SAMPLE_NDJSON}
                --sampling-input ${_SAMPLING_DEMO_FOLDED})
    set_tests_properties(e2e.topo-profile.sampling_jfr_and_folded_conflict_errors PROPERTIES
        WILL_FAIL TRUE
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
        TIMEOUT 10)

    # JFR binary (`.jfr`) path. Real recordings go through
    # the topo-profile-jvm-jfr bridge jar, which translates `jdk.jfr`
    # binary events into the same NDJSON shape the C2 path already parses.
    # Gated on the `jfr_binary_sample` custom target (which itself is
    # gated on TOPO_GRADLE_JAVA_HOME — when Java is absent, the gate is
    # off and CTest skips this case with an explicit log message).
    if(TARGET jfr_binary_sample AND TARGET topo-profile-jvm-jfr)
        get_target_property(_JFR_BINARY_SAMPLE_PATH jfr_binary_sample
            TOPO_JFR_BINARY_PATH)
        # 4b. java backend converts the on-the-fly `.jfr` recording. We
        # cannot anchor a sample count (varies with the recorder's wall-
        # clock-bound JfrDemo workload), so we use the source_format
        # anchor — only emitted after the bridge + converter have run
        # the whole file end-to-end. The `--jfr-bridge-jar` is set
        # explicitly so the test does not rely on $JAVA_HOME (which is
        # set via ENVIRONMENT below); the bridge spawn itself looks up
        # `java` from JAVA_HOME or PATH.
        add_test(NAME e2e.topo-profile.sampling_java_jfr_binary
            COMMAND ${_TOPO_PROFILE_BIN} trace
                    --mode sampling
                    --jfr-binary-input ${_JFR_BINARY_SAMPLE_PATH}
                    --jfr-bridge-jar
                        ${CMAKE_BINARY_DIR}/topo-profile-jvm-jfr.jar
                    --backend java)
        set_tests_properties(e2e.topo-profile.sampling_java_jfr_binary
            PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "\"source_format\": \"jdk_jfr_ndjson\""
                ENVIRONMENT
                    "JAVA_HOME=${TOPO_GRADLE_JAVA_HOME}"
                LABELS
                    "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
                TIMEOUT 60)
    else()
        # Register an explicit CTest entry that reports SKIPPED (exit 77)
        # rather than silently vanishing — a missing Java toolchain must
        # be visible in the test report, not downgraded to a phantom pass.
        message(STATUS "e2e.topo-profile.sampling_java_jfr_binary: "
                       "skipped (TOPO_GRADLE_JAVA_HOME unset — no Java "
                       "toolchain for the JFR bridge).")
        add_test(NAME e2e.topo-profile.sampling_java_jfr_binary
            COMMAND ${CMAKE_COMMAND} -E echo
                    "SKIPPED: TOPO_GRADLE_JAVA_HOME unset; the JFR bridge "
                    "needs a JDK to record + translate a .jfr fixture.")
        set_tests_properties(e2e.topo-profile.sampling_java_jfr_binary
            PROPERTIES
                SKIP_REGULAR_EXPRESSION "SKIPPED:"
                LABELS
                    "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
                TIMEOUT 10)
    endif()

    # extra mutual-exclusion: --jfr-binary-input together
    # with --jfr-input must exit non-zero (matches the existing
    # conflict-error pattern; this case picks the JFR/JFR pair, which
    # is the most semantically confusable, plus one cross-pair check).
    # The .ndjson fixture is host-agnostic; pair it with a fake binary
    # path (existence isn't checked before mutual-exclusion validation).
    add_test(NAME e2e.topo-profile.sampling_jfr_binary_conflict_errors
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --jfr-binary-input /nonexistent/sample.jfr
                --jfr-input ${_JFR_SAMPLE_NDJSON})
    set_tests_properties(e2e.topo-profile.sampling_jfr_binary_conflict_errors
        PROPERTIES
            WILL_FAIL TRUE
            LABELS
                "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
            TIMEOUT 10)
    add_test(NAME e2e.topo-profile.sampling_jfr_binary_conflict_folded
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --jfr-binary-input /nonexistent/sample.jfr
                --sampling-input ${_SAMPLING_DEMO_FOLDED})
    set_tests_properties(e2e.topo-profile.sampling_jfr_binary_conflict_folded
        PROPERTIES
            WILL_FAIL TRUE
            LABELS
                "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
            TIMEOUT 10)

    # negative path: --jfr-binary-input with no resolvable
    # bridge jar must exit 2 with a diagnostic naming the search order.
    # We force the failure by passing an explicit non-existent
    # `--jfr-bridge-jar`. Runs regardless of TOPO_GRADLE_JAVA_HOME — this
    # is the diagnostic-only path and uses no JVM. We also need an
    # existing `.jfr` (the file-existence check fires before bridge
    # resolution) — reuse the NDJSON fixture; the bridge would have
    # failed on it anyway, but we never get that far because the
    # bridge-jar lookup fails first.
    add_test(NAME e2e.topo-profile.sampling_jfr_binary_missing_bridge
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --jfr-binary-input ${_JFR_SAMPLE_NDJSON}
                --jfr-bridge-jar /nonexistent/topo-profile-jvm-jfr.jar
                --backend java)
    set_tests_properties(e2e.topo-profile.sampling_jfr_binary_missing_bridge
        PROPERTIES
            WILL_FAIL TRUE
            FAIL_REGULAR_EXPRESSION "source_format"
            LABELS
                "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
            TIMEOUT 10)

    # async-profiler `collapsed` sampling path (JVM off-CPU /
    # wall-clock / allocation profiling — the cases JFR sampling does not
    # cover well). async-profiler is a native agent the user installs per
    # platform; the checked-in `.collapsed` fixture stands in for its
    # `-o collapsed` text so CTest never attaches the agent (identical
    # reproducibility approach to the JFR `.jfr` and cProfile paths).
    #
    # Fixture (async_profiler_sample.collapsed): 12 samples across 2
    # threads (main tid=1, worker-0 tid=42) and 5 unique stacks. The
    # `[tid] name` async-profiler `-t` thread root is lifted into the
    # per-event `tid`; the converter emits
    # `"source_format": "async_profiler_collapsed"` at the tail after
    # every line is parsed — a unique success anchor (same single-tail-
    # anchor rationale as the JFR / speedscope / cpuprofile e2e tests:
    # CTest regex `.` does not span newlines and PASS_REGULAR_EXPRESSION
    # is OR-semantics across its list).
    set(_ASYNC_PROFILER_COLLAPSED
        "${PROJECT_SOURCE_DIR}/topo-lang-java/topo-profile/test/fixtures/async_profiler_sample.collapsed")

    # async-profiler --type=wall (off-CPU + on-CPU wall-clock; the
    # acceptance criterion's named view).
    add_test(NAME e2e.topo-profile.async_profiler_java_wall
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode async-profiler
                --async-profiler-input ${_ASYNC_PROFILER_COLLAPSED}
                --type wall
                --backend java)
    set_tests_properties(e2e.topo-profile.async_profiler_java_wall PROPERTIES
        PASS_REGULAR_EXPRESSION "\"source_format\": \"async_profiler_collapsed\""
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
        TIMEOUT 30)

    # async-profiler --type=alloc (allocation-site sampling). Same
    # collapsed wire shape; the requested type is recorded in the trace
    # as `profile_type` — anchor on it to prove --type flows through.
    add_test(NAME e2e.topo-profile.async_profiler_java_alloc
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode async-profiler
                --async-profiler-input ${_ASYNC_PROFILER_COLLAPSED}
                --type alloc
                --backend java)
    set_tests_properties(e2e.topo-profile.async_profiler_java_alloc PROPERTIES
        PASS_REGULAR_EXPRESSION "\"profile_type\": \"alloc\""
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
        TIMEOUT 30)

    # Negative: --mode=async-profiler with no --async-profiler-input must
    # exit non-zero with a clear diagnostic (no silent fallback). WILL_FAIL
    # inverts CTest's pass/fail so a non-zero exit is the expected outcome.
    add_test(NAME e2e.topo-profile.async_profiler_missing_input_errors
        COMMAND ${_TOPO_PROFILE_BIN} trace --mode async-profiler --type wall)
    set_tests_properties(e2e.topo-profile.async_profiler_missing_input_errors
        PROPERTIES
            WILL_FAIL TRUE
            LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
            TIMEOUT 10)

    # Negative: an invalid --type must be rejected (exit 2) rather than
    # silently treated as wall.
    add_test(NAME e2e.topo-profile.async_profiler_bad_type_errors
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode async-profiler
                --async-profiler-input ${_ASYNC_PROFILER_COLLAPSED}
                --type bogus)
    set_tests_properties(e2e.topo-profile.async_profiler_bad_type_errors
        PROPERTIES
            WILL_FAIL TRUE
            FAIL_REGULAR_EXPRESSION "source_format"
            LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-java;toolchain"
            TIMEOUT 10)

    # per-Pass profile JFR events. The JVM transform
    # (AdaptivePass / ArenaPass / ParallelPass / PipelinePass via
    # dev.topo.PassEvents) injects stackless topo.pass.<PassName> events;
    # the jfr-bridge forwards them with a "fields" object and
    # JfrNdjsonConverter routes them into pass_events.<PassName>[],
    # additively (jdk.ExecutionSample lines still feed the sampling
    # segment). The fixture mixes both kinds so the e2e proves the
    # routing without needing a JDK at test time (same NDJSON-
    # intermediate reproducibility approach as the C2 path).
    #
    # Anchor: a `pass_events` key with the four PassName sub-arrays only
    # appears once every line has been parsed and routed. `"PipelinePass"`
    # is the most distinctive anchor (it appears nowhere else in the
    # output and only inside pass_events) — CTest regex `.` does not span
    # newlines and PASS_REGULAR_EXPRESSION is OR-semantics, so one unique
    # anchor is the right shape (matches the C2/C3/C4 e2e rationale).
    set(_JFR_PASS_EVENTS_NDJSON
        "${PROJECT_SOURCE_DIR}/topo-lang-java/topo-profile/test/fixtures/jfr_pass_events.ndjson")
    add_test(NAME e2e.topo-profile.jfr_pass_events_routed
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --jfr-input ${_JFR_PASS_EVENTS_NDJSON}
                --backend java)
    set_tests_properties(e2e.topo-profile.jfr_pass_events_routed PROPERTIES
        PASS_REGULAR_EXPRESSION "\"PipelinePass\""
        LABELS "e2e;topo-profile;topo-profile-pass-events;topo-profile-java;toolchain"
        TIMEOUT 30)
    # Second anchor: prove the sampling segment is still populated
    # alongside pass_events (additive contract — ExecutionSample lines
    # must NOT be swallowed by the pass-event routing).
    add_test(NAME e2e.topo-profile.jfr_pass_events_keep_sampling
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --jfr-input ${_JFR_PASS_EVENTS_NDJSON}
                --backend java)
    set_tests_properties(e2e.topo-profile.jfr_pass_events_keep_sampling PROPERTIES
        PASS_REGULAR_EXPRESSION "\"source_format\": \"jdk_jfr_ndjson\""
        LABELS "e2e;topo-profile;topo-profile-pass-events;topo-profile-java;toolchain"
        TIMEOUT 30)

    # Python speedscope-JSON sampling path. Same
    # converter library, different source-format. The fixture is host-
    # agnostic JSON (no Python at test time — speedscope is py-spy's
    # default dump format, but the parser is pure JSON).
    #
    # Fixture content (20 samples across 2 profiles, 6 unique stacks;
    # `compute` appears in 14 / 20 samples):
    #   profile 0 (MainThread, ms)  : main->run->compute->inner    x5
    #                                 main->run->compute            x4
    #                                 main->run->io_flush           x4   (13 samples)
    #   profile 1 (Worker-0, ms)    : main->work->compute->inner    x2
    #                                 main->work->compute           x3
    #                                 main->work->io_flush          x2   (7 samples)
    set(_SPEEDSCOPE_SAMPLE_JSON
        "${PROJECT_SOURCE_DIR}/topo-lang-python/topo-profile/test/fixtures/py_spy_sample.speedscope.json")
    set(_SPEEDSCOPE_EVENTED_JSON
        "${PROJECT_SOURCE_DIR}/topo-lang-python/topo-profile/test/fixtures/py_spy_evented.speedscope.json")

    # 6. python backend converts the speedscope sampled fixture. CTest
    # regex `.` does not span newlines (same caveat as the JFR test).
    # `PASS_REGULAR_EXPRESSION` is OR-semantics across its list entries,
    # which would give us an over-permissive "any one of these" rule;
    # we instead pick a single line that only succeeds for a complete
    # parse of this exact fixture and key off it: the literal
    # `"source_format": "speedscope_sampled"`, emitted only after the
    # whole document is consumed and `unique_threads`, `total_samples`,
    # and `unique_stacks` have all been computed. Stack content
    # (including `compute`) is exercised by the same code path, so a
    # single tail anchor suffices end-to-end. Additionally, the
    # backend-tag wiring is verified by the second test below.
    add_test(NAME e2e.topo-profile.sampling_python_speedscope
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --speedscope-input ${_SPEEDSCOPE_SAMPLE_JSON}
                --backend python)
    set_tests_properties(e2e.topo-profile.sampling_python_speedscope PROPERTIES
        PASS_REGULAR_EXPRESSION "\"source_format\": \"speedscope_sampled\""
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-python;toolchain"
        TIMEOUT 30)

    # 7. Evented profiles are unsupported (only `sampled` is). The
    # converter must reject the whole document with a dedicated
    # diagnostic and exit 2. WILL_FAIL flips the pass criterion so
    # non-zero exit reads as CTest pass.
    add_test(NAME e2e.topo-profile.sampling_speedscope_evented_rejected
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --speedscope-input ${_SPEEDSCOPE_EVENTED_JSON}
                --backend python)
    set_tests_properties(e2e.topo-profile.sampling_speedscope_evented_rejected PROPERTIES
        WILL_FAIL TRUE
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-python;toolchain"
        TIMEOUT 10)

    # TypeScript / V8 `.cpuprofile` sampling path. Same
    # converter library, different source-format. The fixture is a hand-
    # written minimal V8 cpuprofile: 1 (root) + 6 callFrame nodes + 20
    # samples + 20 timeDeltas, no Node / Chrome at test time.
    #
    # Fixture stack tally (3 unique stacks, 20 total samples;
    # `compute` appears in 17 / 20 events):
    #   main -> compute -> innerLoop     x11   (leaf id 4)
    #   main -> compute -> reduce        x6    (leaf id 5)
    #   main -> ioFlush -> syscallWrite  x3    (leaf id 7)
    set(_V8_SAMPLE_CPUPROFILE
        "${PROJECT_SOURCE_DIR}/topo-lang-typescript/topo-profile/test/fixtures/v8_sample.cpuprofile")

    # 8. typescript backend converts the V8 `.cpuprofile` fixture. CTest
    # regex `.` does not span newlines, and PASS_REGULAR_EXPRESSION is
    # OR-semantics across a list. We anchor on the literal
    # `"source_format": "v8_cpuprofile"` — only this code path emits it
    # and it is written at the tail of the JSON object, after every
    # sample has been walked and `total_samples` / `unique_stacks` /
    # `unique_threads` have been computed. Stack content (incl.
    # `compute`) and the `"backend": "typescript"` tag are exercised by
    # the same code path, so one tail anchor suffices end-to-end.
    add_test(NAME e2e.topo-profile.sampling_typescript_cpuprofile
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --cpuprofile-input ${_V8_SAMPLE_CPUPROFILE}
                --backend typescript)
    set_tests_properties(e2e.topo-profile.sampling_typescript_cpuprofile PROPERTIES
        PASS_REGULAR_EXPRESSION "\"source_format\": \"v8_cpuprofile\""
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-typescript;toolchain"
        TIMEOUT 30)

    # 9. Mutually-exclusive input flags: passing both --cpuprofile-input
    # and --sampling-input must exit 2 with a clear diagnostic. Per
    # plan-44 T3 instructions we avoid WILL_FAIL for the exit-code check
    # and instead anchor PASS_REGULAR_EXPRESSION on the unique
    # diagnostic line. PASS_REGULAR_EXPRESSION fires regardless of exit
    # code, so when the message is emitted (which happens together with
    # exit 2 from the input-count guard) the test passes — and a silent
    # silent-pick or successful run produces no such line.
    add_test(NAME e2e.topo-profile.sampling_cpuprofile_and_folded_conflict_errors
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --cpuprofile-input ${_V8_SAMPLE_CPUPROFILE}
                --sampling-input ${_SAMPLING_DEMO_FOLDED})
    set_tests_properties(e2e.topo-profile.sampling_cpuprofile_and_folded_conflict_errors PROPERTIES
        PASS_REGULAR_EXPRESSION "mutually exclusive"
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-typescript;toolchain"
        TIMEOUT 10)

    # Python cProfile NDJSON sampling path.
    # Same converter library, different source-format. The fixture is a
    # hand-curated minimal NDJSON snapshot (6 user-code events) so CTest
    # does not depend on interpreter import-time noise.
    #
    # Fixture content (6 user-code edges, 4 unique stacks):
    #   helper -> nested         (one repeat counted by the fixture)
    #   helper -> twiddle
    #   compute -> helper
    #   <module> -> compute
    #   nested (single-frame entry)
    #   twiddle (single-frame entry)
    set(_CPROFILE_SAMPLE_NDJSON
        "${PROJECT_SOURCE_DIR}/topo-lang-python/topo-profile/test/fixtures/cprofile_sample.ndjson")

    # 10. python backend converts the cProfile NDJSON fixture. CTest regex
    # `.` does not span newlines, and PASS_REGULAR_EXPRESSION is OR-semantics
    # across a list. We anchor on the literal
    # `"source_format": "cprofile_ndjson"` — only this code path emits it
    # and it is written at the tail of the JSON object, after every event
    # has been parsed and `total_samples` / `unique_stacks` /
    # `unique_threads` have been computed. Stack content and the
    # `"backend": "python"` tag are exercised by the same code path, so a
    # single tail anchor suffices end-to-end.
    add_test(NAME e2e.topo-profile.sampling_python_cprofile
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --cprofile-input ${_CPROFILE_SAMPLE_NDJSON}
                --backend python)
    set_tests_properties(e2e.topo-profile.sampling_python_cprofile PROPERTIES
        PASS_REGULAR_EXPRESSION "\"source_format\": \"cprofile_ndjson\""
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-python;toolchain"
        TIMEOUT 30)

    # 11. Mutual-exclusion diagnostic for --cprofile-input. We need to
    # exercise conflict against >=2 other input flags; one test case is
    # enough to verify the diagnostic itself, but covers two conflict
    # combinations by also chaining --jfr-input. Using
    # PASS_REGULAR_EXPRESSION "mutually exclusive" (the diagnostic that
    # fires together with exit 2 from the input-count guard) confirms the
    # mutex without depending on WILL_FAIL semantics.
    add_test(NAME e2e.topo-profile.sampling_cprofile_conflict_errors
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --cprofile-input ${_CPROFILE_SAMPLE_NDJSON}
                --speedscope-input ${_SPEEDSCOPE_SAMPLE_JSON}
                --jfr-input ${_JFR_SAMPLE_NDJSON})
    set_tests_properties(e2e.topo-profile.sampling_cprofile_conflict_errors PROPERTIES
        PASS_REGULAR_EXPRESSION "mutually exclusive"
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-python;toolchain"
        TIMEOUT 10)

    # PEP 669 sys.monitoring path. The converter is fully
    # covered by a checked-in PY_START/PY_RETURN NDJSON fixture (same
    # reproducibility approach as the cProfile / JFR paths — no live
    # interpreter dependency). source_format `py_sys_monitoring` is the
    # tail anchor: only this code path emits it, after every event has
    # been paired and the summary computed.
    set(_SYSMON_SAMPLE_NDJSON
        "${PROJECT_SOURCE_DIR}/topo-lang-python/topo-profile/test/fixtures/sys_monitoring_sample.ndjson")

    add_test(NAME e2e.topo-profile.sampling_python_sys_monitoring
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sys-monitoring
                --sys-monitoring-input ${_SYSMON_SAMPLE_NDJSON}
                --backend python)
    set_tests_properties(e2e.topo-profile.sampling_python_sys_monitoring PROPERTIES
        PASS_REGULAR_EXPRESSION "\"source_format\": \"py_sys_monitoring\""
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-python;toolchain"
        TIMEOUT 30)

    # Missing input flag → clear diagnostic + exit 2.
    add_test(NAME e2e.topo-profile.sys_monitoring_missing_input_errors
        COMMAND ${_TOPO_PROFILE_BIN} trace --mode sys-monitoring)
    set_tests_properties(e2e.topo-profile.sys_monitoring_missing_input_errors PROPERTIES
        PASS_REGULAR_EXPRESSION "requires --sys-monitoring-input"
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-python;toolchain"
        TIMEOUT 10)

    # --sys-monitoring-input under the wrong mode → clear diagnostic.
    add_test(NAME e2e.topo-profile.sys_monitoring_wrong_mode_errors
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --sys-monitoring-input ${_SYSMON_SAMPLE_NDJSON})
    set_tests_properties(e2e.topo-profile.sys_monitoring_wrong_mode_errors PROPERTIES
        PASS_REGULAR_EXPRESSION "requires --mode=sys-monitoring"
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-python;toolchain"
        TIMEOUT 10)

    # Live end-to-end (harness → topo-profile) — registered ONLY when
    # configure found Python >= 3.12 (PEP 669). When absent the converter
    # fixture test above still fully exercises the C++ path; this is an
    # explicit configure-time gate, not a silent runtime skip.
    if(Python3_FOUND AND NOT Python3_VERSION VERSION_LESS "3.12")
        add_test(NAME e2e.topo-profile.sys_monitoring_live_harness
            COMMAND ${Python3_EXECUTABLE}
                    "${PROJECT_SOURCE_DIR}/topo-lang-python/topo-profile/test/sys_monitoring_e2e_driver.py"
                    ${_TOPO_PROFILE_BIN}
                    "${PROJECT_SOURCE_DIR}")
        set_tests_properties(e2e.topo-profile.sys_monitoring_live_harness PROPERTIES
            PASS_REGULAR_EXPRESSION "declaration join verified"
            LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-python;toolchain"
            TIMEOUT 90)
    endif()

    # 12. --cprofile-input is sampling-only; pairing it with --mode=spans
    # must exit 2 with a clear diagnostic rather than silently ignoring
    # the input flag. Anchor on the dedicated diagnostic string emitted
    # by the spans-mode guard.
    add_test(NAME e2e.topo-profile.sampling_cprofile_mode_conflict
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode spans
                --cprofile-input ${_CPROFILE_SAMPLE_NDJSON})
    set_tests_properties(e2e.topo-profile.sampling_cprofile_mode_conflict PROPERTIES
        PASS_REGULAR_EXPRESSION "--cprofile-input requires --mode=sampling"
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-python;toolchain"
        TIMEOUT 10)

    # sourcemap join. The fixture's `sourcemap_demo.cpuprofile`
    # references positions in `sourcemap_demo/index.js`; the sibling
    # `index.js.map` (committed, hand-generated, no tsc at test time)
    # remaps every frame back to `index.ts`. With
    # `--sourcemap-search-root <dir>` pointed at the directory holding
    # the map, the converter resolves frames and flips `source_format`
    # to `cpuprofile_with_sourcemap`. We anchor on that literal — only
    # the resolved path emits it, and it sits at the JSON tail after
    # `frames_resolved_via_sourcemap` (> 0) is computed. The stack
    # content (`index.ts` basename) is exercised by the same code path.
    set(_SOURCEMAP_DEMO_CPUPROFILE
        "${PROJECT_SOURCE_DIR}/topo-lang-typescript/topo-profile/test/fixtures/sourcemap_demo.cpuprofile")
    set(_SOURCEMAP_DEMO_DIR
        "${PROJECT_SOURCE_DIR}/topo-lang-typescript/topo-profile/test/fixtures/sourcemap_demo")
    add_test(NAME e2e.topo-profile.sampling_typescript_cpuprofile_sourcemap
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --cpuprofile-input ${_SOURCEMAP_DEMO_CPUPROFILE}
                --sourcemap-search-root ${_SOURCEMAP_DEMO_DIR}
                --backend typescript)
    set_tests_properties(e2e.topo-profile.sampling_typescript_cpuprofile_sourcemap PROPERTIES
        PASS_REGULAR_EXPRESSION "\"source_format\": \"cpuprofile_with_sourcemap\""
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-typescript;toolchain"
        TIMEOUT 30)

    # `--sourcemap-search-root` without `--cpuprofile-input`
    # is a usage error: the flag is V8-cpuprofile-only. Exit 2 + a clear
    # diagnostic. Same PASS_REGULAR_EXPRESSION-anchor approach as the
    # mutually-exclusive test (regex fires regardless of exit code; the
    # message only appears on the rejection path).
    add_test(NAME e2e.topo-profile.sampling_cpuprofile_sourcemap_root_without_cpuprofile_input
        COMMAND ${_TOPO_PROFILE_BIN} trace
                --mode sampling
                --sampling-input ${_SAMPLING_DEMO_FOLDED}
                --sourcemap-search-root ${_SOURCEMAP_DEMO_DIR})
    set_tests_properties(e2e.topo-profile.sampling_cpuprofile_sourcemap_root_without_cpuprofile_input PROPERTIES
        PASS_REGULAR_EXPRESSION "only applies to --cpuprofile-input"
        LABELS "e2e;topo-profile;topo-profile-sampling;topo-profile-typescript;toolchain"
        TIMEOUT 10)

    # `topo profile trace <bin> --mode hybrid
    # --sampling-input <folded>`: ObservabilityPass span boundaries
    # (exact, from the spawned binary) coexisting with sampling
    # (statistical, from the checked-in folded fixture) in ONE
    # profile document. The negative case below needs no fixtures, so it
    # lives inside this `if(TARGET topo-profile)` block. The positive
    # cpp/rust cases additionally need the spans_demo_{cpp,rs} targets and
    # are gated exactly like trace_cpp / trace_rust.
    #
    # Negative: `--mode hybrid` with a <binary> but no sampling-input flag
    # must exit non-zero (kExitUsage) with a clear diagnostic — no silent
    # fallback to spans-only. WILL_FAIL inverts CTest's pass/fail so the
    # non-zero exit is the expected outcome. Uses topo-profile itself as a
    # throwaway <binary> positional purely to satisfy the positional slot;
    # the missing-sampling-input check fires before the binary is spawned.
    add_test(NAME e2e.topo-profile.hybrid_missing_sampling_input_errors
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_TOPO_PROFILE_BIN} --mode hybrid)
    set_tests_properties(e2e.topo-profile.hybrid_missing_sampling_input_errors PROPERTIES
        WILL_FAIL TRUE
        LABELS "e2e;topo-profile;topo-profile-sampling;toolchain"
        TIMEOUT 10)
endif()

# hybrid positive (C++). Spawn the libtopo-observe-linked
# spans_demo_cpp for exact span boundaries AND ingest the host-agnostic
# stackcollapse-folded fixture for the sampling segment; one merged
# document must carry BOTH a non-empty spans[] (with stage/pipeline) AND
# the converted sampling{} segment. Gated exactly like trace_cpp.
if(TOPO_ENABLE_LLVM AND TARGET topo-profile AND TARGET spans_demo_cpp)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    set(_SPANS_DEMO_CPP "$<TARGET_FILE:spans_demo_cpp>")
    set(_SAMPLING_DEMO_FOLDED
        "${PROJECT_SOURCE_DIR}/topo-lang-cpp/topo-profile/test/fixtures/sampling_demo.folded")

    add_test(NAME e2e.topo-profile.hybrid_cpp
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_SPANS_DEMO_CPP}
                --mode hybrid
                --sampling-input ${_SAMPLING_DEMO_FOLDED}
                --backend cpp)
    set_tests_properties(e2e.topo-profile.hybrid_cpp PROPERTIES
        # CTest `PASS_REGULAR_EXPRESSION` uses OR semantics across a `;`
        # list and `.` never spans newlines, so a single regex cannot
        # assert "spans non-empty AND sampling present". Instead:
        #   PASS  — the folded `source_format` proves the sampling{}
        #           segment merged in (sampling-mode-only output).
        #   FAIL  — `"spans": []` would mean spans[] is empty; the
        #           spawned libtopo-observe binary always emits the three
        #           `pipeline::demo::stage{0,1,2}` spans, so a populated
        #           spans[] prints `"spans": [\n` instead and this never
        #           matches. Both conditions together ⇒ merged document.
        PASS_REGULAR_EXPRESSION "\"source_format\": \"stackcollapse_folded\""
        FAIL_REGULAR_EXPRESSION "\"spans\": \\[\\]"
        LABELS "e2e;topo-profile;topo-profile-sampling;toolchain"
        TIMEOUT 30)
endif()

# hybrid positive (Rust). Same merge, rust backend, gated
# exactly like trace_rust.
if(TOPO_ENABLE_LLVM AND NOT WIN32 AND TARGET topo-profile AND TARGET spans_demo_rs)
    set(_TOPO_PROFILE_BIN "$<TARGET_FILE:topo-profile>")
    get_target_property(_SPANS_DEMO_RS_FIXTURE spans_demo_rs TOPO_FIXTURE_PATH)
    set(_SAMPLING_DEMO_FOLDED
        "${PROJECT_SOURCE_DIR}/topo-lang-cpp/topo-profile/test/fixtures/sampling_demo.folded")

    add_test(NAME e2e.topo-profile.hybrid_rust
        COMMAND ${_TOPO_PROFILE_BIN} trace ${_SPANS_DEMO_RS_FIXTURE}
                --mode hybrid
                --sampling-input ${_SAMPLING_DEMO_FOLDED}
                --backend rust)
    set_tests_properties(e2e.topo-profile.hybrid_rust PROPERTIES
        # Same dual-anchor rationale as hybrid_cpp: PASS on the merged
        # sampling source_format, FAIL if spans[] came back empty.
        PASS_REGULAR_EXPRESSION "\"source_format\": \"stackcollapse_folded\""
        FAIL_REGULAR_EXPRESSION "\"spans\": \\[\\]"
        LABELS "e2e;topo-profile;topo-profile-sampling;toolchain"
        TIMEOUT 30)
endif()

# topo-debug-python debugpy adapter (label: topo-debug-python).
# Gated on the topo-debug-python custom target (which is itself gated on
# Python3 + an importable debugpy). Mirrors the tiny_vector cpp tests but
# against a `list[float]` value instead of a C `double[]`.
#
# `tiny_vector.py` declares `vec = [0.5, 1.5, ..., 7.5]` and the breakpoint
# on line 15 (`sentinel = 0`) fires after `vec` is bound. Sum 0.5..7.5 = 32.0,
# shape [8], dtype f64.
if(TARGET topo-debug-python)
    set(_TOPO_DEBUG_PY "${TOPO_DEBUG_PYTHON_LAUNCHER}")
    set(_TINY_VECTOR_PY "${TOPO_DEBUG_PYTHON_TINY_VECTOR}")

    add_test(NAME e2e.topo-debug-python.sum_vec
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(vec)"
            --break tiny_vector.py:15
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_TINY_VECTOR_PY})
    set_tests_properties(e2e.topo-debug-python.sum_vec PROPERTIES
        # 0.5+1.5+...+7.5 = 32.0
        PASS_REGULAR_EXPRESSION "^32(\\.0+)?\n"
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.shape_vec
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(vec)"
            --break tiny_vector.py:15
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_TINY_VECTOR_PY})
    set_tests_properties(e2e.topo-debug-python.shape_vec PROPERTIES
        PASS_REGULAR_EXPRESSION "^\\[8\\]\n"
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.dtype_vec
        COMMAND ${_TOPO_DEBUG_BIN} query "dtype(vec)"
            --break tiny_vector.py:15
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_TINY_VECTOR_PY})
    set_tests_properties(e2e.topo-debug-python.dtype_vec PROPERTIES
        PASS_REGULAR_EXPRESSION "^f64\n"
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    # stdlib `pdb` fallback when debugpy is unavailable.
    # TOPO_DEBUG_PY_FORCE_PDB=1 forces the degradation path even though
    # the configure-time venv has debugpy, so the fallback is exercised
    # deterministically in CI. Asserts BOTH that the clear degradation
    # notice is emitted when debugpy is unavailable AND that the basic
    # variable still reads correctly through the pdb subprocess for CLI
    # text output. CTest matches the merged stdout+stderr, so one regex
    # can require both.
    # Basic variable still reads correctly through the fallback (the
    # `^32` anchor proves the CLI text output is intact).
    add_test(NAME e2e.topo-debug-python.pdb_fallback_sum_vec
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(vec)"
            --break tiny_vector.py:15
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_TINY_VECTOR_PY})
    set_tests_properties(e2e.topo-debug-python.pdb_fallback_sum_vec PROPERTIES
        ENVIRONMENT "TOPO_DEBUG_PY_FORCE_PDB=1"
        PASS_REGULAR_EXPRESSION "(^|\n)32(\\.0+)?\n"
        LABELS "e2e;topo-debug-python;topo-debug-python-pdb-fallback;toolchain"
        TIMEOUT 60)

    # Clear degradation notice is emitted.
    # Separate test because CTest PASS_REGULAR_EXPRESSION is OR-only and
    # cannot require both the notice and the numeric result at once.
    add_test(NAME e2e.topo-debug-python.pdb_fallback_notice
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(vec)"
            --break tiny_vector.py:15
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_TINY_VECTOR_PY})
    set_tests_properties(e2e.topo-debug-python.pdb_fallback_notice PROPERTIES
        ENVIRONMENT "TOPO_DEBUG_PY_FORCE_PDB=1"
        PASS_REGULAR_EXPRESSION "debugpy not available — falling back to stdlib pdb"
        LABELS "e2e;topo-debug-python;topo-debug-python-pdb-fallback;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.pdb_fallback_dtype_vec
        COMMAND ${_TOPO_DEBUG_BIN} query "dtype(vec)"
            --break tiny_vector.py:15
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_TINY_VECTOR_PY})
    set_tests_properties(e2e.topo-debug-python.pdb_fallback_dtype_vec PROPERTIES
        ENVIRONMENT "TOPO_DEBUG_PY_FORCE_PDB=1"
        PASS_REGULAR_EXPRESSION "(^|\n)f64\n"
        LABELS "e2e;topo-debug-python;topo-debug-python-pdb-fallback;toolchain"
        TIMEOUT 60)

    # stdlib-bridge default summaries. A bare-identifier
    # query against a uuid / decimal128 / ndarray / time_ns host value
    # yields a human-readable summary instead of an exit-4 "unsupported
    # type". NOTE: an earlier bridging-types attempt never delivered these
    # as declared Topo bridging types — the host-value classifier classifies
    # the Python *host value* by runtime type, the realizable form of the
    # criterion (see the open issue on stdlib bridging-type summaries).
    set(_STDLIB_BRIDGE_PY "${TOPO_DEBUG_PYTHON_STDLIB_BRIDGE}")

    # The stdlib-bridge summaries require the full debugpy DAP path: the
    # stdlib `pdb` fallback only supports the Phase-2a basic list[int]/
    # list[float] subset and cannot satisfy the uuid/decimal/time_ns
    # assertions. When debugpy is absent these previously HARD-FAILED,
    # making "debugpy not installed" indistinguishable from a regression.
    # Configure already records TOPO_DEBUG_PYTHON_HAS_DEBUGPY; gate the
    # group on it and, when absent, register one SKIP-with-reason test
    # (same SKIPPED: / SKIP_REGULAR_EXPRESSION idiom used elsewhere in
    # this file) so the gap stays visible as a skip, not a silent drop.
    # Issue: topo-debug-python-e2e-suite-fails-without-debugpy.
    if(TOPO_DEBUG_PYTHON_HAS_DEBUGPY)
    add_test(NAME e2e.topo-debug-python.stdlib_bridge_uuid
        COMMAND ${_TOPO_DEBUG_BIN} query "u"
            --break stdlib_bridge.py:24
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_STDLIB_BRIDGE_PY})
    set_tests_properties(e2e.topo-debug-python.stdlib_bridge_uuid PROPERTIES
        PASS_REGULAR_EXPRESSION "550e8400-e29b-41d4-a716-446655440000"
        LABELS "e2e;topo-debug-python;topo-debug-python-stdlib-bridge;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.stdlib_bridge_decimal
        COMMAND ${_TOPO_DEBUG_BIN} query "d"
            --break stdlib_bridge.py:24
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_STDLIB_BRIDGE_PY})
    set_tests_properties(e2e.topo-debug-python.stdlib_bridge_decimal PROPERTIES
        PASS_REGULAR_EXPRESSION "-12345\\.67890"
        LABELS "e2e;topo-debug-python;topo-debug-python-stdlib-bridge;toolchain"
        TIMEOUT 60)

    # time_ns: assert an ISO-8601 timestamp was rendered (the exact
    # instant is not the point — "human-readable instead of a raw int" is).
    add_test(NAME e2e.topo-debug-python.stdlib_bridge_time_ns
        COMMAND ${_TOPO_DEBUG_BIN} query "t"
            --break stdlib_bridge.py:24
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_STDLIB_BRIDGE_PY})
    set_tests_properties(e2e.topo-debug-python.stdlib_bridge_time_ns PROPERTIES
        PASS_REGULAR_EXPRESSION "20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]"
        LABELS "e2e;topo-debug-python;topo-debug-python-stdlib-bridge;toolchain"
        TIMEOUT 60)

    # ndarray case is numpy-gated at configure time (explicit gate, not a
    # silent skip; the other three always run).
    if(TOPO_DEBUG_PYTHON_HAS_NUMPY)
        add_test(NAME e2e.topo-debug-python.stdlib_bridge_ndarray
            COMMAND ${_TOPO_DEBUG_BIN} query "arr"
                --break stdlib_bridge.py:24
                --adapter ${_TOPO_DEBUG_PY}
                --target ${_STDLIB_BRIDGE_PY})
        set_tests_properties(e2e.topo-debug-python.stdlib_bridge_ndarray PROPERTIES
            PASS_REGULAR_EXPRESSION "ndarray shape=\\(3, 4\\) dtype=float64 \\[1\\.0, 2\\.0"
            LABELS "e2e;topo-debug-python;topo-debug-python-stdlib-bridge;toolchain"
            TIMEOUT 60)
    endif()
    else()
        # debugpy absent: pdb fallback cannot satisfy these summaries.
        # Surface the whole group as ONE explicit skip-with-reason rather
        # than dropping it (silent drop would re-hide the gap) or
        # hard-failing (red would masquerade as a regression).
        add_test(NAME e2e.topo-debug-python.stdlib_bridge.SKIPPED_no_debugpy
            COMMAND ${CMAKE_COMMAND} -E echo
                "SKIPPED: debugpy not importable — stdlib-bridge \
summaries (uuid/decimal/time_ns/ndarray) require the full debugpy DAP \
path; the stdlib pdb fallback supports only the basic list subset. \
Install debugpy and re-run cmake to enable these.")
        set_tests_properties(e2e.topo-debug-python.stdlib_bridge.SKIPPED_no_debugpy
            PROPERTIES
            SKIP_REGULAR_EXPRESSION "SKIPPED:"
            LABELS "e2e;topo-debug-python;topo-debug-python-stdlib-bridge;toolchain"
            TIMEOUT 5)
    endif()
endif()

# Python topo-debug *project-level* fixtures.
#
# Counterpart of the cpp/rust/java project_simple / project_multi blocks
# above. Unlike tiny_vector.py (a single script driven through the adapter),
# these fixtures run the full `topo build` → topo-build-python → main.topo-dbg.json
# pipeline. Each fixture is staged at configure time in
# topo-lang-python/topo-debug/test/CMakeLists.txt
# (TOPO_DEBUG_PYTHON_PROJECT_*_DIR vars). Python is interpreted, so the
# `--target` is `main.py` itself; debugpy launches the script with
# --wait-for-client, hits the literal `sentinel = 0` line, and the adapter
# reads `data` / `a` / `b` from the stopped frame.
#
# DAP handshake quirks (ground truth): debugpy 1.8+ defers the
# `attach` response until after `configurationDone`, so the adapter fires
# attach non-blocking, waits for `initialized`, sets breakpoints, sends
# `configurationDone`, then drains the deferred attach response. These
# tests exercise that path every spawn.
if(TARGET topo-debug-python)
    set(_TOPO_DEBUG_PY "${TOPO_DEBUG_PYTHON_LAUNCHER}")
    set(_TOPO_BUILD_BIN "$<TARGET_FILE:topo-build>")

    # The whole project_simple/project_multi block drives the debugpy DAP
    # path (see the handshake note above). Without debugpy the adapter
    # falls back to stdlib pdb, which cannot satisfy the view/summary
    # assertions, so these previously hard-failed and looked like a
    # regression. Gate the block on the configure-recorded
    # TOPO_DEBUG_PYTHON_HAS_DEBUGPY; when absent, register a single
    # explicit skip-with-reason instead of dropping or failing.
    # Issue: topo-debug-python-e2e-suite-fails-without-debugpy.
    if(TOPO_DEBUG_PYTHON_HAS_DEBUGPY)

    # project_simple — single Python list `data` of length 8 with two
    # literal-bounded halves + summary + snapshot/replay. Sentinel
    # breakpoint is main.py:25.
    #   sum(first_half)  = 1+2+3+4         = 10
    #   sum(second_half) = 10+20+30+40     = 100
    #   sum(data)        = 110
    #   shape(first_half)                  = [4]
    set(_PROJECT_SIMPLE_PY_DIR ${TOPO_DEBUG_PYTHON_PROJECT_SIMPLE_DIR})
    set(_PROJECT_SIMPLE_PY_TARGET ${_PROJECT_SIMPLE_PY_DIR}/main.py)
    # `topo-build-python` is check-only — it doesn't link a binary, but
    # `topo-build` still writes `<output>.topo-dbg.json` next to where the
    # output would live. With `[build] output = "main"`, the dbg.json lives
    # at `<dir>/main.topo-dbg.json`; pass it explicitly via --debug-meta
    # because auto-discovery looks for `<target>.topo-dbg.json` and our
    # `--target` is the .py script (a different basename).
    set(_PROJECT_SIMPLE_PY_DBG ${_PROJECT_SIMPLE_PY_DIR}/main.topo-dbg.json)

    add_test(NAME e2e.topo-debug-python.project_simple.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_simple.build PROPERTIES
        FIXTURES_SETUP project_simple_py_built
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.project_simple.view_first_half_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(first_half)"
            --break main.py:25
            --debug-meta ${_PROJECT_SIMPLE_PY_DBG}
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_PROJECT_SIMPLE_PY_TARGET}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_simple.view_first_half_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_py_built
        PASS_REGULAR_EXPRESSION "^10\n"
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.project_simple.view_second_half_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(second_half)"
            --break main.py:25
            --debug-meta ${_PROJECT_SIMPLE_PY_DBG}
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_PROJECT_SIMPLE_PY_TARGET}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_simple.view_second_half_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_py_built
        PASS_REGULAR_EXPRESSION "^100\n"
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.project_simple.view_first_half_shape
        COMMAND ${_TOPO_DEBUG_BIN} query "shape(first_half)"
            --break main.py:25
            --debug-meta ${_PROJECT_SIMPLE_PY_DBG}
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_PROJECT_SIMPLE_PY_TARGET}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_simple.view_first_half_shape PROPERTIES
        FIXTURES_REQUIRED project_simple_py_built
        PASS_REGULAR_EXPRESSION "^\\[4\\]\n"
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    # Back-compat: raw variable name (no view rewrite) still resolves against
    # the same target — the dbg.json + raw-var coexistence doesn't break the
    # tiny_vector contract for projects that happen to have both.
    add_test(NAME e2e.topo-debug-python.project_simple.raw_data_sum
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data)"
            --break main.py:25
            --debug-meta ${_PROJECT_SIMPLE_PY_DBG}
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_PROJECT_SIMPLE_PY_TARGET}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_simple.raw_data_sum PROPERTIES
        FIXTURES_REQUIRED project_simple_py_built
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    # `summary` subcommand renders `summary_template` against a live frame.
    # Expected: `data sum=110 (halves 10 + 100; count=8)`.
    add_test(NAME e2e.topo-debug-python.project_simple.summary
        COMMAND ${_TOPO_DEBUG_BIN} summary "Buf"
            --break main.py:25
            --debug-meta ${_PROJECT_SIMPLE_PY_DBG}
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_PROJECT_SIMPLE_PY_TARGET}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_simple.summary PROPERTIES
        FIXTURES_REQUIRED project_simple_py_built
        PASS_REGULAR_EXPRESSION "^data sum=110 \\(halves 10 \\+ 100; count=8\\)\n"
        LABELS "e2e;topo-debug-python;toolchain;topo-debug-summary"
        TIMEOUT 60)

    # `snapshot` capture + `query --replay` end-to-end. Step A captures a
    # TOPO_SNAP_V1 file from the live target. Step B replays the snapshot
    # (no adapter spawn) and asserts the same `110` answer.
    set(_PROJECT_SIMPLE_PY_SNAP
        ${CMAKE_CURRENT_BINARY_DIR}/project_simple_py_buf.snap)
    add_test(NAME e2e.topo-debug-python.project_simple.snapshot_capture
        COMMAND ${_TOPO_DEBUG_BIN} snapshot "Buf"
            --break main.py:25
            --debug-meta ${_PROJECT_SIMPLE_PY_DBG}
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_PROJECT_SIMPLE_PY_TARGET}
            --out ${_PROJECT_SIMPLE_PY_SNAP}
        WORKING_DIRECTORY ${_PROJECT_SIMPLE_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_simple.snapshot_capture PROPERTIES
        FIXTURES_REQUIRED project_simple_py_built
        FIXTURES_SETUP project_simple_py_snapshot
        LABELS "e2e;topo-debug-python;toolchain;topo-debug-snapshot"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.project_simple.snapshot_then_replay
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(data)"
            --replay ${_PROJECT_SIMPLE_PY_SNAP})
    set_tests_properties(e2e.topo-debug-python.project_simple.snapshot_then_replay PROPERTIES
        FIXTURES_REQUIRED "project_simple_py_built;project_simple_py_snapshot"
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-python;toolchain;topo-debug-snapshot"
        TIMEOUT 30)

    # project_multi — two list[int] locals in scope, summary template
    # references both. Sentinel breakpoint is main.py:20.
    #   sum(a)              = 10
    #   sum(b)              = 100
    #   sum(a)+sum(b)       = 110
    #   max(b)-max(a)       = 36
    set(_PROJECT_MULTI_PY_DIR ${TOPO_DEBUG_PYTHON_PROJECT_MULTI_DIR})
    set(_PROJECT_MULTI_PY_TARGET ${_PROJECT_MULTI_PY_DIR}/main.py)
    set(_PROJECT_MULTI_PY_DBG ${_PROJECT_MULTI_PY_DIR}/main.topo-dbg.json)

    add_test(NAME e2e.topo-debug-python.project_multi.build
        COMMAND ${_TOPO_BUILD_BIN}
        WORKING_DIRECTORY ${_PROJECT_MULTI_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_multi.build PROPERTIES
        FIXTURES_SETUP project_multi_py_built
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.project_multi.sum_a
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(a)"
            --break main.py:20
            --debug-meta ${_PROJECT_MULTI_PY_DBG}
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_PROJECT_MULTI_PY_TARGET}
        WORKING_DIRECTORY ${_PROJECT_MULTI_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_multi.sum_a PROPERTIES
        FIXTURES_REQUIRED project_multi_py_built
        PASS_REGULAR_EXPRESSION "^10\n"
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.project_multi.sum_b
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(b)"
            --break main.py:20
            --debug-meta ${_PROJECT_MULTI_PY_DBG}
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_PROJECT_MULTI_PY_TARGET}
        WORKING_DIRECTORY ${_PROJECT_MULTI_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_multi.sum_b PROPERTIES
        FIXTURES_REQUIRED project_multi_py_built
        PASS_REGULAR_EXPRESSION "^100\n"
        LABELS "e2e;topo-debug-python;toolchain"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.project_multi.sum_a_plus_b
        COMMAND ${_TOPO_DEBUG_BIN} query "sum(a) + sum(b)"
            --break main.py:20
            --debug-meta ${_PROJECT_MULTI_PY_DBG}
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_PROJECT_MULTI_PY_TARGET}
        WORKING_DIRECTORY ${_PROJECT_MULTI_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_multi.sum_a_plus_b PROPERTIES
        FIXTURES_REQUIRED project_multi_py_built
        PASS_REGULAR_EXPRESSION "^110\n"
        LABELS "e2e;topo-debug-python;toolchain;topo-debug-multivar"
        TIMEOUT 60)

    add_test(NAME e2e.topo-debug-python.project_multi.summary
        COMMAND ${_TOPO_DEBUG_BIN} summary "TwoArrays"
            --break main.py:20
            --debug-meta ${_PROJECT_MULTI_PY_DBG}
            --adapter ${_TOPO_DEBUG_PY}
            --target ${_PROJECT_MULTI_PY_TARGET}
        WORKING_DIRECTORY ${_PROJECT_MULTI_PY_DIR})
    set_tests_properties(e2e.topo-debug-python.project_multi.summary PROPERTIES
        FIXTURES_REQUIRED project_multi_py_built
        PASS_REGULAR_EXPRESSION "^a=10, b=100, total=110, span=36\n"
        LABELS "e2e;topo-debug-python;toolchain;topo-debug-multivar;topo-debug-summary"
        TIMEOUT 60)
    else()
        # debugpy absent: the project_simple/project_multi DAP fixtures
        # cannot run. Surface as one explicit skip-with-reason (not a
        # silent drop, not a red failure).
        add_test(NAME e2e.topo-debug-python.project.SKIPPED_no_debugpy
            COMMAND ${CMAKE_COMMAND} -E echo
                "SKIPPED: debugpy not importable — project_simple.* and \
project_multi.* drive the full debugpy DAP path (view/summary/snapshot \
assertions); the stdlib pdb fallback cannot satisfy them. Install \
debugpy and re-run cmake to enable these.")
        set_tests_properties(e2e.topo-debug-python.project.SKIPPED_no_debugpy
            PROPERTIES
            SKIP_REGULAR_EXPRESSION "SKIPPED:"
            LABELS "e2e;topo-debug-python;toolchain"
            TIMEOUT 5)
    endif()
endif()
# Note: the "Python3 found but topo-debug-python target missing" verification
# gap previously enforced here as e2e.topo-debug-python.MISSING was moved to
# topo-lang-python/test/CMakeLists.txt — the contract holder for "if you
# import me, I must provide topo-debug-python".

# Feature dimension labels
set_property(DIRECTORY APPEND PROPERTY
    TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/FeatureMap.cmake")
