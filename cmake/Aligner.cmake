include_guard(GLOBAL)
# =============================================================================
# Aligner.cmake — builds the EXISTING semantic aligner (SemPTDTAlignmentICSE)
# from its unmodified sources as libraries of this project.
#
# Rationale: the upstream aligner build is hard-wired to x86_64-linux paths
# (UDBM/build-x86_64-linux-release/...). Instead of editing the aligner, this
# file compiles the very same translation units against UDBM/UTAP built for the
# host by scripts/bootstrap-deps.sh. No file inside SemPTDTAlignmentICSE/ is
# modified. See docs/existing-aligner-integration.md.
#
# Targets:
#   aligner::dtpta     timed automata, zone graphs, (D)TPTA checker  (UTAP+UDBM)
#   aligner::semalign  ontology, interpretations, Algorithm 1        (+ Z3)
# =============================================================================

set(ALIGNER_DIR "${PROJECT_SOURCE_DIR}/SemPTDTAlignmentICSE"
    CACHE PATH "Location of the SemPTDTAlignmentICSE sources")
set(TWIN_DEPS_PREFIX "${PROJECT_SOURCE_DIR}/third_party/install"
    CACHE PATH "Install prefix of UDBM/UTAP/UUtils built by scripts/bootstrap-deps.sh")

if(NOT EXISTS "${ALIGNER_DIR}/include/dtpta/timedautomaton.h")
    message(FATAL_ERROR "SemPTDTAlignmentICSE not found at ${ALIGNER_DIR}")
endif()

# --- UPPAAL libraries (static) ---------------------------------------------------
foreach(lib UDBM UTAP base hash udebug xml2)
    find_library(TWIN_LIB_${lib} NAMES ${lib} lib${lib}
                 PATHS "${TWIN_DEPS_PREFIX}/lib" NO_DEFAULT_PATH)
    if(NOT TWIN_LIB_${lib})
        message(FATAL_ERROR
            "Third-party library '${lib}' not found in ${TWIN_DEPS_PREFIX}/lib.\n"
            "Run ./scripts/bootstrap-deps.sh first.")
    endif()
endforeach()

add_library(twin_uppaal_libs INTERFACE)
target_include_directories(twin_uppaal_libs SYSTEM INTERFACE "${TWIN_DEPS_PREFIX}/include")
# Link order matters for static archives: UTAP -> libxml2 ; UDBM -> UUtils.
target_link_libraries(twin_uppaal_libs INTERFACE
    ${TWIN_LIB_UTAP} ${TWIN_LIB_xml2}
    ${TWIN_LIB_UDBM} ${TWIN_LIB_base} ${TWIN_LIB_hash} ${TWIN_LIB_udebug})
if(APPLE)
    # libxml2 (static) needs these system libraries on macOS.
    target_link_libraries(twin_uppaal_libs INTERFACE iconv z m)
endif()

# --- Z3 ---------------------------------------------------------------------------
find_path(Z3_INCLUDE_DIR NAMES z3++.h PATHS /opt/homebrew/include /usr/local/include /usr/include)
find_library(Z3_LIBRARY NAMES z3 PATHS /opt/homebrew/lib /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu)
if(NOT Z3_INCLUDE_DIR OR NOT Z3_LIBRARY)
    message(FATAL_ERROR "Z3 (headers + library) is required for the semantic aligner. "
                        "macOS: brew install z3 ; Debian/Ubuntu: apt install libz3-dev")
endif()

# --- aligner::dtpta (same translation units as upstream's `dtpta` target) ----------
file(GLOB_RECURSE ALIGNER_CORE_SOURCES CONFIGURE_DEPENDS "${ALIGNER_DIR}/src/*.cpp")
list(FILTER ALIGNER_CORE_SOURCES EXCLUDE REGEX ".*/semalign/.*")
add_library(aligner_dtpta STATIC ${ALIGNER_CORE_SOURCES})
add_library(aligner::dtpta ALIAS aligner_dtpta)
target_include_directories(aligner_dtpta SYSTEM PUBLIC "${ALIGNER_DIR}/include")
target_link_libraries(aligner_dtpta PUBLIC twin_uppaal_libs)
# DEV_MODE=0 silences the aligner's debug prints (same as upstream `make release`).
target_compile_definitions(aligner_dtpta PUBLIC DEV_MODE=0)
# The aligner is C++17 code; UDBM headers require C++20 features, so compile
# it as C++20 (source compatible). Warnings in third-party code are not ours.
set_target_properties(aligner_dtpta PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(aligner_dtpta PRIVATE -w)

# --- aligner::semalign (Algorithm 1) -----------------------------------------------
file(GLOB ALIGNER_SEMALIGN_SOURCES CONFIGURE_DEPENDS "${ALIGNER_DIR}/src/semalign/*.cpp")
add_library(aligner_semalign STATIC ${ALIGNER_SEMALIGN_SOURCES})
add_library(aligner::semalign ALIAS aligner_semalign)
target_include_directories(aligner_semalign SYSTEM PUBLIC "${ALIGNER_DIR}/include" "${Z3_INCLUDE_DIR}")
target_link_libraries(aligner_semalign PUBLIC aligner_dtpta ${Z3_LIBRARY} nlohmann_json::nlohmann_json)
set_target_properties(aligner_semalign PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(aligner_semalign PRIVATE -w)

# --- The aligner's own reproduction drivers (used to validate the integration) ------
option(TWIN_BUILD_ALIGNER_BENCHMARKS "Build the aligner's original benchmark drivers" ON)
if(TWIN_BUILD_ALIGNER_BENCHMARKS)
    foreach(bm run_use_case run_CS1 run_CS2 run_CS3 run_CS4 run_CS5 run_CS6 run_CS7 run_CS8 run_all_semantic)
        add_executable(aligner_${bm} "${ALIGNER_DIR}/benchmark/${bm}.cpp")
        target_link_libraries(aligner_${bm} PRIVATE aligner_semalign)
        set_target_properties(aligner_${bm} PROPERTIES
            CXX_STANDARD 20 OUTPUT_NAME ${bm}
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/aligner-bin")
        target_compile_options(aligner_${bm} PRIVATE -w)
    endforeach()
endif()
