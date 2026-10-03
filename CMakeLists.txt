# =============================================================================
# Verified Twin Execution Kernel — top-level build
#
#   V_D --(twin compile)--> IR(V_D) --(twin::kernel)--> K_sem  ⊂  K_prod (twin-runtime)
#
# Repository layout
#   include/twin/<module>/   public headers (namespaced: #include "twin/kernel/kernel.hpp")
#   src/<module>/            library implementations, one CMake target per module
#   apps/                    executables: twin (CLI), twin-runtime (DT), twin-world (PT + env API)
#   tests/                   unit, integration and architecture tests (GoogleTest)
#   SemPTDTAlignmentICSE/    the existing semantic aligner (built from unmodified sources)
#
# Module dependency rules (enforced by target_link_libraries and tests/architecture):
#   twin::kernel   depends only on twin::ir_model and twin::core_types. No I/O, no JSON,
#                  no networking, no persistence. It is the single semantic authority.
#   twin::world    (ground truth) is never linked into twin-runtime.
#   twin::planner  is untrusted; it never mutates semantic state.
# =============================================================================
cmake_minimum_required(VERSION 3.24)
project(VerifiedTwin
        VERSION 1.0.0
        DESCRIPTION "Verified Twin Execution Kernel for semantically aligned Digital Twins"
        LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake --preset release")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TWIN_BUILD_TESTS "Build the automated test suites" ON)
option(TWIN_WARNINGS_AS_ERRORS "Treat warnings in first-party code as errors" ON)

# Versions stamped into IR, manifests, ledgers and API responses (single source of truth).
set(TWIN_COMPILER_VERSION "1.0.0")
set(TWIN_KERNEL_VERSION   "1.0.0")
set(TWIN_KERNEL_COMPAT    "twin-kernel/1")
set(TWIN_IR_FORMAT        "twin-ir/1")
set(TWIN_PACKAGE_FORMAT   "twin-package/1")
set(TWIN_LEDGER_SCHEMA    "twin-ledger/1")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(ProjectOptions)
include(Dependencies)
include(Aligner)

add_subdirectory(src)
add_subdirectory(apps)

if(TWIN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# API documentation: cmake --build --preset release --target docs
find_package(Doxygen QUIET COMPONENTS dot)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} "${PROJECT_SOURCE_DIR}/docs/Doxyfile"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Generating Doxygen documentation into build/docs/html"
        VERBATIM)
endif()
