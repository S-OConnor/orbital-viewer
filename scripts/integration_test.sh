#!/usr/bin/env bash
# integration_test.sh — end-to-end check: simulator --generate -> backend UDP
# -> WebSocket broadcast -> ws_probe capture -> structural + frontend-parser
# validation of the captured JSON frames + backend log assertions.
#
# Two legs, each a fresh backend + simulator pair: the default OLV1 path, then
# the OLV2 input mode (docs/PROTOCOL_OLV2.md), which must also deliver
# `trailPoints` (docs/PROTOCOL_WS.md §2) that OLV1 frames never carry.
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

LOG="$WORK/backend.log"  # log of the leg currently running

stop_pair() {
  [ -n "$SIM_PID" ] && kill "$SIM_PID" 2>/dev/null
  [ -n "$BACKEND_PID" ] && kill "$BACKEND_PID" 2>/dev/null
  wait 2>/dev/null
  SIM_PID="" BACKEND_PID=""
}

cleanup() {
  stop_pair
  rm -rf "$WORK"
}
trap cleanup EXIT

fail() {
  echo "INTEGRATION FAIL: $*" >&2
  echo "--- $(basename "$LOG") (tail) ---" >&2
  tail -30 "$LOG" 2>/dev/null >&2
  exit 1
}

# Wait for the WebSocket port ($1) to accept connections.
wait_ws() {
  local up=0
  for _ in $(seq 1 50); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null; then
      exec 3>&- 3<&-; up=1; break
    fi
    kill -0 "$BACKEND_PID" 2>/dev/null || fail "backend exited during startup"
    sleep 0.2
  done
  [ "$up" = 1 ] || fail "backend WS port never came up"
}

# Re-parse captured frames ($1) with the real frontend parser, requiring at
# least $2 objects.
validate_frames() {
  if command -v node >/dev/null 2>&1; then
    node "$ROOT/frontend/tests/validate_message.mjs" "$1" --min-objects "$2" ||
      fail "frontend parser rejected backend JSON"
  else
    echo "note: node not found, skipping frontend-parser validation"
  fi
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

wait_ws "$WS_PORT"

"$SIM" --generate 500 --dest 127.0.0.1 --port "$UDP_PORT" --rate 2 --duration 60 &
SIM_PID=$!

"$PROBE" --host 127.0.0.1 --port "$WS_PORT" --count 6 --timeout 30 \
         --out "$WORK/frames.jsonl" || fail "ws_probe failed (rc=$?)"

# --- Structural checks on captured frames ---------------------------------
[ -s "$WORK/frames.jsonl" ] || fail "no frames captured"
grep -q '"type":"state"' "$WORK/frames.jsonl" || fail "no state frame captured"
grep -q '"satellite":{'  "$WORK/frames.jsonl" || fail "no frame carried satellite state"
grep -q '"lastDataTime":"' "$WORK/frames.jsonl" || fail "lastDataTime never set"
grep -q '"trailPoints"' "$WORK/frames.jsonl" && fail "OLV1 frames must not carry trailPoints"

# --- Frontend-compatibility: re-parse frames with the real frontend parser -
validate_frames "$WORK/frames.jsonl" 100

# --- Backend log assertions ------------------------------------------------
grep -qi 'accepted'         "$WORK/backend.log" || fail "log has no accepted packets"
grep -qi 'client connected' "$WORK/backend.log" || fail "log has no connection event"
echo "leg olv1: pass"
stop_pair

# ===========================================================================
# Leg 2: OLV2 input mode. Mode and port come from the config file (there are
# no --olv2-* flags); 10 points/datagram at 10 Hz = one batch per second.
# ===========================================================================
OLV2_PORT=$(( UDP_PORT + 1 ))
WS2_PORT=$(( WS_PORT + 1 ))
LOG="$WORK/backend_olv2.log"
cat > "$WORK/backend_olv2.toml" <<TOML
[input]
mode = "olv2"
olv2_bind = "127.0.0.1"
olv2_port = $OLV2_PORT
[logging]
level = "debug"
TOML

"$BACKEND" --config "$WORK/backend_olv2.toml" --ws-port "$WS2_PORT" \
           --log-file "$LOG" --quiet &
BACKEND_PID=$!
wait_ws "$WS2_PORT"

"$SIM" --generate 50 --dest 127.0.0.1 --port "$OLV2_PORT" --protocol olv2 --rate 10 \
       --duration 60 --quiet &
SIM_PID=$!

"$PROBE" --host 127.0.0.1 --port "$WS2_PORT" --count 6 --timeout 30 \
         --out "$WORK/frames_olv2.jsonl" || fail "ws_probe failed on olv2 leg (rc=$?)"

[ -s "$WORK/frames_olv2.jsonl" ] || fail "no olv2 frames captured"
grep -q '"satellite":{' "$WORK/frames_olv2.jsonl" || fail "olv2 never set the satellite"
grep -q '"trailPoints":\[\[' "$WORK/frames_olv2.jsonl" || fail "olv2 frames carried no trailPoints"
validate_frames "$WORK/frames_olv2.jsonl" 40

grep -q 'input_mode=olv2' "$LOG" || fail "startup log lacks input_mode=olv2"
grep -q 'accepted track=' "$LOG" || fail "log has no accepted olv2 datagrams"
grep -q 'dropped' "$LOG" && fail "olv2 leg dropped datagrams"
echo "leg olv2: pass"

echo "INTEGRATION PASS"
exit 0
