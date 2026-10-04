# twin::geo — occupancy grids and map-update messages (outside the trusted core).
twin_add_library(geo
    SOURCES grid.cpp
    PUBLIC_DEPS twin::json twin::core)
