# Studio engine services (route table): label "studio", built with -DTWIN_BUILD_STUDIO=ON.
twin_add_test(studio_engine_tests LABEL studio
    SOURCES engine_routes_test.cpp engine_contract_test.cpp
    DEPS twin::studio_engine twin::authoring twin::alignment httplib::httplib)
