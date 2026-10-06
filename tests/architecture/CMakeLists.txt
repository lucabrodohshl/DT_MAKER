# Architecture rules checked on the sources (TCB boundary, single semantic authority).
twin_add_test(architecture_tests LABEL architecture
    SOURCES architecture_test.cpp
    DEPS twin::core)
