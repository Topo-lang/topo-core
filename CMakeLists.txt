cmake_minimum_required(VERSION 3.20)

project(topo-core
    VERSION 0.1.0
    DESCRIPTION "Topo declaration-language core — frontend, analysis, check, build/transpile orchestration"
    HOMEPAGE_URL "https://github.com/topo-lang/topo-core"
    LANGUAGES C CXX
)

# topo-core: Language core (zero LLVM dependency)
# Libraries: Platform, Basic, Stdlib, Lexer, AST, Parser, Sema, Analysis,
# Check, Build, Distribution, Format, Transform, Transpile, Debug, Profile,
# LSP.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TopoCompilerFlags)

# ── External dependencies ─────────────────────────────
# nlohmann_json — JSON I/O across the toolchain (CLI JSON modes, debug
# adapter wire protocol, etc.). vcpkg.json pins via the builtin-baseline.
find_package(nlohmann_json CONFIG REQUIRED)

# reproc++ — cross-platform subprocess library. Vendored in
# third_party/reproc/ and pulled via FetchContent; the install rules
# co-publish reproc artifacts under <prefix>/share/{reproc,reproc++}
# so downstream find_package(topo-core) finds them transparently via
# topo-coreConfig.cmake's find_dependency(reproc++ CONFIG PATHS ...).
add_subdirectory(third_party/reproc)

# ── Libraries ─────────────────────────────────────────
add_subdirectory(lib/Platform)
add_subdirectory(lib/Basic)
add_subdirectory(lib/Stdlib)
add_subdirectory(lib/Lexer)
add_subdirectory(lib/AST)
add_subdirectory(lib/Parser)
add_subdirectory(lib/Sema)
add_subdirectory(lib/Analysis)
add_subdirectory(lib/Check)
add_subdirectory(lib/Distribution)
add_subdirectory(lib/Format)
add_subdirectory(lib/Transform)
add_subdirectory(lib/Debug)
add_subdirectory(lib/Profile)

# ── Plugin-aware libs (gated on topo-lang) ────────────
# TopoBuildLib and TopoTranspile PUBLIC-depend on TopoLang for plugin
# registration / dispatch. In monorepo builds topo-lang/ is add_subdirectory'd
# alongside topo-core/, so the targets are available. In a standalone
# topo-core build we resolve topo-lang via find_package(topo-lang) — and
# skip the lib *targets* if topo-lang is not installed, so the frontend +
# check + fmt + Debug + Profile + Distribution closure still builds.
# Phase-4 sequencing: lift topo-core (this gate stays OFF) → lift topo-lang
# → rebuild topo-core with topo-lang on the prefix path (this gate
# auto-flips ON).
#
# The Build / Backend / Transpile *headers* are installed unconditionally
# below (out of the lib subdir gate) because downstream packages reference
# them even when the lib targets are skipped — e.g. topo-lang's
# CheckRunnerBase.h includes <topo/Build/PassConfig.h>.
#
# Embedded under the meta-repo: meta sets TOPO_CORE_DEFER_LANG_SUBDIRS=ON
# before add_subdirectory(topo-core) because topo-lang is added AFTER
# topo-core in the meta's topological order (consumers come after
# providers). The lang-subdirs are then added back via
# `add_subdirectory(topo-core/lib-late ...)` after topo-lang has registered
# topo::lang::TopoLang.
option(TOPO_CORE_DEFER_LANG_SUBDIRS
    "Skip lib/Build + lib/Transpile here; a parent project will add them later via lib-late/"
    OFF)
if(NOT TOPO_CORE_DEFER_LANG_SUBDIRS)
    if(NOT TARGET topo::lang::TopoLang)
        # Set the cycle-guard sentinel before find_package(topo-lang) so
        # topo-langConfig skips find_dependency(topo-core) — otherwise
        # that cascade tries to import the previously-installed
        # topo::core::* targets, which conflict with the same-named
        # targets/ALIASes already defined by this in-progress topo-core
        # configure. See topo-langConfig.cmake.in for the matching guard.
        set(_topo_core_loading TRUE)
        find_package(topo-lang CONFIG QUIET)
        unset(_topo_core_loading)
    endif()
    if(TARGET topo::lang::TopoLang)
        message(STATUS "topo-lang found — building TopoBuildLib + TopoTranspile")
        add_subdirectory(lib/Build)
        add_subdirectory(lib/Transpile)
        set(TOPO_CORE_WITH_LANG ON)
    else()
        message(STATUS "topo-lang not on CMAKE_PREFIX_PATH — skipping "
            "TopoBuildLib + TopoTranspile lib targets (still installing their "
            "public headers). Set -DCMAKE_PREFIX_PATH=<topo-lang-install-prefix> "
            "to enable them.")
        set(TOPO_CORE_WITH_LANG OFF)
    endif()
else()
    message(STATUS "topo-lang subdirs deferred (TOPO_CORE_DEFER_LANG_SUBDIRS=ON); "
        "parent project will add topo-core/lib-late after topo-lang is registered")
    set(TOPO_CORE_WITH_LANG OFF)
endif()

# Public headers for the gated subsystems — install regardless of whether
# the lib targets above are built.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/topo/Build
    DESTINATION include/topo
    COMPONENT topo-core-build
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/topo/Backend
    DESTINATION include/topo
    COMPONENT topo-core-build
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/topo/Transpile
    DESTINATION include/topo
    COMPONENT topo-core-transpile
    FILES_MATCHING PATTERN "*.h"
)
# LSP bridge interface — base class for host LSP bridges (clangd, jdtls,
# pyright, rust-analyzer, tsserver). Lives in topo-core so language packages
# and topo-v8 do not need a link-time dep on topo-lsp.
add_subdirectory(lib/LSP)

# ── Tools ─────────────────────────────────────────────
add_subdirectory(tools/topo)
add_subdirectory(tools/topo-fmt)
# debug mock adapter + CLI (depends only on TopoDebug).
add_subdirectory(tools/topo-debug-mock-adapter)
add_subdirectory(tools/topo-debug)
# topo-build / topo-transpile / topo-check / topo-init / topo-profile
# (plugin-dispatch CLIs) live in the separate topo-cli repo (they link every
# language plugin and so must be configured after every topo-lang-* package
# resolves).

# ── Tests (off by default to keep consumer / installer builds cheap) ─
# Mirrors the topo-tpm pattern: a top-level option that gates the entire
# test tree behind PROJECT_IS_TOP_LEVEL so `find_package(topo-core)` users
# never pay the test build cost. The standalone test tree self-skips
# language-plugin-dependent suites when topo-lang is not on the prefix
# path (TOPO_CORE_WITH_LANG OFF) and self-skips tests that pull source
# files from topo-cli when TOPO_CORE_TOPO_CLI_SOURCE_DIR is unset.
option(TOPO_CORE_BUILD_TESTS "Build topo-core tests" OFF)

# Optional opt-in: point this at the topo-cli source tree (the
# repo or worktree containing tools/topo-build/ and tools/topo-init/)
# to enable the test/build/ and test/init/ suites that link CLI .cpp
# sources directly. Unset by default — standalone topo-core skips
# those suites and they live in topo-cli's own test tree.
set(TOPO_CORE_TOPO_CLI_SOURCE_DIR "" CACHE PATH
    "Path to a topo-cli source tree (provides tools/topo-build/ and \
tools/topo-init/ headers/cpp sources used by select test suites). \
Leave empty in standalone builds.")

if(TOPO_CORE_BUILD_TESTS AND (PROJECT_IS_TOP_LEVEL OR TOPO_META_DRIVING_TESTS))
    enable_testing()
    # GoogleTest setup — only needed when topo-core drives its own tests
    # (PROJECT_IS_TOP_LEVEL) or meta is driving but topo-core is the only
    # consumer in that scope. Meta-embedded mode where another sibling
    # already FetchContent'd googletest gets the targets transparently via
    # CMake's global target registry; the second FetchContent here would
    # short-circuit because the source dir is already populated.
    if(NOT TARGET GTest::gtest_main)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)
    include(GoogleTest)
    add_subdirectory(test)
endif()

# ─── install-component metadata ────────────────────────────
# Export topo-core targets so downstream packages can:
#     find_package(topo-core COMPONENTS frontend check fmt ...)
#     target_link_libraries(... PRIVATE topo::core::TopoCheck topo::core::TopoSema)
# The export set TopoCoreTargets is populated by per-lib install(TARGETS ...
# EXPORT TopoCoreTargets COMPONENT topo-core-<comp>) rules in lib/<X>/.
#
# Export scope: EXPORT covers the targets whose PUBLIC dependency closure
# stays inside topo-core — Platform/Basic/Lexer/AST/Parser/Sema (frontend),
# Analysis/Check (check), Format/Transform (fmt), Distribution (the
# `topo backend` CLI; PUBLIC-depends only on TopoPlatform). Targets that
# PUBLIC-depend on TopoLang (TopoBuildLib, TopoTranspile, TopoCheckRunner)
# ship as install components without EXPORT because cross-package
# find_dependency(topo-lang) plumbing is out of scope. The namespaced
# targets topo::core::build / topo::core::transpile are therefore deferred
# to a follow-up once topo-lang has its own export surface.
#
# The EXPORT install itself ships under the frontend component because every
# consumer (check-only, fmt-only, etc.) needs the CMake metadata to resolve
# transitive PUBLIC link edges.

# Test fixture directories — exposed as cache variables so downstream
# (topo-llvm/test, topo-lsp/test, topo-jvm/test/e2e) reference a single
# variable instead of hardcoding ${CMAKE_CURRENT_SOURCE_DIR}/test/... .
# After 1:1 repo split, topo-coreConfig.cmake sets these to the installed
# location (share/topo-core/{fixtures,build-fixtures}) so consumers don't
# need to know whether they're running in-tree or against a find_package'd
# install. The fixtures themselves install via the topo-core-fixtures
# component below.
set(TOPO_CORE_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures"
    CACHE PATH "Path to topo-core's .topo test fixtures (source tree).")
set(TOPO_CORE_BUILD_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test/build/fixtures"
    CACHE PATH "Path to topo-core's build/e2e test project fixtures (source tree).")

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/
    DESTINATION share/topo-core/fixtures
    COMPONENT topo-core-fixtures
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test/build/fixtures/
    DESTINATION share/topo-core/build-fixtures
    COMPONENT topo-core-fixtures
)

install(EXPORT TopoCoreTargets
    FILE TopoCoreTargets.cmake
    NAMESPACE topo::core::
    DESTINATION lib/cmake/topo-core
    COMPONENT topo-core-frontend
)

# The topo-lang-linked targets (TopoBuildLib / TopoTranspile) live in a
# SECOND export set so the config can include base targets before pulling
# topo-lang (whose Targets file references topo::core::* back). Installed
# here only on the standalone gate-ON path; in deferred (meta) mode the
# set is populated and installed by lib-late/ instead.
if(TOPO_CORE_WITH_LANG)
    install(EXPORT TopoCoreLangTargets
        FILE TopoCoreLangTargets.cmake
        NAMESPACE topo::core::
        DESTINATION lib/cmake/topo-core
        COMPONENT topo-core-frontend
    )
endif()

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/topo-coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/topo-coreConfig.cmake
    INSTALL_DESTINATION lib/cmake/topo-core
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/topo-coreConfigVersion.cmake
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/topo-coreConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/topo-coreConfigVersion.cmake
    DESTINATION lib/cmake/topo-core
    COMPONENT topo-core-frontend
)
