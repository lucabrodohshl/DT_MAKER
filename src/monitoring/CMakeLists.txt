# twin::monitoring — the property language and monitor documents (twin-monitors/1).
# Light on purpose (core, json, ir, kernel): the production runtime evaluates
# behavioural properties on committed states with it.
twin_add_library(monitoring
    SOURCES
        property.cpp
        monitors.cpp
    PUBLIC_DEPS twin::kernel twin::json)
