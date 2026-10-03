# =============================================================================
# ProjectOptions.cmake — compile settings and helpers shared by first-party targets.
# =============================================================================
include_guard(GLOBAL)

# Version/format identifiers compiled into every target so that each artefact
# (IR, manifest, ledger record, API response) states which tool produced it.
set(TWIN_VERSION_DEFINITIONS
    TWIN_COMPILER_VERSION="${TWIN_COMPILER_VERSION}"
    TWIN_KERNEL_VERSION="${TWIN_KERNEL_VERSION}"
    TWIN_KERNEL_COMPAT="${TWIN_KERNEL_COMPAT}"
    TWIN_IR_FORMAT="${TWIN_IR_FORMAT}"
    TWIN_PACKAGE_FORMAT="${TWIN_PACKAGE_FORMAT}"
    TWIN_LEDGER_SCHEMA="${TWIN_LEDGER_SCHEMA}")

## twin_set_warnings(<target>)
## Strict warnings for first-party code; third-party code is compiled with its own flags.
function(twin_set_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wnull-dereference
            -Wimplicit-fallthrough -Woverloaded-virtual)
        if(TWIN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

## twin_add_library(<name> SOURCES <src...> [PUBLIC_DEPS <t...>] [PRIVATE_DEPS <t...>])
## Declares the static library `twin_<name>` with alias `twin::<name>`. Public
## headers live in include/twin/<name>/ and are exposed through include/.
function(twin_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})
    add_library(twin_${name} STATIC ${ARG_SOURCES})
    add_library(twin::${name} ALIAS twin_${name})
    target_include_directories(twin_${name} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
    target_compile_features(twin_${name} PUBLIC cxx_std_20)
    target_compile_definitions(twin_${name} PRIVATE ${TWIN_VERSION_DEFINITIONS})
    if(ARG_PUBLIC_DEPS)
        target_link_libraries(twin_${name} PUBLIC ${ARG_PUBLIC_DEPS})
    endif()
    if(ARG_PRIVATE_DEPS)
        target_link_libraries(twin_${name} PRIVATE ${ARG_PRIVATE_DEPS})
    endif()
    twin_set_warnings(twin_${name})
endfunction()

## twin_add_executable(<name> SOURCES <src...> DEPS <t...>)
function(twin_add_executable name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_compile_definitions(${name} PRIVATE ${TWIN_VERSION_DEFINITIONS})
    target_link_libraries(${name} PRIVATE ${ARG_DEPS})
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    twin_set_warnings(${name})
endfunction()
