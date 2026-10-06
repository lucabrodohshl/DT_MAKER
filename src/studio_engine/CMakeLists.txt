# twin::studio_engine — the engine operations behind Studio Mode (authoring, and later
# alignment explanations, properties, sandbox previews, binding tests) as a route table
# that twin-studio's HTTP server registers. Contract: api/studio-engine.openapi.yaml.
twin_add_library(studio_engine
    SOURCES
        engine.cpp
        http.cpp
    PUBLIC_DEPS twin::json httplib::httplib
    PRIVATE_DEPS twin::authoring twin::alignment twin::monitoring)
