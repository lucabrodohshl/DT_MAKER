# twin::json — canonical JSON (hashed artefacts) and typed, error-returning access.
twin_add_library(json
    SOURCES canonical.cpp
    PUBLIC_DEPS twin::core nlohmann_json::nlohmann_json)
