twin_add_test(core_unit_tests LABEL unit
    SOURCES core/sha256_test.cpp core/logical_time_test.cpp core/result_test.cpp
    DEPS twin::core)

twin_add_test(ir_unit_tests LABEL unit
    SOURCES ir/codec_test.cpp ir/validate_test.cpp
    DEPS twin::ir)

twin_add_test(kernel_unit_tests LABEL unit
    SOURCES kernel/semantics_test.cpp kernel/state_set_test.cpp kernel/explore_test.cpp
            kernel/instance_test.cpp kernel/property_test.cpp
    DEPS twin::kernel)

twin_add_test(compiler_unit_tests LABEL unit
    SOURCES compiler/compiler_test.cpp
    DEPS twin::compiler twin::package_builder)

twin_add_test(package_unit_tests LABEL unit
    SOURCES package/package_test.cpp package/package_additions_test.cpp
    DEPS twin::package_builder twin::authoring)

twin_add_test(ledger_unit_tests LABEL unit
    SOURCES ledger/ledger_test.cpp
    DEPS twin::ledger twin::runtime twin::package_builder)

twin_add_test(runtime_unit_tests LABEL unit
    SOURCES runtime/session_test.cpp runtime/monitor_test.cpp runtime/what_if_test.cpp
    DEPS twin::runtime_shell twin::package_builder)

twin_add_test(planner_unit_tests LABEL unit
    SOURCES planner/planner_test.cpp
    DEPS twin::planner twin::runtime_shell)

twin_add_test(world_unit_tests LABEL unit
    SOURCES world/world_test.cpp
    DEPS twin::world)

twin_add_test(ptfeed_unit_tests LABEL unit
    SOURCES ptfeed/feed_test.cpp
    DEPS twin::ptfeed)

twin_add_test(authoring_unit_tests LABEL unit
    SOURCES authoring/model_test.cpp authoring/validate_test.cpp authoring/text_test.cpp authoring/render_test.cpp authoring/import_test.cpp authoring/properties_test.cpp
    DEPS twin::authoring)

twin_add_test(alignment_unit_tests LABEL unit
    SOURCES alignment/diagnostics_test.cpp
    DEPS twin::alignment)

twin_add_test(monitoring_unit_tests LABEL unit
    SOURCES monitoring/property_test.cpp monitoring/monitors_test.cpp
    DEPS twin::monitoring twin::compiler)
