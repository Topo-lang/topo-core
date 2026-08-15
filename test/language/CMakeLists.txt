# Spec-based .topo file tests — validate frontend parsing and semantics
# Driven by `topo --check` (compiler frontend, zero LLVM dependency)

set(TOPO_LANGUAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(SPEC_FEATURES syntax declarations stages visibility multireturn pipeline handler_flow classes templates ownership priority lifetime hints containment stdlib debug)

foreach(FEATURE ${SPEC_FEATURES})
    # Valid files: expect topo --check to succeed
    file(GLOB VALID_FILES "${TOPO_LANGUAGE_DIR}/spec/${FEATURE}/valid/*.topo")
    foreach(F ${VALID_FILES})
        get_filename_component(FNAME ${F} NAME_WE)
        add_test(NAME "lang.${FEATURE}.valid.${FNAME}"
            COMMAND topo --check "${F}")
        set_tests_properties("lang.${FEATURE}.valid.${FNAME}"
            PROPERTIES LABELS "language;${FEATURE}" TIMEOUT 30)
    endforeach()

    # Invalid files: expect topo --check to fail (non-zero exit)
    file(GLOB INVALID_FILES "${TOPO_LANGUAGE_DIR}/spec/${FEATURE}/invalid/*.topo")
    foreach(F ${INVALID_FILES})
        get_filename_component(FNAME ${F} NAME_WE)
        add_test(NAME "lang.${FEATURE}.invalid.${FNAME}"
            COMMAND topo --check "${F}")
        set_tests_properties("lang.${FEATURE}.invalid.${FNAME}"
            PROPERTIES LABELS "language;${FEATURE}" TIMEOUT 30
            WILL_FAIL TRUE)
    endforeach()
endforeach()

# Recovery-count pins: invalid fixtures whose DIAGNOSTIC COUNT is asserted
# (bounded first-error-plus-resync output, not a wall of same-position
# repeats). ctest's regex properties match per output line, so the count
# check runs in a cmake -P script (cross-platform, no shell pipeline).
# Each fixture carries its own bound as a comment:
#   // EXPECT-COUNT: <diagnostic text>, <min>, <max>
foreach(FEATURE ${SPEC_FEATURES})
    file(GLOB RECOVERY_FILES "${TOPO_LANGUAGE_DIR}/spec/${FEATURE}/recovery/*.topo")
    foreach(F ${RECOVERY_FILES})
        file(STRINGS ${F} _count_spec REGEX "^// EXPECT-COUNT:")
        list(LENGTH _count_spec _count_spec_len)
        if(NOT _count_spec_len EQUAL 1)
            message(FATAL_ERROR "${F}: recovery fixture must carry exactly one '// EXPECT-COUNT: <text>, <min>, <max>' line")
        endif()
        string(REGEX REPLACE "^// EXPECT-COUNT: " "" _count_spec "${_count_spec}")
        string(REPLACE "," ";" _count_parts "${_count_spec}")
        list(LENGTH _count_parts _count_parts_len)
        if(NOT _count_parts_len EQUAL 3)
            message(FATAL_ERROR "${F}: '// EXPECT-COUNT:' must have three comma-separated fields")
        endif()
        list(GET _count_parts 0 _pattern)
        list(GET _count_parts 1 _min_count)
        list(GET _count_parts 2 _max_count)
        string(STRIP "${_pattern}" _pattern)
        string(STRIP "${_min_count}" _min_count)
        string(STRIP "${_max_count}" _max_count)

        get_filename_component(FNAME ${F} NAME_WE)
        add_test(NAME "lang.${FEATURE}.recovery.${FNAME}"
            COMMAND ${CMAKE_COMMAND}
                "-DTOPO_BIN=$<TARGET_FILE:topo>"
                "-DFIXTURE=${F}"
                "-DPATTERN=${_pattern}"
                "-DMIN_COUNT=${_min_count}"
                "-DMAX_COUNT=${_max_count}"
                -P "${TOPO_LANGUAGE_DIR}/spec/count_diagnostics.cmake")
        set_tests_properties("lang.${FEATURE}.recovery.${FNAME}"
            PROPERTIES LABELS "language;${FEATURE}" TIMEOUT 30)
    endforeach()
endforeach()
