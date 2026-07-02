# Orbital LOS Viewer

Orbital LOS Viewer visualizes one primary satellite in Earth orbit plus up to
5,000 tracked objects (debris, stars, comets, other satellites, and hot
ground objects) in a browser, all in ECEF meters at 1 Hz. A C++ simulator
replays CSV missions (or synthesizes load-test data) as binary UDP packets to
a C++/Boost backend, which validates, aggregates, and rebroadcasts the world
state as WebSocket JSON to a dependency-free WebGL frontend. The whole stack
targets localhost/LAN use with no runtime internet access.

## Features

- Simplified Earth globe (graticule, Sun-lit shading) with an approximate
  Sun position computed from the client clock.
- All five tracked-object categories: debris, stars, comets, other
  satellites, and hot ground objects (rendered with distinct colors/sizes).
- Per-object trails, 0–60 s configurable, age-faded.
- Optional on-screen labels (nearest objects + satellite + current selection;
  capped for readability).
- UI panels: object list (filterable by category, capped with a
  "showing N of M" note), satellite telemetry, connection/status, latest
  data time, and live UDP/WebSocket stats.
- Settings menu (persisted to `localStorage`): trails on/off + duration,
  labels on/off, per-category visibility toggles, and WebSocket host/port
  with a reconnect button.

## Architecture

Data flow: **simulator → UDP (binary, `OLV1`) → backend (Boost.Asio/Beast,
one thread decodes+validates UDP, a second thread broadcasts a 1 Hz
WebSocket JSON snapshot) → browser (plain HTML/CSS/JS, custom minimal WebGL
renderer)**. There is exactly one shared, mutex-protected state store between
the backend's two threads, and no TLS/auth by design (see §11).

- Full design, threading model, and the Mermaid architecture diagram:
  [`docs/PLAN.md`](docs/PLAN.md)
- Binary UDP wire format (normative): [`docs/PROTOCOL_UDP.md`](docs/PROTOCOL_UDP.md)
- WebSocket JSON format (normative): [`docs/PROTOCOL_WS.md`](docs/PROTOCOL_WS.md)

## Prerequisites

A C++20 compiler, CMake ≥ 3.20, and Boost ≥ 1.74 headers (Asio/Beast are
header-only — no compiled Boost libraries are linked).

```sh
# Debian / Ubuntu
sudo apt install g++ cmake libboost-dev

# Fedora
sudo dnf install gcc-c++ cmake boost-devel

# Immutable/Atomic distros (Bazzite, Silverblue, etc.) via Homebrew/Linuxbrew
brew install cmake boost
```

Optional, for the full development workflow:

- **Node.js ≥ 18** — runs the frontend logic tests (`node --test "frontend/tests/*.test.mjs"`)
  and the integration test's frontend-parser check. Never required to serve
  or run the frontend itself.
- **Python 3** — runs `scripts/gen_sbom.py`, `scripts/serve_frontend.sh`,
  and `scripts/make_example_csv.py` (stdlib only, no pip packages).
- **cppcheck**, **clang-format** — optional lint/format tooling (see §8).

## Build & run

```sh
# Build (Boost >= 1.74 headers + CMake >= 3.20 + C++20 compiler)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Unit + integration tests
ctest --test-dir build --output-on-failure

# Frontend logic tests (Node >= 18, dev-only dependency)
node --test "frontend/tests/*.test.mjs"

# Run (three processes)
./build/backend/olv_backend --udp-port 47000 --ws-port 8765 --log-file olv_backend.log
./build/simulator/olv_sim --csv simulator/data/example_mission.csv --rate 1 --loop
./build/simulator/olv_sim --generate 5000 --rate 1        # load test
scripts/serve_frontend.sh 8000                            # then open http://localhost:8000/frontend/

# Lint / format / SBOM
cmake --build build --target format lint
python3 scripts/gen_sbom.py --out sbom/
```

Binaries land at `build/backend/olv_backend`, `build/backend/olv_ws_probe`,
and `build/simulator/olv_sim` (each target's default per-subdirectory output
directory; no `CMAKE_RUNTIME_OUTPUT_DIRECTORY` override is configured).

### One-liner

```sh
scripts/run_all.sh
```

Builds (only if the binaries are missing), then starts the backend, the
simulator (replaying `simulator/data/example_mission.csv` on loop at 1 Hz),
and the frontend static server together, printing every URL/port. A single
Ctrl-C stops all three processes.

## Configuration files

All three components read optional TOML configuration files. Commented
examples matching the built-in defaults live in [config/](config/) and
[frontend/config.toml](frontend/config.toml):

| Component | File | How it's loaded |
|---|---|---|
| Backend | `config/backend.toml` | `olv_backend --config config/backend.toml` |
| Simulator | `config/simulator.toml` | `olv_sim --config config/simulator.toml` |
| Frontend | `frontend/config.toml` | fetched relative to the page at startup |

Precedence is always *built-in defaults < config file < explicit overrides*
(command-line flags for the C++ binaries; the user's saved Settings, stored
in localStorage, for the frontend). Backend and simulator are **strict** —
unknown keys, wrong types, or out-of-range values abort startup with the
offending key and line so typos can't hide. The frontend is deliberately
**lenient** — a missing or invalid `config.toml` logs a `console.warn` and
falls back to defaults, so a bad deployment file never bricks the page.

Configurable items include the backend's UDP/WebSocket bind addresses and
ports, broadcast rate, object-expiry window and logging; the simulator's
target host/port, data source (CSV path or synthetic generation), rate,
chunking and seed; and the frontend's default WebSocket host/port and
display toggles (trails, labels, trail duration).

The files are parsed by a small first-party TOML *subset* parser
(`backend/include/olv/toml.hpp`, mirrored in `frontend/js/toml.js`): comments,
one level of `[tables]`, strings/integers/floats/booleans. Arrays, dotted
keys, dates and multi-line strings are rejected with a line-numbered error —
see the header comment in `toml.hpp` for the exact grammar.

## Simulator usage

```sh
./build/simulator/olv_sim [options]
```

| Flag | Purpose |
|---|---|
| `--csv PATH` | Replay a mission CSV file (see column format below) instead of synthesizing data. |
| `--generate N` | Synthesize N tracked objects (up to `kMaxTrackedObjects` = 5000) plus a primary satellite instead of reading a CSV — used for load testing. |
| `--dest HOST` | UDP destination host/IP (e.g. `127.0.0.1`, or a container/compose service name such as `backend`). |
| `--port N` | UDP destination port (matches the backend's `--udp-port`; protocol default `47000`). |
| `--rate N` | Update rate in Hz (protocol target/default: 1 Hz). |
| `--loop` | Replay a `--csv` mission repeatedly instead of exiting after one pass. |
| `--chunk N` | Cap objects per UDP packet (protocol max `kMaxObjectsPerPacket` = 128); lower it to stay under one Ethernet MTU and avoid IP fragmentation. |
| `--duration N` | Stop sending after N seconds; `0` runs indefinitely. |
| `--seed N` | Seed the synthetic generator (`--generate`); runs are always deterministic, default seed `1`. |
| `--quiet` | Suppress console progress output. |

### CSV column format

`simulator/data/example_mission.csv` and any `--csv` input use these
columns:

```
time_s,kind,id,type,px_m,py_m,pz_m,vx_mps,vy_mps,vz_mps,confidence,intensity,flags
```

| Column | Meaning |
|---|---|
| `time_s` | Seconds from mission start; rows sharing a `time_s` are sent together as one update cycle. |
| `kind` | Row kind — the primary satellite state vs. a tracked object record. |
| `id` | Object/satellite identifier (`u32`). |
| `type` | Object type: `1` debris, `2` star, `3` comet, `4` satellite, `5` ground-hot (see `docs/PROTOCOL_UDP.md` §3). |
| `px_m, py_m, pz_m` | ECEF position, meters. |
| `vx_mps, vy_mps, vz_mps` | ECEF velocity, m/s (blank/omitted when no velocity is available). |
| `confidence` | 0–100 (%). |
| `intensity` | Kelvin for ground-hot objects, apparent magnitude for stars, else 0. |
| `flags` | bit0 HAS_VELOCITY, bit1 HIGHLIGHT (see `docs/PROTOCOL_UDP.md` §3). |

## Testing

```sh
ctest --test-dir build --output-on-failure   # backend + simulator unit tests, and integration
node --test "frontend/tests/*.test.mjs"                # frontend logic (DOM-free) tests
```

`ctest` runs:

- **`backend_unit`** (`olv_backend_tests`) — protocol encode/decode
  round-trips, every `DecodeError` path, CRC vectors, sequence
  staleness/wraparound, object merge/expiry, JSON output shape.
- **`sim_unit`** (`olv_sim_tests`) — CSV happy path and malformed-row
  handling, chunking, sequence monotonicity.
- **`integration`** (`scripts/integration_test.sh`, wired via CTest,
  120 s timeout) — end-to-end: starts `olv_backend` on ephemeral UDP/WS
  ports, runs `olv_sim --generate 500 --dest 127.0.0.1 --rate 2 --duration 60`,
  captures broadcast frames with `olv_ws_probe`, checks their structure
  (`state`/`satellite`/`lastDataTime` present), re-parses them with the real
  frontend parser (`node frontend/tests/validate_message.mjs`) to prove the
  backend's JSON is frontend-compatible, and asserts the backend log recorded
  accepted packets and a client-connected event.

## Lint / format

```sh
cmake --build build --target format        # clang-format, in place
cmake --build build --target format-check  # clang-format, --dry-run --Werror
cmake --build build --target lint          # cppcheck (warning/performance/portability)
```

Both targets no-op with a notice if the underlying tool isn't installed.
`clang-tidy` (config: `.clang-tidy`) isn't wired into a CMake target since it
needs a compilation database — run it via your IDE, or manually:

```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build backend/src/*.cpp simulator/src/*.cpp
```

## Containers

Multi-stage, Podman-friendly images (non-root at runtime); see
`containers/Containerfile.cpp` (targets `backend`, `simulator`) and
`containers/Containerfile.frontend` for the exact commands and air-gap
notes.

```sh
podman build -f containers/Containerfile.cpp --target backend   -t olv-backend .
podman build -f containers/Containerfile.cpp --target simulator -t olv-sim .
podman build -f containers/Containerfile.frontend -t olv-frontend .

# or all three together:
podman-compose -f containers/compose.yaml up --build
# (docker compose -f containers/compose.yaml up --build works identically)
```

Then open <http://localhost:8000/frontend/> — the frontend's default WebSocket setting
(`localhost:8765`) matches the backend's published port, so no
configuration is needed.

## SBOM & licenses

```sh
python3 scripts/gen_sbom.py --out sbom/
```

Generates CycloneDX 1.5 SBOMs for the backend (`sbom/backend.cdx.json`,
records the detected Boost version) and the frontend
(`sbom/frontend.cdx.json`, zero third-party components), plus a one-line
license summary per component. Full strategy and sample output:
[`docs/SBOM.md`](docs/SBOM.md). Dependency/license table:
[`THIRD_PARTY.md`](THIRD_PARTY.md).

## Security scope

- **No TLS, no authentication, by design.** UDP and WebSocket are plaintext.
  This project is explicitly scoped to trusted localhost/LAN deployments,
  not the public internet.
- The backend writes a plain-text log file (default `olv_backend.log`,
  override with `--log-file`) recording accepted/dropped packets (with
  reason) and WebSocket connect/disconnect events.
- Every inbound UDP packet is validated before being applied: structural
  checks (length, magic, version, message type, object/count limits),
  CRC-32 integrity, NaN/Inf rejection, coordinate range bound, unknown
  object-type rejection, and wraparound-safe sequence-staleness rejection.
  Any failure drops the whole packet (never partially applied), increments a
  counter, and is logged — see `docs/PROTOCOL_UDP.md` §4 for the full,
  normative ordered list.

## Troubleshooting

- **`Could NOT find Boost` / CMake can't find Boost** (common on immutable
  distros with Homebrew/Linuxbrew-installed Boost):
  ```sh
  cmake -S . -B build -DCMAKE_PREFIX_PATH=$(brew --prefix)
  ```
  (`cmake/common.cmake` also auto-adds `$HOMEBREW_PREFIX` and
  `/home/linuxbrew/.linuxbrew` to `CMAKE_PREFIX_PATH` when present, so this
  is usually only needed with a nonstandard Homebrew install location.)
- **UDP packets from the simulator never arrive / blocked by `firewalld`:**
  ```sh
  sudo firewall-cmd --add-port=47000/udp --add-port=8765/tcp   # runtime only
  # or, to persist:
  sudo firewall-cmd --permanent --add-port=47000/udp --add-port=8765/tcp
  sudo firewall-cmd --reload
  ```
- **WS port busy** (`bind: address already in use` on 8765): another
  `olv_backend` (or something else) is already listening — stop it, or pass
  a different `--ws-port` to `olv_backend` and update the frontend's
  Settings → WebSocket port to match.
- **Page loads but the globe stays empty:** open Settings and confirm the
  WebSocket host/port match a running `olv_backend` (default
  `localhost:8765`); check the status panel — if it shows "stalled", the
  backend isn't reachable or isn't broadcasting.
- **Frontend works when served but not when opened directly:** the frontend
  uses ES modules, which browsers block from `file://` origins. Always serve
  it over `http://` — `scripts/serve_frontend.sh`,
  `containers/Containerfile.frontend`, or any static file server.

## Repo layout

```
.
├── CMakeLists.txt, cmake/common.cmake   # build config, format/lint targets
├── backend/                             # olv_backend (C++20, Boost.Asio/Beast)
├── simulator/                           # olv_sim (C++20)
├── frontend/                            # plain HTML/CSS/JS, no frameworks
├── tests/support/olv_test.hpp           # shared minimal C++ test framework
├── docs/                                # PLAN, PROTOCOL_UDP, PROTOCOL_WS, SBOM
├── scripts/                             # integration test, sbom gen, run/serve helpers
└── containers/                          # Containerfiles + compose.yaml
```

See [`docs/PLAN.md`](docs/PLAN.md) §3 for the full annotated tree.

## License

MIT — see [`LICENSE`](LICENSE). Dependency/license details:
[`THIRD_PARTY.md`](THIRD_PARTY.md).
