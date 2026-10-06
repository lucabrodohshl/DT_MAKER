#!/usr/bin/env bash
# =============================================================================
# Verified Twin Studio — one-command demo.
#
#   ./scripts/start-demo.sh            (or: make demo)
#
# Builds what is missing, seeds the examples on first start, launches every
# process of the integrated product and prints the URL:
#
#   twin-studio   :8080  Verified Twin Studio (web UI + platform API + proxy)
#   twin-runtime  :8090  indoor-drone twin (co-simulation mode; paused until "Start mission")
#   twin-world    :8091  drone Physical Twin simulator + building-information service
#   twin-runtime  :8092  industrial-pump twin (monitor mode)
#   twin-pt-feed         scripted PLC feed of pump P-101 (events + telemetry)
#
# Each runtime executes exactly the package that Studio deployed for its twin
# (looked up through the Studio API), so the hashes shown in Studio and the
# ones recorded in the runtime ledgers are the same.
#
# Options:
#   --fresh        delete the demo data (Studio database, packages, ledgers) first
#   --no-build     do not build (use existing binaries and web/studio/dist)
#   --speed X      drone co-simulation speed (logical s per wall s, default 1.5)
#   --data DIR     data directory (default var/demo)
#
# Ports can be overridden with STUDIO_PORT, DRONE_PORT, WORLD_PORT and PUMP_PORT.
#
# Persistence: everything lives in the data directory and survives restarts
# (assets, telemetry history, artefact versions, evidence, packages,
# deployments, engineering audit, execution ledgers). Stop with Ctrl-C.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PRESET="studio-release"
BIN="build/$PRESET/bin"
DATA="var/demo"
FRESH=0
BUILD=1
SPEED="1.5"
STUDIO_PORT="${STUDIO_PORT:-8080}"
DRONE_PORT="${DRONE_PORT:-8090}"
WORLD_PORT="${WORLD_PORT:-8091}"
PUMP_PORT="${PUMP_PORT:-8092}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fresh) FRESH=1; shift ;;
        --no-build) BUILD=0; shift ;;
        --speed) SPEED="$2"; shift 2 ;;
        --data) DATA="$2"; shift 2 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

STUDIO_DATA="$DATA/studio"
LOGS="$DATA/logs"
PIDS=()

log() { printf '\033[1m[demo]\033[0m %s\n' "$*"; }
die() { printf '\033[31m[demo] %s\033[0m\n' "$*" >&2; exit 1; }

cleanup() {
    if [[ ${#PIDS[@]} -gt 0 ]]; then
        log "stopping ${#PIDS[@]} processes"
        kill "${PIDS[@]}" 2>/dev/null || true
        wait "${PIDS[@]}" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

port_free() { ! (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null; }

wait_http() {  # url, name, timeout seconds
    local url="$1" name="$2" timeout="${3:-60}" i
    for ((i = 0; i < timeout * 4; i++)); do
        if curl -sf "$url" >/dev/null 2>&1; then return 0; fi
        sleep 0.25
    done
    die "$name did not become ready at $url (see $LOGS)"
}

json_get() {  # json-text, python expression on `d`
    python3 -c "import json,sys; d=json.load(sys.stdin); print($2)" <<<"$1"
}

# ----------------------------------------------------------------- prerequisites
command -v cmake >/dev/null || die "cmake is required"
command -v python3 >/dev/null || die "python3 is required"
command -v curl >/dev/null || die "curl is required"
for p in $STUDIO_PORT $DRONE_PORT $WORLD_PORT $PUMP_PORT; do
    port_free "$p" || die "port $p is in use (another demo running?)"
done

# ------------------------------------------------------------------------- build
if [[ $BUILD -eq 1 ]]; then
    if [[ ! -d third_party/install ]]; then
        log "building third-party dependencies (once; several minutes)"
        ./scripts/bootstrap-deps.sh
    fi
    if [[ ! -f "build/$PRESET/CMakeCache.txt" ]]; then
        log "configuring ($PRESET)"
        cmake --preset "$PRESET" >/dev/null
    fi
    log "building C++ components"
    cmake --build "build/$PRESET" --target twin twin-studio twin-runtime twin-world twin-pt-feed -j 8 >/dev/null
    if [[ -f web/studio/package.json ]]; then
        command -v npm >/dev/null || die "npm is required to build the web UI"
        if [[ ! -d web/studio/node_modules ]]; then
            log "installing web UI dependencies"
            (cd web/studio && npm ci --no-audit --no-fund >/dev/null)
        fi
        log "building the web UI"
        (cd web/studio && npm run build >/dev/null)
    fi
fi
for b in twin-studio twin-runtime twin-world twin-pt-feed; do
    [[ -x "$BIN/$b" ]] || die "missing $BIN/$b (run without --no-build)"
done

# -------------------------------------------------------------------------- data
if [[ $FRESH -eq 1 && -d "$DATA" ]]; then
    log "removing $DATA (--fresh)"
    rm -rf "$DATA"
fi
mkdir -p "$LOGS" "$DATA/ledgers/drone" "$DATA/ledgers/pump"
if [[ ! -f "$STUDIO_DATA/studio.db" ]]; then
    log "seeding examples (validation, refinement, alignment, compilation and packaging run for real)"
    "$BIN/twin-studio" seed --example examples/industrial-pump --data-dir "$STUDIO_DATA" | tee -a "$LOGS/seed.log"
    "$BIN/twin-studio" seed --example examples/indoor-drone --data-dir "$STUDIO_DATA" | tee -a "$LOGS/seed.log"
fi

# --------------------------------------------------------------------- processes
WEB_ROOT=()
[[ -f web/studio/dist/index.html ]] && WEB_ROOT=(--web-root web/studio/dist)

log "starting twin-world (drone Physical Twin) on :$WORLD_PORT"
"$BIN/twin-world" --scenario scenarios/inspection_default.json --port $WORLD_PORT >"$LOGS/twin-world.log" 2>&1 &
PIDS+=($!)

log "starting Verified Twin Studio on :$STUDIO_PORT"
"$BIN/twin-studio" serve --data-dir "$STUDIO_DATA" --port $STUDIO_PORT ${WEB_ROOT[@]+"${WEB_ROOT[@]}"} \
    --runtime "indoor-drone-dt=http://127.0.0.1:$DRONE_PORT" \
    --world "indoor-drone-dt=http://127.0.0.1:$WORLD_PORT" \
    --runtime "pump-p101-dt=http://127.0.0.1:$PUMP_PORT" >"$LOGS/twin-studio.log" 2>&1 &
PIDS+=($!)
wait_http "http://127.0.0.1:$STUDIO_PORT/api/v1/twins" "twin-studio" 60
wait_http "http://127.0.0.1:$WORLD_PORT/health" "twin-world" 30

deployed_package_dir() {  # twin id -> package directory of its current deployment
    local twin="$1" body id
    body="$(curl -sf "http://127.0.0.1:$STUDIO_PORT/api/v1/twins/$twin")" || die "Studio does not know twin $twin"
    id="$(json_get "$body" "(d.get('deployment') or {}).get('packageId') or (d.get('package') or {}).get('id') or ''")"
    [[ -n "$id" && -d "$STUDIO_DATA/packages/$id" ]] || die "twin $twin has no deployed package"
    echo "$STUDIO_DATA/packages/$id"
}
DRONE_PKG="$(deployed_package_dir indoor-drone-dt)"
PUMP_PKG="$(deployed_package_dir pump-p101-dt)"

log "starting the drone twin runtime on :$DRONE_PORT (package $(basename "$DRONE_PKG"), paused)"
"$BIN/twin-runtime" --package "$DRONE_PKG" --world "http://127.0.0.1:$WORLD_PORT" --port $DRONE_PORT \
    --ledger-dir "$DATA/ledgers/drone" --package-store "$STUDIO_DATA/packages" --speed "$SPEED" --paused \
    >"$LOGS/twin-runtime-drone.log" 2>&1 &
PIDS+=($!)

log "starting the pump twin runtime on :$PUMP_PORT (package $(basename "$PUMP_PKG"), monitor mode)"
"$BIN/twin-runtime" --package "$PUMP_PKG" --monitor --port $PUMP_PORT \
    --ledger-dir "$DATA/ledgers/pump" --package-store "$STUDIO_DATA/packages" \
    >"$LOGS/twin-runtime-pump.log" 2>&1 &
PIDS+=($!)
wait_http "http://127.0.0.1:$DRONE_PORT/health" "drone runtime" 30
wait_http "http://127.0.0.1:$PUMP_PORT/health" "pump runtime" 30

log "starting the pump PLC feed"
"$BIN/twin-pt-feed" --feed scenarios/pump_operating_cycle.json --runtime "http://127.0.0.1:$PUMP_PORT" --speed 1 \
    >"$LOGS/twin-pt-feed.log" 2>&1 &
PIDS+=($!)

cat <<EOF

  ┌────────────────────────────────────────────────────────────────────┐
  │  Verified Twin Studio is running:  http://127.0.0.1:$STUDIO_PORT            │
  └────────────────────────────────────────────────────────────────────┘
  Showcase: Examples → Indoor Inspection Drone → Start mission
  Logs:     $LOGS/      Data: $DATA/      Stop: Ctrl-C

EOF
if [[ ${#WEB_ROOT[@]} -eq 0 ]]; then
    log "web/studio/dist not found: the API is up; run 'npm run dev' in web/studio for the UI"
fi
wait
