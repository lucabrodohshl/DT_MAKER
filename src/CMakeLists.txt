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
add_subdirectory(compiler)
add_subdirectory(alignment)
if(TWIN_BUILD_STUDIO)  # Verified Twin Studio (docs/studio/); see cmake/Dependencies.cmake
    add_subdirectory(ontology)
    add_subdirectory(platform)
    add_subdirectory(studio)
endif()
add_subdirectory(package)
add_subdirectory(ledger)
add_subdirectory(runtime)
add_subdirectory(geo)
add_subdirectory(planner)
add_subdirectory(world)
