# =============================================================================
# Libraries — one static library per module (target twin_<module>, alias twin::<module>).
# Headers: include/twin/<module>/. The dependency graph is intentionally a DAG:
#
#   core <- json <- ir <- compiler(+aligner::dtpta) <- alignment(+aligner::semalign)
#   core <- kernel(ir_model only)
#   core, json, ir, kernel <- package <- ledger <- runtime
#   core <- geo <- planner, world
# =============================================================================

# Generated header with version/format identifiers (single source of truth).
configure_file("${PROJECT_SOURCE_DIR}/cmake/version.hpp.in"
               "${PROJECT_BINARY_DIR}/generated/include/twin/core/version.hpp" @ONLY)

add_subdirectory(core)
add_subdirectory(json)
add_subdirectory(ir)
add_subdirectory(kernel)
