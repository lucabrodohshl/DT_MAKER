# =============================================================================
# Executables
#   twin          offline toolchain: compile, align, package, ledger, replay
#   twin-runtime  the production Digital Twin runtime (K_prod)
#   twin-world    the Physical Twin simulator + environment/map service
# =============================================================================
add_subdirectory(twin)
if(TWIN_BUILD_STUDIO)  # twin-studio: Verified Twin Studio API server
    add_subdirectory(twin-studio)
endif()
add_subdirectory(twin-world)
add_subdirectory(twin-runtime)
