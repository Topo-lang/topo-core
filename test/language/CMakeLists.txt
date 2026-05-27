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
