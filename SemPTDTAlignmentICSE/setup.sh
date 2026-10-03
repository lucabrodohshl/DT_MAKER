#!/bin/bash
#
# setup.sh — install build dependencies and the two third-party UPPAAL
# libraries (UDBM, UTAP) that the core library links against.
#
# Usage:
#   ./setup.sh                 # install apt packages + build UDBM and UTAP
#   ./setup.sh --no-apt        # skip apt-get (use when packages already present)
#
# All paths are relative to this script; nothing is written outside the
# repository except the apt packages.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UDBM_DIR="$SCRIPT_DIR/UDBM"
UTAP_DIR="$SCRIPT_DIR/utap"

NO_APT=0
for arg in "$@"; do
    [ "$arg" = "--no-apt" ] && NO_APT=1
done

# ---------------------------------------------------------------------------
# 1. System packages
# ---------------------------------------------------------------------------
if [ "$NO_APT" -eq 0 ]; then
    echo "=== Installing build dependencies (apt) ==="
    SUDO=""
    [ "$(id -u)" -ne 0 ] && SUDO="sudo"
    $SUDO apt-get update
    $SUDO apt-get install -y \
        build-essential cmake git \
        libz3-dev libomp-dev libtbb-dev
else
    echo "=== Skipping apt-get (--no-apt) ==="
fi

# ---------------------------------------------------------------------------
# 2. UDBM (UPPAAL DBM library)  — https://github.com/UPPAALModelChecker/UDBM
# ---------------------------------------------------------------------------
echo "=== Setting up UDBM ==="
if [ ! -d "$UDBM_DIR" ]; then
    git clone https://github.com/UPPAALModelChecker/UDBM "$UDBM_DIR"
fi
cd "$UDBM_DIR"
if [ ! -f "build-x86_64-linux-release/src/libUDBM.a" ]; then
    echo "Building UDBM..."
    ./getlibs.sh x86_64-linux
    ./compile.sh x86_64-linux-libs-release
else
    echo "UDBM already built."
fi
cd "$SCRIPT_DIR"

# ---------------------------------------------------------------------------
# 3. UTAP (UPPAAL timed-automata parser) — https://github.com/UPPAALModelChecker/utap
# ---------------------------------------------------------------------------
echo "=== Setting up UTAP ==="
if [ ! -d "$UTAP_DIR" ]; then
    git clone https://github.com/UPPAALModelChecker/utap "$UTAP_DIR"
fi
cd "$UTAP_DIR"
if [ ! -f "build-x86_64-linux-release/src/libUTAP.a" ]; then
    echo "Building UTAP..."
    ./getlibs.sh x86_64-linux
    ./compile.sh x86_64-linux
else
    echo "UTAP already built."
fi
cd "$SCRIPT_DIR"

echo "=== Setup complete ==="
echo "Build the project with:  make release"
