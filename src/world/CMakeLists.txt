# twin::world — the Physical Twin side: ground truth, drone simulator, building-information
# service. NEVER linked into twin-runtime (checked by tests/architecture).
twin_add_library(world
    SOURCES scenario.cpp drone.cpp world.cpp service.cpp
    PUBLIC_DEPS twin::geo)
