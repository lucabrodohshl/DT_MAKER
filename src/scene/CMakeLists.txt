# twin::scene — World & Layout documents (twin-world/1) and the mobile-robot simulation
# adapter that turns an authored world into the simulator's scenario. Outside the trusted
# core: the scene is context, never semantic state.
twin_add_library(scene
    SOURCES world.cpp robot_sim.cpp
    PUBLIC_DEPS twin::geo twin::json twin::core)
