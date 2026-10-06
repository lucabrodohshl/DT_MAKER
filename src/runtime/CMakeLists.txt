# twin::runtime — the session: the single mutation path of semantic state (TCB part
# of the production runtime). Links only kernel/ledger/package.
twin_add_library(runtime
    SOURCES session.cpp
    PUBLIC_DEPS twin::kernel twin::ledger twin::package)

# twin::runtime_shell — everything else in K_prod: adapters, world model, mission
# controller, co-simulation driver, API. Untrusted for semantics; must never link
# twin::world (ground truth) — checked by tests/architecture.
twin_add_library(runtime_shell
    SOURCES
        views.cpp
    what_if.cpp
        executions.cpp
        host.cpp
        monitor_host.cpp
        world_model.cpp
        pt_adapter.cpp
        plan_validator.cpp
        mission_controller.cpp
        event_hub.cpp
        cosim_driver.cpp
        world_port.cpp
        http_world_port.cpp
        api_server.cpp
    PUBLIC_DEPS twin::runtime twin::planner twin::geo
    PRIVATE_DEPS httplib::httplib)
