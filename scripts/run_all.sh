#!/usr/bin/env bash
# run_all.sh — one-command demo: build if needed, then run backend +
# simulator + frontend static server together. A single Ctrl-C stops all
# three.
#
# Usage: scripts/run_all.sh [--udp-port N] [--ws-port N] [--http-port N]
#
# Defaults match docs/PLAN.md: UDP 47000, WS 8765, frontend http 8000.
# Simulator replays simulator/data/example_mission.csv on loop at 1 Hz.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT}/build"

UDP_PORT=47000
WS_PORT=8765
HTTP_PORT=8000

while [ $# -gt 0 ]; do
  case "$1" in
    --udp-port) UDP_PORT="$2"; shift 2 ;;
    --ws-port) WS_PORT="$2"; shift 2 ;;
    --http-port) HTTP_PORT="$2"; shift 2 ;;
    *) echo "run_all.sh: unknown argument: $1" >&2; exit 1 ;;
  esac
done

BACKEND_BIN="${BUILD_DIR}/backend/olv_backend"
SIM_BIN="${BUILD_DIR}/simulator/olv_sim"

if [ ! -x "${BACKEND_BIN}" ] || [ ! -x "${SIM_BIN}" ]; then
  echo "== binaries missing, building (cmake -S . -B build && cmake --build build -j) =="
  cmake -S "${ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
  cmake --build "${BUILD_DIR}" -j
fi

if [ ! -x "${BACKEND_BIN}" ]; then
  echo "run_all.sh: expected backend binary not found at ${BACKEND_BIN}" >&2
  exit 1
fi
if [ ! -x "${SIM_BIN}" ]; then
  echo "run_all.sh: expected simulator binary not found at ${SIM_BIN}" >&2
  exit 1
fi

PIDS=()

cleanup() {
  echo
  echo "== stopping =="
  for pid in "${PIDS[@]:-}"; do
    kill "${pid}" 2>/dev/null || true
  done
  wait 2>/dev/null
  exit 0
}
trap cleanup INT TERM

echo "== starting backend (udp=${UDP_PORT} ws=${WS_PORT}) =="
echo "   log: ${BUILD_DIR}/olv_backend.log"
"${BACKEND_BIN}" --udp-port "${UDP_PORT}" --ws-port "${WS_PORT}" \
                 --log-file "${BUILD_DIR}/olv_backend.log" &
PIDS+=("$!")

# Give the backend a moment to bind its sockets before the simulator starts
# sending it data.
sleep 1

echo "== starting simulator (--csv simulator/data/example_mission.csv --loop --rate 1) =="
"${SIM_BIN}" --csv "${ROOT}/simulator/data/example_mission.csv" --dest 127.0.0.1 \
             --port "${UDP_PORT}" --loop --rate 1 &
PIDS+=("$!")

echo "== starting frontend static server =="
# Repo root (not frontend/) so ../docs links in the About modal resolve.
python3 -m http.server "${HTTP_PORT}" --directory "${ROOT}" --bind 0.0.0.0 \
  >/dev/null 2>&1 &
PIDS+=("$!")

cat <<EOF

Orbital LOS Viewer is running:
  Frontend:    http://localhost:${HTTP_PORT}/frontend/
  WebSocket:   ws://localhost:${WS_PORT}
  UDP target:  127.0.0.1:${UDP_PORT}  (simulator -> backend)
  Backend log: ${BUILD_DIR}/olv_backend.log

Open http://localhost:${HTTP_PORT}/frontend/ in a browser. Press Ctrl-C here to stop
all processes.
EOF

wait
