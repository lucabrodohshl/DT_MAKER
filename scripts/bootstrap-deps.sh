#!/usr/bin/env bash
# =============================================================================
# bootstrap-deps.sh — fetch and build the third-party libraries required by the
# existing semantic aligner (SemPTDTAlignmentICSE) for the HOST platform.
#
# The aligner's own setup.sh targets x86_64-linux only. This script builds the
# same libraries (UDBM, UTAP and their dependencies) for macOS (arm64/x86_64)
# and Linux, pinned to exact revisions, into third_party/install/.
#
# Usage:   ./scripts/bootstrap-deps.sh            (idempotent; re-run is cheap)
# Result:  third_party/install/{include,lib}  with libUDBM.a libUTAP.a libxml2.a
#          libbase.a libhash.a libudebug.a (+ boost/xxhash headers)
#
# Pinned revisions (see also third_party/VERSIONS.txt):
#   UDBM    85ff047c0640577030d59d80cbe2c750b369cf26
#   UTAP    v2.1.1-rc (5b152db) — last revision exposing UTAP::Constants, which
#           the aligner sources use; later revisions renamed it.
#   UUtils  v2.0.7,  xxHash 0.8.3,  boost 1.88.0,  libxml2 v2.13.9
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="$ROOT/third_party"
SRC="$TP/src"
BLD="$TP/build"
PREFIX="$TP/install"
DL="$TP/downloads"
JOBS="${JOBS:-$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

UDBM_REV=85ff047c0640577030d59d80cbe2c750b369cf26
UTAP_REV=v2.1.1-rc
UUTILS_REV=v2.0.7
XXHASH_VERSION=0.8.3
XXHASH_SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80
BOOST_VERSION=1.88.0
BOOST_SHA256=f48b48390380cfb94a629872346e3a81370dc498896f16019ade727ab72eb1ec

log()  { printf '\033[1;34m[bootstrap]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[bootstrap] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

sha256_check() { # <file> <expected>
    local actual
    if command -v sha256sum >/dev/null 2>&1; then actual=$(sha256sum "$1" | cut -d' ' -f1)
    else actual=$(shasum -a 256 "$1" | cut -d' ' -f1); fi
    [ "$actual" = "$2" ] || die "checksum mismatch for $1 (expected $2, got $actual)"
}

# --- prerequisites --------------------------------------------------------------
for tool in cmake git curl flex; do command -v "$tool" >/dev/null || die "missing tool: $tool"; done
BISON=""
for cand in /opt/homebrew/opt/bison/bin/bison /usr/local/opt/bison/bin/bison "$(command -v bison || true)"; do
    if [ -n "$cand" ] && [ -x "$cand" ]; then
        ver=$("$cand" --version | head -1 | sed -E 's/.* ([0-9]+)\.([0-9]+).*/\1\2/')
        if [ "${ver:-0}" -ge 36 ]; then BISON="$cand"; break; fi
    fi
done
[ -n "$BISON" ] || die "GNU bison >= 3.6 is required (macOS: brew install bison)"
log "using bison: $BISON"

mkdir -p "$SRC" "$BLD" "$PREFIX" "$DL"

clone_at() { # <url> <dir> <rev>
    if [ ! -d "$2/.git" ]; then git clone --quiet "$1" "$2"; fi
    git -C "$2" fetch --quiet --tags origin || true
    git -C "$2" -c advice.detachedHead=false checkout --quiet "$3"
    log "$(basename "$2") at $(git -C "$2" rev-parse --short HEAD)"
}

# --- xxHash ---------------------------------------------------------------------
if [ ! -r "$PREFIX/include/xxhash.h" ]; then
    log "building xxHash $XXHASH_VERSION"
    f="$DL/xxHash-$XXHASH_VERSION.tgz"
    [ -r "$f" ] || curl -sSL "https://github.com/Cyan4973/xxHash/archive/refs/tags/v$XXHASH_VERSION.tar.gz" -o "$f"
    sha256_check "$f" "$XXHASH_SHA256"
    rm -rf "$BLD/xxHash-src" && mkdir -p "$BLD/xxHash-src" && tar -xzf "$f" -C "$BLD/xxHash-src" --strip-components=1
    cmake -S "$BLD/xxHash-src/cmake_unofficial" -B "$BLD/xxhash" -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
    cmake --build "$BLD/xxhash" -j "$JOBS" >/dev/null && cmake --install "$BLD/xxhash" >/dev/null
fi

# --- boost (headers + math), as required by UUtils --------------------------------
if [ ! -r "$PREFIX/include/boost/math/distributions/arcsine.hpp" ]; then
    log "building boost $BOOST_VERSION (headers, math) — this takes a few minutes"
    f="$DL/boost-$BOOST_VERSION-cmake.tar.xz"
    [ -r "$f" ] || curl -sSL "https://github.com/boostorg/boost/releases/download/boost-$BOOST_VERSION/boost-$BOOST_VERSION-cmake.tar.xz" -o "$f"
    sha256_check "$f" "$BOOST_SHA256"
    rm -rf "$BLD/boost-src" && mkdir -p "$BLD/boost-src" && tar -xJf "$f" -C "$BLD/boost-src" --strip-components=1
    cmake -S "$BLD/boost-src" -B "$BLD/boost" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
          -DBOOST_INCLUDE_LIBRARIES="headers;math" -DBOOST_ENABLE_MPI=OFF -DBOOST_ENABLE_PYTHON=OFF \
          -DBOOST_RUNTIME_LINK=static -DBUILD_TESTING=OFF -DBOOST_INSTALL_LAYOUT=system \
          -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
    cmake --build "$BLD/boost" -j "$JOBS" >/dev/null && cmake --install "$BLD/boost" >/dev/null
fi

# --- UUtils ---------------------------------------------------------------------
if [ ! -r "$PREFIX/lib/libbase.a" ]; then
    clone_at https://github.com/UPPAALModelChecker/UUtils.git "$SRC/UUtils" "$UUTILS_REV"
    log "building UUtils"
    cmake -S "$SRC/UUtils" -B "$BLD/uutils" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" \
          -DUUtils_WITH_TESTS=OFF -DUUtils_WITH_BENCHMARKS=OFF -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
    cmake --build "$BLD/uutils" -j "$JOBS" >/dev/null && cmake --install "$BLD/uutils" >/dev/null
fi

# --- UDBM -----------------------------------------------------------------------
if [ ! -r "$PREFIX/lib/libUDBM.a" ]; then
    clone_at https://github.com/UPPAALModelChecker/UDBM "$SRC/UDBM" "$UDBM_REV"
    log "building UDBM"
    cmake -S "$SRC/UDBM" -B "$BLD/udbm" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" \
          -DUDBM_WITH_TESTS=OFF -DUDBM_CLANG_TIDY=OFF -DFIND_FATAL=ON -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
    cmake --build "$BLD/udbm" -j "$JOBS" >/dev/null && cmake --install "$BLD/udbm" >/dev/null
fi

# --- UTAP (+ libxml2 2.13.9, static) --------------------------------------------
if [ ! -r "$PREFIX/lib/libUTAP.a" ] || [ ! -r "$PREFIX/lib/libxml2.a" ]; then
    clone_at https://github.com/UPPAALModelChecker/utap "$SRC/utap" "$UTAP_REV"
    log "building UTAP (and its pinned libxml2)"
    cmake -S "$SRC/utap" -B "$BLD/utap" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
          -DLIBXML2_SHARED_LIBS=OFF -DUTAP_TESTS=OFF -DUTAP_CLANG_TIDY=OFF -DUTAP_CCACHE=OFF \
          -DBISON_EXECUTABLE="$BISON" -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER \
          -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
    cmake --build "$BLD/utap" -j "$JOBS" >/dev/null && cmake --install "$BLD/utap" >/dev/null
    cp "$BLD/utap/_deps/libxml2-build/libxml2.a" "$PREFIX/lib/"
fi

log "done — third-party libraries installed in $PREFIX"
log "next: cmake --preset release && cmake --build --preset release"
