# twin::core — dependency-free foundations: errors, Result<T>, exact logical time,
# SHA-256. Part of the trusted computing base (the kernel depends on it).
twin_add_library(core
    SOURCES
        error.cpp
        logical_time.cpp
        sha256.cpp
        wall_clock.cpp)
target_include_directories(twin_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated/include>)
