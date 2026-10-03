# twin::ir_model — IR value types and well-formedness rules. No JSON, no I/O:
# this is all the trusted kernel needs to know about the IR.
twin_add_library(ir_model
    SOURCES model.cpp validate.cpp
    PUBLIC_DEPS twin::core)

# twin::ir — canonical JSON codec of the IR (format "twin-ir/1").
twin_add_library(ir
    SOURCES codec.cpp
    PUBLIC_DEPS twin::ir_model twin::json)
