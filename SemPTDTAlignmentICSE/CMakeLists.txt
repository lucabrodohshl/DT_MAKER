cmake_minimum_required(VERSION 3.16)
project(DTPTAlignment)

# Export compile commands for IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# SemAlign dependencies: Z3 (system) + nlohmann/json (FetchContent)
# ============================================================

# --- Z3 (installed as system library) ---
find_library(Z3_LIBRARY NAMES z3 libz3
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu
    DOC "Z3 SMT solver shared library"
)
find_path(Z3_INCLUDE_DIR NAMES z3++.h z3.h
    PATHS /usr/include
    DOC "Z3 C++ API header directory"
)

if(NOT Z3_LIBRARY OR NOT Z3_INCLUDE_DIR)
    message(WARNING "Z3 not found — semalign targets will be skipped. "
                    "Install libz3-dev (sudo apt-get install libz3-dev).")
    set(SEMALIGN_AVAILABLE OFF)
else()
    message(STATUS "Z3 found: ${Z3_LIBRARY}  (headers: ${Z3_INCLUDE_DIR})")
    set(SEMALIGN_AVAILABLE ON)
endif()

# --- nlohmann/json (header-only, fetched via FetchContent) ---
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
# Allow offline builds: only fetch if not already populated
FetchContent_GetProperties(nlohmann_json)
if(NOT nlohmann_json_POPULATED)
    # Try to find a system install first
    find_package(nlohmann_json 3.11 QUIET)
    if(NOT nlohmann_json_FOUND)
        message(STATUS "nlohmann/json not found as system package — fetching v3.11.3 via FetchContent")
        FetchContent_MakeAvailable(nlohmann_json)
    else()
        message(STATUS "nlohmann/json found as system package")
    endif()
endif()

# Set default installation prefix if not specified
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)
endif()



# Create subdirectories for examples and benchmarks
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks)

# Development mode option (ON by default)
option(DEV_MODE "Enable development mode with debug prints" ON)


# Set build type and compile flags
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
    message(STATUS "Debug build: ON (debug symbols enabled)")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    message(STATUS "Release build: ON (optimizations enabled)")
    # Find and enable OpenMP
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        message(STATUS "OpenMP found: enabling OpenMP support")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OpenMP_CXX_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${OpenMP_EXE_LINKER_FLAGS}")
    else()
        message(WARNING "OpenMP not found! Please install libomp-dev (sudo apt-get install libomp-dev) or your system's OpenMP package.")
    endif()

    # Find and enable TBB (concurrent containers for lock-free caches)
    find_package(TBB)
    if(TBB_FOUND)
        message(STATUS "TBB found: enabling concurrent hash map support")
    else()
        message(WARNING "TBB not found! Please install libtbb-dev (sudo apt-get install libtbb-dev).")
    endif()
endif()

if(DEV_MODE)
    message(STATUS "Development mode: ON (debug prints enabled)")
    add_definitions(-DDEV_MODE=1)
else()
    message(STATUS "Development mode: OFF (debug prints disabled)")
    add_definitions(-DDEV_MODE=0)
endif()

# Check if UDBM directory exists, if not provide instructions
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/UDBM")
    message(FATAL_ERROR "UDBM directory not found! Please run:\n"
                        "git clone https://github.com/UPPAALModelChecker/UDBM\n"
                        "cd UDBM\n"
                        "./getlibs.sh x86_64-linux\n"
                        "./compile.sh x86_64-linux-libs-release")
endif()

# Check if UDBM is built, if not provide instructions
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/UDBM/build-x86_64-linux-release/src/libUDBM.a")
    message(FATAL_ERROR "UDBM library not built! Please run:\n"
                        "cd UDBM\n"
                        "./getlibs.sh x86_64-linux\n"
                        "./compile.sh x86_64-linux-libs-release")
endif()

# Check if UTAP directory exists, if not provide instructions
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utap")
    message(FATAL_ERROR "UTAP directory not found! Please run:\n"
                        "git clone https://github.com/UPPAALModelChecker/utap\n"
                        "cd utap\n"
                        "./getlibs.sh x86_64-linux\n"
                        "./compile.sh x86_64-linux")
endif()

# Check if UTAP is built, if not provide instructions
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utap/build-x86_64-linux-release/src/libUTAP.a")
    message(FATAL_ERROR "UTAP library not built! Please run:\n"
                        "cd utap\n"
                        "./getlibs.sh x86_64-linux\n"
                        "./compile.sh x86_64-linux")
endif()

# Find the UDBM library
find_library(UDBM_LIBRARY 
    NAMES UDBM libUDBM
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/UDBM/build-x86_64-linux-release/src
    NO_DEFAULT_PATH
)

if(NOT UDBM_LIBRARY)
    message(FATAL_ERROR "UDBM library not found!")
endif()

# Find UUtils libraries (dependencies)
find_library(UUTILS_BASE_LIBRARY 
    NAMES base libbase
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/UDBM/local/x86_64-linux/lib
    NO_DEFAULT_PATH
)

find_library(UUTILS_HASH_LIBRARY 
    NAMES hash libhash
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/UDBM/local/x86_64-linux/lib
    NO_DEFAULT_PATH
)

find_library(UUTILS_DEBUG_LIBRARY 
    NAMES udebug libudebug
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/UDBM/local/x86_64-linux/lib
    NO_DEFAULT_PATH
)

# Find the UTAP library
find_library(UTAP_LIBRARY 
    NAMES UTAP libUTAP
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/utap/build-x86_64-linux-release/src
    NO_DEFAULT_PATH
)

if(NOT UTAP_LIBRARY)
    message(FATAL_ERROR "UTAP library not found!")
endif()

# Find UTAP dependencies
find_library(XML2_LIBRARY 
    NAMES xml2 libxml2
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/utap/local/x86_64-linux/lib
    NO_DEFAULT_PATH
)

find_library(Y_LIBRARY 
    NAMES y liby
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/utap/local/x86_64-linux/lib
    NO_DEFAULT_PATH
)
# Y_LIBRARY is optional: UTAP on some builds embeds the yacc runtime.
if(NOT Y_LIBRARY)
    set(Y_LIBRARY "")
    message(STATUS "liby not found — assuming it is embedded in libUTAP.a")
endif()

# Common include directories
set(COMMON_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/UDBM/include
    ${CMAKE_CURRENT_SOURCE_DIR}/UDBM/build-x86_64-linux-release/include
    ${CMAKE_CURRENT_SOURCE_DIR}/UDBM/local/x86_64-linux/include
    ${CMAKE_CURRENT_SOURCE_DIR}/utap/include
    ${CMAKE_CURRENT_SOURCE_DIR}/utap/build-x86_64-linux-release/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/utap/local/x86_64-linux/include
)

# Common libraries for UDBM examples
set(UDBM_LIBRARIES
    ${UDBM_LIBRARY}
    ${UUTILS_BASE_LIBRARY}
    ${UUTILS_HASH_LIBRARY} 
    ${UUTILS_DEBUG_LIBRARY}
)

# Common libraries for UTAP examples
set(UTAP_LIBRARIES
    ${UTAP_LIBRARY}
    ${XML2_LIBRARY}
    ${Y_LIBRARY}
)

# Collect all source files in src directory and header files in include directory
file(GLOB_RECURSE DTPTA_SOURCES "src/*.cpp")
file(GLOB_RECURSE DTPTA_HEADERS "include/*.h" "include/*.hpp")

# Exclude semalign sources from the core dtpta library (they live in src/semalign/)
list(FILTER DTPTA_SOURCES EXCLUDE REGEX ".*/semalign/.*")

# Create the dtpta library
add_library(dtpta STATIC ${DTPTA_SOURCES} ${DTPTA_HEADERS})

# Set include directories differently for build and install
target_include_directories(dtpta PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/UDBM/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/UDBM/build-x86_64-linux-release/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/UDBM/local/x86_64-linux/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/utap/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/utap/build-x86_64-linux-release/src/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/utap/local/x86_64-linux/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(dtpta PUBLIC ${UDBM_LIBRARIES} ${UTAP_LIBRARIES})

# Link TBB if available (concurrent containers for OpenMP caches)
if(TBB_FOUND)
    target_link_libraries(dtpta PUBLIC TBB::tbb)
    target_compile_definitions(dtpta PUBLIC DTPTA_HAS_TBB=1)
endif()

# ============================================================
# semalign_lib — static library for semantic alignment
# ============================================================
if(SEMALIGN_AVAILABLE)
    file(GLOB SEMALIGN_SOURCES "src/semalign/*.cpp")

    add_library(semalign_lib STATIC ${SEMALIGN_SOURCES})

    target_include_directories(semalign_lib PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${Z3_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    )

    target_link_libraries(semalign_lib PUBLIC dtpta ${Z3_LIBRARY})

    if(nlohmann_json_FOUND)
        target_link_libraries(semalign_lib PUBLIC nlohmann_json::nlohmann_json)
    else()
        target_include_directories(semalign_lib PUBLIC
            $<BUILD_INTERFACE:${nlohmann_json_SOURCE_DIR}/include>
        )
    endif()

    message(STATUS "semalign_lib configured (Z3 + nlohmann/json)")
endif()

# Function to create a UDBM example executable
function(add_udbm_example EXAMPLE_NAME)
    add_executable(${EXAMPLE_NAME} example/${EXAMPLE_NAME}.cpp)
    target_include_directories(${EXAMPLE_NAME} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_link_libraries(${EXAMPLE_NAME} dtpta)
    set_target_properties(${EXAMPLE_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
    )
endfunction()

# Function to create a UTAP example executable
function(add_utap_example EXAMPLE_NAME)
    add_executable(${EXAMPLE_NAME} example/${EXAMPLE_NAME}.cpp)
    target_include_directories(${EXAMPLE_NAME} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_link_libraries(${EXAMPLE_NAME} dtpta)
    set_target_properties(${EXAMPLE_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
    )
endfunction()

# Function to create an integrated example executable (needs both UTAP and UDBM)
function(add_integration_example EXAMPLE_NAME)
    add_executable(${EXAMPLE_NAME} example/${EXAMPLE_NAME}.cpp)
    target_include_directories(${EXAMPLE_NAME} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_link_libraries(${EXAMPLE_NAME} dtpta)
    set_target_properties(${EXAMPLE_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
    )
endfunction()

# Function to add benchmark 
function(add_benchmark BENCHMARK_NAME)
    add_executable(${BENCHMARK_NAME} benchmark/${BENCHMARK_NAME}.cpp)
    target_include_directories(${BENCHMARK_NAME} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_link_libraries(${BENCHMARK_NAME} dtpta)
    set_target_properties(${BENCHMARK_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks
    )
endfunction()


# Library demos are not part of the artifact's reproduction targets.
# Copy assets directory to build directory
# Must be able to specify which subfolder according to the target

#add_custom_target(copy_assets_examples ALL
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#    ${CMAKE_CURRENT_SOURCE_DIR}/assets
#    ${CMAKE_CURRENT_BINARY_DIR}/examples/assets
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#    ${CMAKE_CURRENT_SOURCE_DIR}/assets
#    ${CMAKE_CURRENT_BINARY_DIR}/assets
#    COMMENT "Copying assets directory to examples directory and build root"
#)
#
#add_custom_target(copy_assets_benchmarks ALL
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#    ${CMAKE_CURRENT_SOURCE_DIR}/assets
#    ${CMAKE_CURRENT_BINARY_DIR}/benchmarks/assets
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#    ${CMAKE_CURRENT_SOURCE_DIR}/assets
#    ${CMAKE_CURRENT_BINARY_DIR}/assets
#    COMMENT "Copying assets directory to benchmarks directory and build root"
#)




# ============================================================
# Asset copying (parallel-build safe)
# ============================================================

# Root copy (executed once)
add_custom_target(copy_assets_root ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    ${CMAKE_CURRENT_BINARY_DIR}/assets
    COMMENT "Copying assets to build root"
)

# Copy assets for examples
add_custom_target(copy_assets_examples
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    ${CMAKE_CURRENT_BINARY_DIR}/examples/assets
    COMMENT "Copying assets to examples directory"
)

# Copy assets for benchmarks
add_custom_target(copy_assets_benchmarks
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    ${CMAKE_CURRENT_BINARY_DIR}/benchmarks/assets
    COMMENT "Copying assets to benchmarks directory"
)

# Ensure root copy happens first
add_dependencies(copy_assets_examples copy_assets_root)
add_dependencies(copy_assets_benchmarks copy_assets_root)
# ============================================================
# Semantic-alignment benchmark executables (require Z3)
#   RQ3 & RQ4 : run_CS1 .. run_CS8  (paper Table III / Figure 4)
#   RQ1 & RQ2 : run_use_case        (drone running-example formal validation)
#   utility   : run_all_semantic    (generic PT/DT/ontology batch runner)
# ============================================================
if(SEMALIGN_AVAILABLE)
    function(add_semalign_benchmark BENCHMARK_NAME)
        add_executable(${BENCHMARK_NAME} benchmark/${BENCHMARK_NAME}.cpp)
        target_include_directories(${BENCHMARK_NAME} PRIVATE ${COMMON_INCLUDE_DIRS} ${Z3_INCLUDE_DIR})
        target_link_libraries(${BENCHMARK_NAME} semalign_lib)
        set_target_properties(${BENCHMARK_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks
        )
    endfunction()

    set(SEMALIGN_BENCHMARKS
        run_all_semantic
        run_CS1 run_CS2 run_CS3 run_CS4 run_CS5 run_CS6 run_CS7 run_CS8
        run_use_case
    )
    foreach(BM ${SEMALIGN_BENCHMARKS})
        add_semalign_benchmark(${BM})
        add_dependencies(${BM} copy_assets_benchmarks)
    endforeach()

    message(STATUS "Semantic-alignment benchmarks: ${SEMALIGN_BENCHMARKS}")
endif()


message(STATUS "UDBM Library: ${UDBM_LIBRARY}")
message(STATUS "UUtils Base: ${UUTILS_BASE_LIBRARY}")
message(STATUS "UUtils Hash: ${UUTILS_HASH_LIBRARY}")
message(STATUS "UUtils Debug: ${UUTILS_DEBUG_LIBRARY}")
message(STATUS "UTAP Library: ${UTAP_LIBRARY}")
message(STATUS "XML2 Library: ${XML2_LIBRARY}")
message(STATUS "Y Library: ${Y_LIBRARY}")

# Convenience target to build every reproduction benchmark
if(SEMALIGN_AVAILABLE)
    add_custom_target(benchmarks DEPENDS ${SEMALIGN_BENCHMARKS})
endif()


# Install configuration
# Install the static library
install(TARGETS dtpta
    EXPORT DTPTAlignmentTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Export targets
install(EXPORT DTPTAlignmentTargets
    FILE DTPTAlignmentTargets.cmake
    NAMESPACE DTPTAlignment::
    DESTINATION lib/cmake/DTPTAlignment
)

# Install all header files preserving directory structure
install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install examples (optional - uncomment if you want to install examples)
# install(DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/examples/
#     DESTINATION bin/examples
#     FILES_MATCHING PATTERN "*"
#     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
# )

# Install benchmarks (optional - uncomment if you want to install benchmarks)  
# install(DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/benchmarks/
#     DESTINATION bin/benchmarks
#     FILES_MATCHING PATTERN "*"
#     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
# )

# Create a find package config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "DTPTAlignmentConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DTPTAlignmentConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/DTPTAlignmentConfig.cmake"
    INSTALL_DESTINATION lib/cmake/DTPTAlignment
)

# Install the config files (create the cmake directory first)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/DTPTAlignmentConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/DTPTAlignmentConfigVersion.cmake"
    DESTINATION lib/cmake/DTPTAlignment
)

message(STATUS "Installation directory: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "To install, run: make install")
message(STATUS "To change install directory, use: cmake -DCMAKE_INSTALL_PREFIX=/your/path ..")
