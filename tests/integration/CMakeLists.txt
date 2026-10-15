# Integration: components against real corpora (the SemPTDTAlignmentICSE models).
twin_add_test(integration_tests LABEL integration
    SOURCES compiler_corpus_test.cpp
    DEPS twin::compiler)

twin_add_test(authoring_integration_tests LABEL integration
    SOURCES authoring_corpus_test.cpp
    DEPS twin::authoring)

twin_add_test(model_integration_tests LABEL integration
    SOURCES model_toolchain_test.cpp
    DEPS twin::model twin::alignment twin::compiler)
