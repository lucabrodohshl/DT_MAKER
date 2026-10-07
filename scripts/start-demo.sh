#!/usr/bin/env bash
# =============================================================================
# Verified Twin Studio — one-command demo.
#
#   ./scripts/start-demo.sh            (or: make demo)
#
# Builds what is missing, seeds the example Blueprints on first start (each one
# is validated, aligned, compiled, scenario-tested, packaged and published for
# real, and its instance is deployed), starts Verified Twin Studio and prints
# the URL:
#
#   twin-studio   :8080  Verified Twin Studio (web UI + platform API + proxy)
#
# Studio's deployment supervisor then starts the processes of every deployed
# twin instance on free local ports (18100-18999), exactly as a Release ->
# Deployment does in the UI:
#
#   indoor-drone-dt  twin-world (simulator generated from the Blueprint's world)
#                    + twin-runtime (co-simulation; paused until "Start mission")
#   pump-p101-dt     twin-runtime (monitor mode) + twin-pt-feed (the Blueprint's
#                    event-script simulator)
#
# Each runtime executes exactly the package its instance is deployed on, so the
# hashes shown in Studio and the ones recorded in the runtime ledgers are the same.
#
# Options:
#   --fresh        delete the demo data (Studio database, packages, ledgers) first
#   --no-build     do not build (use existing binaries and web/studio/dist)
#   --data DIR     data directory (default var/demo)
#
# The Studio port can be overridden with STUDIO_PORT.
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
STUDIO_PORT="${STUDIO_PORT:-8080}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fresh) FRESH=1; shift ;;
        --no-build) BUILD=0; shift ;;
        --data) DATA="$2"; shift 2 ;;
        -h|--help) sed -n '2,36p' "$0"; exit 0 ;;
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

# ----------------------------------------------------------------- prerequisites
command -v cmake >/dev/null || die "cmake is required"
command -v python3 >/dev/null || die "python3 is required"
command -v curl >/dev/null || die "curl is required"
port_free "$STUDIO_PORT" || die "port $STUDIO_PORT is in use (another demo running?)"

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
mkdir -p "$LOGS"
if [[ ! -f "$STUDIO_DATA/studio.db" ]]; then
    log "seeding the example Blueprints (validation, alignment, compilation, scenario tests and packaging run for real)"
    "$BIN/twin-studio" seed --example examples/industrial-pump --data-dir "$STUDIO_DATA" | tee -a "$LOGS/seed.log"
    "$BIN/twin-studio" seed --example examples/indoor-drone --data-dir "$STUDIO_DATA" | tee -a "$LOGS/seed.log"
fi

# --------------------------------------------------------------------- processes
WEB_ROOT=()
[[ -f web/studio/dist/index.html ]] && WEB_ROOT=(--web-root web/studio/dist)

log "starting Verified Twin Studio on :$STUDIO_PORT (it deploys the twin instances itself)"
"$BIN/twin-studio" serve --data-dir "$STUDIO_DATA" --port $STUDIO_PORT ${WEB_ROOT[@]+"${WEB_ROOT[@]}"} \
    --bin-dir "$BIN" --templates examples/templates >"$LOGS/twin-studio.log" 2>&1 &
PIDS+=($!)
wait_http "http://127.0.0.1:$STUDIO_PORT/api/v1/twins" "twin-studio" 60

wait_instance() {  # instance id: wait until the supervisor reports it running
    local id="$1" i state
    for ((i = 0; i < 240; i++)); do
        state="$(curl -sf "http://127.0.0.1:$STUDIO_PORT/api/v1/instances/$id" \
            | python3 -c "import json,sys; print((json.load(sys.stdin).get('runtime') or {}).get('state',''))" 2>/dev/null || true)"
        case "$state" in
            running) log "instance $id is running"; return 0 ;;
            failed) die "instance $id failed to start (see $STUDIO_DATA/instances/$id/logs)" ;;
        esac
        sleep 0.25
    done
    die "instance $id did not start within 60 s (see $LOGS/twin-studio.log)"
}
wait_instance pump-p101-dt
wait_instance indoor-drone-dt

cat <<EOF

  ┌────────────────────────────────────────────────────────────────────┐
  │  Verified Twin Studio is running:  http://127.0.0.1:$STUDIO_PORT            │
  └────────────────────────────────────────────────────────────────────┘
  Showcase: Your Twins → Indoor Inspection Drone → Start mission
  Author:   Studio → Blueprints (each example is an editable, versioned Blueprint)
  Logs:     $LOGS/      Data: $DATA/      Stop: Ctrl-C

EOF
if [[ ${#WEB_ROOT[@]} -eq 0 ]]; then
    log "web/studio/dist not found: the API is up; run 'npm run dev' in web/studio for the UI"
fi
wait
