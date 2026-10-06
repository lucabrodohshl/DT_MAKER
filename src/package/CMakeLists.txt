# twin::package — package manifest + verification. Runtime-safe: links only
# core/json/ir and twin::monitoring (monitor documents) — no compiler, no UTAP, no Z3.
twin_add_library(package
    SOURCES package.cpp
    PUBLIC_DEPS twin::ir twin::json
    PRIVATE_DEPS twin::monitoring)

# twin::package_builder — offline release pipeline (compile + align + bundle).
twin_add_library(package_builder
    SOURCES builder.cpp
    PUBLIC_DEPS twin::package twin::compiler
    PRIVATE_DEPS twin::alignment twin::authoring twin::monitoring)
