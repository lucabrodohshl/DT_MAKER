# twin::ledger — tamper-evident execution ledger: records, writer, verifier, replay.
# Records what the kernel decided; never influences semantics.
twin_add_library(ledger
    SOURCES record.cpp writer.cpp verifier.cpp replay.cpp
    PUBLIC_DEPS twin::kernel twin::package twin::json)
