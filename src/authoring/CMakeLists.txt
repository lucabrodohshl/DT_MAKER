# twin::authoring — the canonical timed-automaton model behind every authoring path
# (UPPAAL import, diagram editor, TwinTA text), its validation, renderings and
# import/export. The compiler and the aligner only ever read its toolchain rendering.
twin_add_library(authoring
    SOURCES
        model.cpp
        diagnostics.cpp
        validate.cpp
        layout.cpp
        text_lexer.cpp
        text_parser.cpp
        text_printer.cpp
        render_xml.cpp
        toolchain.cpp
        xml_scan.cpp
        import_uppaal.cpp
        importers.cpp
        diff.cpp
        zone_graph.cpp
        properties.cpp
    PUBLIC_DEPS twin::ir twin::json twin::compiler twin::monitoring
    PRIVATE_DEPS twin::alignment aligner::dtpta)
# UDBM headers (zone graphs for property checks) are third-party.
target_include_directories(twin_authoring SYSTEM PRIVATE "${TWIN_DEPS_PREFIX}/include")
