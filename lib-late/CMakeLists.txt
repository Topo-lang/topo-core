# topo-core/lib-late/ — embedded-mode late hook for the topo-lang-gated
# libs (TopoBuildLib, TopoTranspile).
#
# When topo-core's root CMakeLists is configured with
# TOPO_CORE_DEFER_LANG_SUBDIRS=ON, it skips lib/Build + lib/Transpile
# because topo::lang::TopoLang has not yet been registered. A parent
# project (the Topo meta-repo) then add_subdirectory's THIS file after
# topo-lang is added, so the lang-dependent libs can resolve their
# PUBLIC link to topo::lang::TopoLang.
#
# Standalone topo-core never enters this path — TOPO_CORE_DEFER_LANG_SUBDIRS
# defaults OFF, the root's normal gate runs, and lib-late/ is unused.
#
# The lib targets are configured against the topo-core source tree (one
# directory up) but their CMake binary dir lives under lib-late/ so the
# parent's build tree stays clean. Once configured the targets are
# globally visible as `TopoBuildConfig`, `TopoBuildLib`, `TopoTranspile`
# plus their `topo::core::*` ALIASes — exactly as in the
# non-deferred path.

if(NOT TARGET topo::lang::TopoLang)
    find_package(topo-lang CONFIG QUIET)
endif()
if(NOT TARGET topo::lang::TopoLang)
    message(FATAL_ERROR
        "topo-core/lib-late requires topo::lang::TopoLang but neither the "
        "ALIAS target nor an installed topo-lang were found. The parent "
        "project must add_subdirectory(topo-lang) (or install topo-lang on "
        "CMAKE_PREFIX_PATH) before add_subdirectory(topo-core/lib-late).")
endif()

# Mark TOPO_CORE_WITH_LANG ON in case any peer module checks it.
set(TOPO_CORE_WITH_LANG ON CACHE INTERNAL "topo-core built with topo-lang gate ON")

add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../lib/Build
    ${CMAKE_CURRENT_BINARY_DIR}/Build
)
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../lib/Transpile
    ${CMAKE_CURRENT_BINARY_DIR}/Transpile
)
