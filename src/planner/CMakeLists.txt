# twin::planner — UNTRUSTED path planner: proposes routes, never decides behaviour.
twin_add_library(planner
    SOURCES astar.cpp
    PUBLIC_DEPS twin::geo)
