# =============================================================================
# Test suites (GoogleTest). Tests are validation, not proof: the correctness
# argument is in proof/. Labels: unit, integration, architecture, e2e.
#   ctest --preset release                 # everything
#   ctest --preset release -L unit         # one category
# =============================================================================
include(GoogleTest)

add_library(twin_test_support INTERFACE)
target_include_directories(twin_test_support INTERFACE "${PROJECT_SOURCE_DIR}/tests")
target_compile_definitions(twin_test_support INTERFACE
    TWIN_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    TWIN_BINARY_DIR="${PROJECT_BINARY_DIR}")

## twin_add_test(<name> LABEL <label> SOURCES <...> DEPS <...>)
function(twin_add_test name)
    cmake_parse_arguments(ARG "" "LABEL" "SOURCES;DEPS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_DEPS} twin_test_support GTest::gtest_main)
    if(APPLE)  # static libraries legitimately appear on several dependency paths
        target_link_options(${name} PRIVATE "LINKER:-no_warn_duplicate_libraries")
    endif()
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    twin_set_warnings(${name})
    gtest_discover_tests(${name}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        PROPERTIES LABELS "${ARG_LABEL}"
        DISCOVERY_TIMEOUT 60)
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(architecture)
add_subdirectory(e2e)
if(TWIN_BUILD_STUDIO)  # Verified Twin Studio suites (tests/studio/) and engine services (tests/engine/)
    add_subdirectory(studio)
    add_subdirectory(engine)
endif()
