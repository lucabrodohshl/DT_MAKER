# twin::ingest — built-in plant connectivity of deployed twins (REST and MQTT inputs, mapping to
# the data contract, logical time). Talks to the twin only through the runtime's HTTP API: it
# never links the kernel, the ledger or the runtime (checked by tests/architecture).
twin_add_library(ingest
    SOURCES mqtt.cpp net.cpp mqtt_endpoint.cpp mqtt_subscriber.cpp mapping.cpp clock.cpp
            runtime_link.cpp service.cpp config.cpp
    PUBLIC_DEPS twin::json twin::core
    PRIVATE_DEPS httplib::httplib)
