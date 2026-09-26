#!/usr/bin/env bash
# cleanup.sh — stop the project's container webservers and any leftover
# run-script processes.
#
# 1. Tears down the "olv" compose stack (backend + frontend, and the
#    simulator if it was started with --sim) started by scripts/run_all.sh,
#    the same way run_all.sh's own EXIT trap would (compose down).
# 2. Kills any still-running scripts/run_all.sh or scripts/serve_frontend.sh
#    processes (and the http.server child serve_frontend.sh execs), in case
#    the stack was started in a way that skipped the normal Ctrl-C teardown.
#
# Usage: scripts/cleanup.sh

set -u

# shellcheck source=scripts/_common.sh
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"
ROOT="${OLV_ROOT}"
PROJECT="olv"

if olv_find_compose; then
  # --profile sim so the (optional) simulator container is removed too.
  echo "== stopping compose stack: ${COMPOSE[*]} down =="
  "${COMPOSE[@]}" --profile sim down --remove-orphans || true
else
  echo "== no compose tool found; skipping compose down ==" >&2
fi

# --- belt-and-suspenders: kill any olv_* containers directly ---------------
for engine in podman docker; do
  command -v "${engine}" >/dev/null 2>&1 || continue
  ids="$("${engine}" ps -q --filter "name=^${PROJECT}_" 2>/dev/null)"
  [ -n "${ids}" ] && { echo "== ${engine}: stopping leftover ${PROJECT}_* containers =="; "${engine}" stop ${ids}; }
done

# --- kill leftover run-script processes and their children -----------------
echo "== killing leftover run_all.sh / serve_frontend.sh processes =="
pkill -f "scripts/run_all.sh" 2>/dev/null && echo "   stopped run_all.sh"
pkill -f "scripts/serve_frontend.sh" 2>/dev/null && echo "   stopped serve_frontend.sh"
pkill -f "http.server.*--directory ${ROOT}" 2>/dev/null && echo "   stopped python http.server on ${ROOT}"

echo "== cleanup done =="
