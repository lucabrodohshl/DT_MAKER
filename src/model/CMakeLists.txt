# twin::model — the twin modelling language: the canonical model (twin-model/1), its
# validation, and the compilers that derive V_D (twin-ta/1, UPPAAL XML), the domain theory
# K with I_D/I_P, the OWL 2 + SHACL schema and RDF knowledge graph and the Blueprint
# sections from it; plus lifting (timed automaton -> model). Model once, derive the rest.
twin_add_library(model
    SOURCES
        document.cpp
        expression.cpp
        resolve.cpp
        validate.cpp
        behavior.cpp
        theory.cpp
        rdf.cpp
        contract.cpp
        compile.cpp
        lift.cpp
    PUBLIC_DEPS twin::authoring twin::json twin::core)
