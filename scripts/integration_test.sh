#!/usr/bin/env bash
# integration_test.sh — end-to-end check: simulator --generate -> backend UDP
# -> WebSocket broadcast -> ws_probe capture -> structural + frontend-parser
# validation of the captured JSON frames + backend log assertions.
#
# Usage: integration_test.sh BACKEND_BIN SIM_BIN PROBE_BIN [REPO_ROOT]
# Wired into CTest by the top-level CMakeLists (test name: integration).

set -u

BACKEND=${1:?backend binary}
SIM=${2:?simulator binary}
PROBE=${3:?ws_probe binary}
ROOT=${4:-$(cd "$(dirname "$0")/.." && pwd)}

WORK=$(mktemp -d)
BACKEND_PID="" SIM_PID=""

cleanup() {
  [ -n "$SIM_PID" ] && kill "$SIM_PID" 2>/dev/null
  [ -n "$BACKEND_PID" ] && kill "$BACKEND_PID" 2>/dev/null
  wait 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

fail() {
  echo "INTEGRATION FAIL: $*" >&2
  echo "--- backend.log (tail) ---" >&2
  tail -30 "$WORK/backend.log" 2>/dev/null >&2
  exit 1
}

UDP_PORT=$(( (RANDOM % 2000) + 46000 ))
WS_PORT=$(( (RANDOM % 2000) + 18000 ))
echo "ports: udp=$UDP_PORT ws=$WS_PORT work=$WORK"

# Exercise the TOML config path: the file sets the log level (and a bogus
# UDP port that the CLI flag must override — this fails loudly if the
# defaults < file < flags precedence ever breaks).
cat > "$WORK/backend.toml" <<EOF
[network]
udp_port = 1
[logging]
level = "debug"
EOF

"$BACKEND" --config "$WORK/backend.toml" --udp-port "$UDP_PORT" --ws-port "$WS_PORT" \
           --log-file "$WORK/backend.log" --quiet &
BACKEND_PID=$!

# Wait for the WebSocket port to accept connections.
up=0
for _ in $(seq 1 50); do
  if (exec 3<>"/dev/tcp/127.0.0.1/$WS_PORT") 2>/dev/null; then
    exec 3>&- 3<&-; up=1; break
  fi
  kill -0 "$BACKEND_PID" 2>/dev/null || fail "backend exited during startup"
  sleep 0.2
done
[ "$up" = 1 ] || fail "backend WS port never came up"

"$SIM" --generate 500 --dest 127.0.0.1 --port "$UDP_PORT" --rate 2 --duration 60 &
SIM_PID=$!

"$PROBE" --host 127.0.0.1 --port "$WS_PORT" --count 6 --timeout 30 \
         --out "$WORK/frames.jsonl" || fail "ws_probe failed (rc=$?)"

# --- Structural checks on captured frames ---------------------------------
[ -s "$WORK/frames.jsonl" ] || fail "no frames captured"
grep -q '"type":"state"' "$WORK/frames.jsonl" || fail "no state frame captured"
grep -q '"satellite":{'  "$WORK/frames.jsonl" || fail "no frame carried satellite state"
grep -q '"lastDataTime":"' "$WORK/frames.jsonl" || fail "lastDataTime never set"

# --- Frontend-compatibility: re-parse frames with the real frontend parser -
if command -v node >/dev/null 2>&1; then
  node "$ROOT/frontend/tests/validate_message.mjs" "$WORK/frames.jsonl" \
       --min-objects 100 || fail "frontend parser rejected backend JSON"
else
  echo "note: node not found, skipping frontend-parser validation"
fi

# --- Backend log assertions ------------------------------------------------
grep -qi 'accepted'         "$WORK/backend.log" || fail "log has no accepted packets"
grep -qi 'client connected' "$WORK/backend.log" || fail "log has no connection event"

echo "INTEGRATION PASS"
exit 0
