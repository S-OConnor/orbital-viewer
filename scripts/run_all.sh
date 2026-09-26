#!/usr/bin/env bash
# run_all.sh — step 6: run the container stack via containers/compose.yaml:
# backend + frontend, plus the simulator with --sim. A single Ctrl-C tears
# the whole stack down.
#
# Usage: scripts/run_all.sh [--sim] [--build] [--ws-port N] [--udp-port N]
#                           [--http-port N] [-- <extra compose up args>]
#   --sim     also start the simulator (compose profile "sim"), replaying
#             tools/simulator/data/example_mission.csv on loop at 1 Hz
#   --build   run scripts/build_all.sh first (build, test, package) and
#             rebuild the frontend image
#
# The backend and simulator images come from the build pipeline
# (scripts/build_all.sh); this script never compiles anything unless --build
# is given. Set OLV_BACKEND_IMAGE / OLV_SIM_IMAGE to run registry images.
#
# Defaults match docs/PLAN.md: WS 8765, UDP 47000, frontend http 8000. Only
# the *published host* ports change with those flags; the containers' internal
# ports stay fixed, so the internal simulator->backend wiring is unaffected.
#
# Engine/compose detection lives in scripts/_common.sh (OLV_ENGINE and
# OLV_COMPOSE override it).

set -u
# shellcheck source=scripts/_common.sh
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

# Exported so compose.yaml's ${OLV_*_PORT} host-port mappings pick them up.
export OLV_WS_PORT=8765
export OLV_UDP_PORT=47000
export OLV_HTTP_PORT=8000
WITH_SIM=0
FORCE_BUILD=0
EXTRA=()

while [ $# -gt 0 ]; do
  case "$1" in
    --sim)       WITH_SIM=1;         shift ;;
    --ws-port)   OLV_WS_PORT="$2";   shift 2 ;;
    --udp-port)  OLV_UDP_PORT="$2";  shift 2 ;;
    --http-port) OLV_HTTP_PORT="$2"; shift 2 ;;
    --build)     FORCE_BUILD=1;      shift ;;
    --)          shift; EXTRA+=("$@"); break ;;
    *) olv_die "unknown argument: $1" ;;
  esac
done

olv_find_compose ||
  olv_die "no compose tool found. Install podman or docker (with the compose plugin), or set OLV_COMPOSE."
echo "== compose engine: ${COMPOSE[*]} =="

# --- build pipeline (optional) / check the runtime images exist --------------
if [ "${FORCE_BUILD}" -eq 1 ]; then
  "$(dirname "${BASH_SOURCE[0]}")/build_all.sh" || olv_die "build pipeline failed"
fi
NEEDED=("${OLV_BACKEND_IMAGE}")
[ "${WITH_SIM}" -eq 1 ] && NEEDED+=("${OLV_SIM_IMAGE}")
for image in "${NEEDED[@]}"; do
  # Registry images are pulled by compose; local ones must already exist.
  if [[ "${image}" == localhost/* ]] && ! olv_image_exists "${image}"; then
    olv_die "${image} not found; run scripts/build_all.sh (or pass --build)"
  fi
done

LOGS_PID=""
cleanup() {
  [ -n "${LOGS_PID}" ] && kill "${LOGS_PID}" 2>/dev/null
  echo
  echo "== stopping (compose down) =="
  # --profile sim so down also removes the simulator if it was started.
  "${COMPOSE[@]}" --profile sim down --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT
# Exit on Ctrl-C/TERM so the EXIT trap (teardown) runs even if the signal
# reaches only this script and not the log-follow child below.
trap 'exit 130' INT
trap 'exit 143' TERM

# --- bring the stack up (detached; builds missing images) ------------------
PROFILE_ARGS=()
[ "${WITH_SIM}" -eq 1 ] && PROFILE_ARGS=(--profile sim)
UP_ARGS=(up -d --remove-orphans)
[ "${FORCE_BUILD}" -eq 1 ] && UP_ARGS+=(--build)

# Always rebuild the frontend image: it is just frontend/ + docs/ COPY'd onto
# nginx (a cache hit when nothing changed), and `up` alone only builds it when
# missing — so a stale image would keep serving old JS that can reject newer
# backend frames (connected, but nothing drawn).
if [ "${FORCE_BUILD}" -eq 0 ]; then
  echo "== building frontend image =="
  "${COMPOSE[@]}" build frontend || olv_die "frontend image build failed"
fi

SERVICES="backend + frontend"
[ "${WITH_SIM}" -eq 1 ] && SERVICES="${SERVICES} + simulator"
echo "== starting ${SERVICES} (ws=${OLV_WS_PORT} udp=${OLV_UDP_PORT} http=${OLV_HTTP_PORT}) =="
if ! "${COMPOSE[@]}" "${PROFILE_ARGS[@]}" "${UP_ARGS[@]}" "${EXTRA[@]}"; then
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
  Simulator:   $([ "${WITH_SIM}" -eq 1 ] && echo "replaying example_mission.csv" || echo "off (pass --sim to start it)")

Open http://localhost:${OLV_HTTP_PORT}/ in a browser. The frontend defaults to
WebSocket port 8765; if you passed --ws-port, set the new port under Settings.

Streaming container logs — press Ctrl-C to stop and tear the stack down.
EOF

# Follow logs in the background and wait, so a SIGINT delivered to this script
# interrupts the wait and triggers teardown (a foreground child would defer the
# trap until it exits). On Ctrl-C in a real terminal the whole process group is
# signaled, so the log-follow child dies too; cleanup() also kills it directly.
"${COMPOSE[@]}" "${PROFILE_ARGS[@]}" logs -f &
LOGS_PID=$!
wait "${LOGS_PID}"
