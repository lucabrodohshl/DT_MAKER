# twin::alignment — runs the unmodified semantic aligner (Z3) and records evidence.
twin_add_library(alignment
    SOURCES alignment.cpp
    PUBLIC_DEPS twin::json
    PRIVATE_DEPS twin::compiler aligner::semalign)
target_include_directories(twin_alignment PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated/include>)
target_include_directories(twin_alignment SYSTEM PRIVATE "${TWIN_DEPS_PREFIX}/include")
