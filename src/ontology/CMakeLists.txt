# twin::ontology — formal ontology services for Verified Twin Studio:
# strict .ont/.interp parsing, validation, structural diff, refinement (Def. 4),
# alignment preservation (Theorem 3) and 3-valued interpretation evaluation.
# The authoritative theory is the aligner's reading (aligner::semalign + Z3);
# Z3 types stay private to this module (src/ontology/theory.hpp).
twin_add_library(ontology
    SOURCES
        source_parser.cpp
        theory.cpp
        validation.cpp
        evaluate.cpp
        diff.cpp
        refinement.cpp
    PUBLIC_DEPS twin::core
    PRIVATE_DEPS aligner::semalign)
target_include_directories(twin_ontology PRIVATE
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated/include>)
target_include_directories(twin_ontology SYSTEM PRIVATE "${TWIN_DEPS_PREFIX}/include")
