#!/usr/bin/env bash
# Capture current product screenshots from a disposable seeded demo stack.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
DATA="$(mktemp -d "${TMPDIR:-/tmp}/doppelganger-docs.XXXXXX")"
PORT=18080
STUDIO_PORT="$PORT" ./scripts/start-demo.sh --no-build --data "$DATA" >"$DATA/stack.log" 2>&1 &
STACK=$!
cleanup() {
  kill "$STACK" 2>/dev/null || true
  wait "$STACK" 2>/dev/null || true
  rm -rf "$DATA"
}
trap cleanup EXIT INT TERM
for _ in $(seq 1 600); do
  if curl -sf "http://127.0.0.1:$PORT/api/v1/instances/indoor-drone-dt" 2>/dev/null | grep -q '"state":"running"'; then
    break
  fi
  sleep 0.5
done
curl -sf "http://127.0.0.1:$PORT/api/v1/twins" >/dev/null || { cat "$DATA/stack.log" >&2; exit 1; }
STUDIO_URL="http://127.0.0.1:$PORT" node scripts/screenshots/current.mjs
