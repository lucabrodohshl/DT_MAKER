# twin::kernel — the TRUSTED semantic kernel K_sem.
#
# Dependency rule (checked by tests/architecture): this target may link ONLY
# twin::ir_model and twin::core. No JSON, no networking, no files, no logging.
twin_add_library(kernel
    SOURCES
        model.cpp
        configuration.cpp
        semantics.cpp
        state_set.cpp
        explore.cpp
        instance.cpp
    PUBLIC_DEPS twin::ir_model twin::core)
