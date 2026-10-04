# twin::compiler — V_D (UPPAAL XML) -> canonical Twin IR.
# Reuses the aligner's parser stack (UTAP) and its TimedAutomaton for translation
# validation, so the compiled model is provably the model the aligner verified.
twin_add_library(compiler
    SOURCES
        compiler.cpp
        utap_reader.cpp
        crosscheck.cpp
        interpretation.cpp
    PUBLIC_DEPS twin::ir
    PRIVATE_DEPS aligner::dtpta)
# UTAP/UDBM headers trigger warnings under our strict flags; they are third-party.
target_include_directories(twin_compiler SYSTEM PRIVATE "${TWIN_DEPS_PREFIX}/include")
