twin_add_executable(twin
    SOURCES
        main.cpp
        compile_command.cpp
        align_command.cpp
        package_command.cpp
        ledger_command.cpp
    DEPS twin::compiler twin::alignment twin::package_builder twin::ledger)
