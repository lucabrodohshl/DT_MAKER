# twin::studio — Verified Twin Studio application services (use cases behind the
# HTTP API), the live event hub and the HTTP server. Formal conclusions are
# delegated to twin::ontology, twin::alignment, twin::compiler and twin::package;
# behavioural state is proxied from twin-runtime, never computed here.
twin_add_library(studio
    SOURCES
        events.cpp
        ontology_json.cpp
        services_core.cpp
        services_artifacts.cpp
        services_checks.cpp
        services_release.cpp
        services_ops.cpp
        seed.cpp
        server.cpp
    PUBLIC_DEPS twin::platform twin::ontology
    PRIVATE_DEPS twin::compiler twin::alignment twin::package_builder httplib::httplib)
target_include_directories(twin_studio PRIVATE
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated/include>)
