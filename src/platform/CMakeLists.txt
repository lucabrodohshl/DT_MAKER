# twin::platform — Verified Twin Studio persistence and lifecycle: SQLite records,
# content-addressed object store, artefact versions, evidence, applicability,
# engineering audit, assets/graph, telemetry history, twins/packages/deployments,
# Twin Blueprints (versions, bundles),
# application log. No formal reasoning happens here (see twin::ontology).
twin_add_library(platform
    SOURCES
        database.cpp
        clock.cpp
        object_store.cpp
        artifacts.cpp
        evidence.cpp
        applicability.cpp
        audit.cpp
        assets.cpp
        telemetry.cpp
        twins.cpp
        blueprints.cpp
        app_log.cpp
    PUBLIC_DEPS twin::core twin::json
    PRIVATE_DEPS SQLite::SQLite3)
