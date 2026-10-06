#!/usr/bin/env bash
# =============================================================================
# Regenerate docs/screenshots/ from the running integrated product.
#
# Starts an ISOLATED demo stack (ports 18080/18090/18091/18092, data in
# var/screenshots, fixed SOURCE_DATE_EPOCH so package hashes are stable),
# drives it with Playwright (scripts/screenshots/capture.mjs) and stops it.
# A demo already running on the default ports is not affected.
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export STUDIO_PORT=18080 DRONE_PORT=18090 WORLD_PORT=18091 PUMP_PORT=18092
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1791072000}"
DATA="var/screenshots"
rm -rf "$DATA"
./scripts/start-demo.sh --data "$DATA" "$@" > "$ROOT/var/screenshots-stack.log" 2>&1 &
STACK=$!
trap 'kill $STACK 2>/dev/null || true; wait $STACK 2>/dev/null || true' EXIT
for _ in $(seq 1 600); do
    if curl -sf "http://127.0.0.1:$PUMP_PORT/health" >/dev/null 2>&1 && curl -sf "http://127.0.0.1:$STUDIO_PORT/api/v1/twins" >/dev/null 2>&1; then
        break
    fi
    sleep 0.5
done
curl -sf "http://127.0.0.1:$STUDIO_PORT/api/v1/twins" >/dev/null || { echo "stack did not start (see var/screenshots-stack.log)"; exit 1; }
sleep 3  # let the pump feed produce a little live data
STUDIO_URL="http://127.0.0.1:$STUDIO_PORT" node scripts/screenshots/capture.mjs
# Tutorial "Safely Evolving an Ontology" (mutates the pump twin: draft, release, deploy).
STUDIO_URL="http://127.0.0.1:$STUDIO_PORT" node scripts/screenshots/tutorial.mjs
echo "screenshots written to docs/screenshots/"
