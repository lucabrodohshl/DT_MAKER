# =============================================================================
# Verified Twin Studio test suites (label "studio"; only built with
# -DTWIN_BUILD_STUDIO=ON, never part of the default ctest run).
#   ctest --preset studio-debug
# =============================================================================
twin_add_test(studio_ontology_tests LABEL studio
    SOURCES ontology/source_parser_test.cpp ontology/refinement_test.cpp ontology/validation_evaluate_diff_test.cpp
    DEPS twin::ontology)

twin_add_test(studio_platform_tests LABEL studio
    SOURCES platform/platform_test.cpp
    DEPS twin::platform)

twin_add_test(studio_service_tests LABEL studio
    SOURCES studio/studio_test.cpp
    DEPS twin::studio httplib::httplib)

twin_add_test(studio_blueprint_tests LABEL studio
    SOURCES studio/blueprint_test.cpp
    DEPS twin::studio)
# The deployment test launches the real runtime binaries (supervisor): build them first.
add_dependencies(studio_blueprint_tests twin-runtime twin-pt-feed twin-world)
