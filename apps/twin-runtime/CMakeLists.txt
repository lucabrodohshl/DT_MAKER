twin_add_executable(twin-runtime
    SOURCES main.cpp
    DEPS twin::runtime_shell httplib::httplib)
