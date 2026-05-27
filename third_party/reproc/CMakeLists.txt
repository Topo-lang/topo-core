# third_party/reproc — vendored cross-platform subprocess library
#
# Upstream:  https://github.com/DaanDeMeyer/reproc
# License:   MIT (verified against tag v14.2.7 LICENSE — see THIRD-PARTY.md)
# Used by:   topo-core/lib/Platform/Process.cpp (subprocess driver backend)
#
# Pulled via FetchContent at a pinned release tag. Tests and examples are
# disabled to keep our configure/build slim. REPROC_INSTALL is ON so that
# `cmake --install` of topo-core also installs reproc's libraries, headers,
# and CMake config files into the same prefix — topo-coreConfig.cmake's
# find_dependency(reproc++) then resolves transparently for standalone
# downstream repos (split-topo-core-static-lib-transitive-reproc-leak).

include(FetchContent)

set(REPROC++ ON CACHE BOOL "" FORCE)
set(REPROC_TEST OFF CACHE BOOL "" FORCE)
set(REPROC_EXAMPLES OFF CACHE BOOL "" FORCE)
set(REPROC_INSTALL ON CACHE BOOL "" FORCE)
set(REPROC_INSTALL_PKGCONFIG OFF CACHE BOOL "" FORCE)
# reproc installs its <Pkg>Config.cmake under share/<pkg>/ — line up with
# topo-coreConfig.cmake's find_dependency(... PATHS "${PACKAGE_PREFIX_DIR}/share")
set(REPROC_INSTALL_CMAKECONFIGDIR "share" CACHE STRING "" FORCE)

FetchContent_Declare(reproc
    GIT_REPOSITORY https://github.com/DaanDeMeyer/reproc.git
    GIT_TAG        v14.2.7
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(reproc)

# reproc emits a handful of warnings under our project-wide -Wall/-Wextra and
# the C++23 deprecation knob we set globally; suppress them by marking the
# reproc include trees as SYSTEM headers for consumers.
if(TARGET reproc)
    get_target_property(_REPROC_INCLUDES reproc INTERFACE_INCLUDE_DIRECTORIES)
    if(_REPROC_INCLUDES)
        set_target_properties(reproc PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_REPROC_INCLUDES}")
    endif()
endif()
if(TARGET reproc++)
    get_target_property(_REPROCXX_INCLUDES reproc++ INTERFACE_INCLUDE_DIRECTORIES)
    if(_REPROCXX_INCLUDES)
        set_target_properties(reproc++ PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_REPROCXX_INCLUDES}")
    endif()
endif()
