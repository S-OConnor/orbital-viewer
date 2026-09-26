#!/usr/bin/env bash
# run_all.sh — one-command demo: run backend + simulator + frontend together
# as a container stack via containers/compose.yaml. Images are built on first
# run (or with --build); a single Ctrl-C tears the whole stack down.
#
# Usage: scripts/run_all.sh [--ws-port N] [--udp-port N] [--http-port N]
#                           [--build] [-- <extra compose up args>]
#
# Defaults match docs/PLAN.md: WS 8765, UDP 47000, frontend http 8000. Only
# the *published host* ports change with those flags; the containers' internal
# ports stay fixed, so the internal simulator->backend wiring is unaffected.
# The simulator replays tools/simulator/data/example_mission.csv on loop at 1 Hz
# (baked into the image; see containers/compose.yaml).
#
# Requires a container engine with compose support. Detection order:
# 'podman compose', 'docker compose', 'podman-compose', 'docker-compose'.
# Override with OLV_COMPOSE, e.g.  OLV_COMPOSE="docker compose" scripts/run_all.sh
# Set OLV_BUILDER_IMAGE to use a prebuilt build-environment image (e.g. from
# your registry) instead of building containers/Dockerfile.builder locally.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
COMPOSE_FILE="${ROOT}/containers/compose.yaml"
PROJECT="olv"

# Exported so compose.yaml's ${OLV_*_PORT} host-port mappings pick them up.
export OLV_WS_PORT=8765
export OLV_UDP_PORT=47000
export OLV_HTTP_PORT=8000
FORCE_BUILD=0
EXTRA=()

while [ $# -gt 0 ]; do
  case "$1" in
    --ws-port)   OLV_WS_PORT="$2";   shift 2 ;;
    --udp-port)  OLV_UDP_PORT="$2";  shift 2 ;;
    --http-port) OLV_HTTP_PORT="$2"; shift 2 ;;
    --build)     FORCE_BUILD=1;      shift ;;
    --)          shift; EXTRA+=("$@"); break ;;
    *) echo "run_all.sh: unknown argument: $1" >&2; exit 1 ;;
  esac
done

# --- pick a compose command ------------------------------------------------
COMPOSE=()
if [ -n "${OLV_COMPOSE:-}" ]; then
  # shellcheck disable=SC2206
  COMPOSE=(${OLV_COMPOSE})
elif command -v podman >/dev/null 2>&1 && podman compose version >/dev/null 2>&1; then
  COMPOSE=(podman compose)
elif command -v docker >/dev/null 2>&1 && docker compose version >/dev/null 2>&1; then
  COMPOSE=(docker compose)
elif command -v podman-compose >/dev/null 2>&1; then
  COMPOSE=(podman-compose)
elif command -v docker-compose >/dev/null 2>&1; then
  COMPOSE=(docker-compose)
else
  echo "run_all.sh: no compose tool found. Install podman or docker (with the" >&2
  echo "            compose plugin), or set OLV_COMPOSE to a compose command." >&2
  exit 1
fi

COMPOSE=("${COMPOSE[@]}" -p "${PROJECT}" -f "${COMPOSE_FILE}")
echo "== compose engine: ${COMPOSE[*]} =="

LOGS_PID=""
cleanup() {
  [ -n "${LOGS_PID}" ] && kill "${LOGS_PID}" 2>/dev/null
  echo
  echo "== stopping (compose down) =="
  "${COMPOSE[@]}" down --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT
# Exit on Ctrl-C/TERM so the EXIT trap (teardown) runs even if the signal
# reaches only this script and not the log-follow child below.
trap 'exit 130' INT
trap 'exit 143' TERM

# --- build environment image (backend/simulator compile FROM it) -----------
# Layer-cached, so this is quick after the first run. Skip it when
# OLV_BUILDER_IMAGE names a prebuilt image (e.g. pulled from a registry).
if [ -z "${OLV_BUILDER_IMAGE:-}" ]; then
  echo "== building olv-builder (containers/Dockerfile.builder) =="
  if ! "${COMPOSE[@]}" --profile builder build builder; then
    echo "run_all.sh: building the olv-builder image failed" >&2
    exit 1
  fi
fi

# --- bring the stack up (detached; builds missing images) ------------------
UP_ARGS=(up -d --remove-orphans)
[ "${FORCE_BUILD}" -eq 1 ] && UP_ARGS+=(--build)

echo "== starting stack (ws=${OLV_WS_PORT} udp=${OLV_UDP_PORT} http=${OLV_HTTP_PORT}) =="
echo "   (first run builds images — this can take a few minutes)"
if ! "${COMPOSE[@]}" "${UP_ARGS[@]}" "${EXTRA[@]}"; then
  echo "run_all.sh: 'compose up' failed" >&2
  exit 1
fi

# --- wait for the backend WebSocket port to accept connections -------------
printf "== waiting for backend on port %s " "${OLV_WS_PORT}"
ready=0
for _ in $(seq 1 60); do
  if (exec 3<>"/dev/tcp/127.0.0.1/${OLV_WS_PORT}") 2>/dev/null; then
    exec 3>&- 3<&-
    ready=1
    break
  fi
  printf "."
  sleep 0.5
done
echo
if [ "${ready}" -ne 1 ]; then
  echo "run_all.sh: backend did not become ready on port ${OLV_WS_PORT}; recent logs:" >&2
  "${COMPOSE[@]}" logs --tail 40 backend >&2 || true
  exit 1
fi

cat <<EOF

Orbital LOS Viewer is running (containerized):
  Frontend:    http://localhost:${OLV_HTTP_PORT}/
  WebSocket:   ws://localhost:${OLV_WS_PORT}
  UDP (host):  127.0.0.1:${OLV_UDP_PORT}   (published; simulator->backend is internal)

Open http://localhost:${OLV_HTTP_PORT}/ in a browser. The frontend defaults to
WebSocket port 8765; if you passed --ws-port, set the new port under Settings.

Streaming container logs — press Ctrl-C to stop and tear the stack down.
EOF

# Follow logs in the background and wait, so a SIGINT delivered to this script
# interrupts the wait and triggers teardown (a foreground child would defer the
# trap until it exits). On Ctrl-C in a real terminal the whole process group is
# signaled, so the log-follow child dies too; cleanup() also kills it directly.
"${COMPOSE[@]}" logs -f &
LOGS_PID=$!
wait "${LOGS_PID}"
