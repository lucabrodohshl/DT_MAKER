twin_add_test(core_unit_tests LABEL unit
    SOURCES core/sha256_test.cpp core/logical_time_test.cpp core/result_test.cpp
    DEPS twin::core)

twin_add_test(ir_unit_tests LABEL unit
    SOURCES ir/codec_test.cpp ir/validate_test.cpp
    DEPS twin::ir)

twin_add_test(kernel_unit_tests LABEL unit
    SOURCES kernel/semantics_test.cpp kernel/state_set_test.cpp kernel/explore_test.cpp
            kernel/instance_test.cpp
    DEPS twin::kernel)
