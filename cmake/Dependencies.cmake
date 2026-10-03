include_guard(GLOBAL)
# =============================================================================
# Dependencies.cmake — pinned third-party dependencies fetched at configure time.
#
# Every dependency is pinned to an exact release tag so that builds are
# reproducible. None of these libraries is linked into the trusted semantic
# kernel (target twin_kernel); see docs/trusted-computing-base.md.
# =============================================================================
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# --- nlohmann/json (MIT) ------------------------------------------------------
# Same version as the one used by SemPTDTAlignmentICSE, so the aligner and the
# twin tooling share one JSON implementation.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# --- cpp-httplib (MIT) — production shell only (never the kernel) -------------
FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.7.tar.gz
    URL_HASH SHA256=b7b1e9e4e77565a5a9bc95e761d5df3e7c0e8ca37c90fd78b1b031bc6cb90fc1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE INTERNAL "")
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE INTERNAL "")
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE INTERNAL "")
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE INTERNAL "")
set(HTTPLIB_COMPILE OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(httplib)

# --- GoogleTest (BSD-3) — tests only -----------------------------------------
if(TWIN_BUILD_TESTS)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(INSTALL_GTEST OFF CACHE INTERNAL "")
    set(BUILD_GMOCK OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(googletest)
endif()
