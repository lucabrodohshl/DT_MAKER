# End-to-end: the indoor-drone mission through the real runtime (session, kernel,
# ledger, planner, controller, co-simulation driver) against the real world
# service, connected in-process through the same JSON API the HTTP transport uses.
twin_add_test(e2e_tests LABEL e2e
    SOURCES drone_mission_test.cpp
    DEPS twin::runtime_shell twin::world twin::scene twin::package_builder)
