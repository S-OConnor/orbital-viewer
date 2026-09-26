# Orbital LOS Viewer

Orbital LOS Viewer visualizes one primary satellite in Earth orbit plus up to
5,000 tracked objects (debris, stars, comets, other satellites, and hot
ground objects) in a browser, all in ECEF meters at 1 Hz. A C++ simulator
replays CSV missions (or synthesizes load-test data) as binary UDP packets to
a C++/Boost backend, which validates, aggregates, and rebroadcasts the world
state as WebSocket JSON to a dependency-free WebGL frontend. The whole stack
targets localhost/LAN use with no runtime internet access.

![alt text](<Screenshot From 2026-07-02 06-49-04.png>)

## Features

- Simplified Earth globe (graticule, Sun-lit shading) with an approximate
  Sun position computed from the state message's timestamp, textured with
  vendored public-domain NASA Blue Marble (day) and Black Marble (night)
  imagery — see [frontend/assets/README.md](frontend/assets/README.md) and
  [THIRD_PARTY.md](THIRD_PARTY.md).
- All six tracked-object categories: debris, stars, comets, other
  satellites, hot ground objects, and unknown (objects whose type couldn't
  be determined) — each rendered with a distinct color/size.
- Optional time-accurate celestial background: the ~200 brightest stars, the
  Moon (with an illuminated-fraction readout), and the five naked-eye planets,
  all positioned from the state message's `serverTime` and drawn in both view
  modes (see [docs/features/FEATURE_SKY.md](docs/features/FEATURE_SKY.md)).
- Per-object trails, 0–60 s configurable, age-faded.
- Optional on-screen labels (nearest objects + satellite + current selection;
  capped for readability).
- UI panels: object list (filterable by category, capped with a
  "showing N of M" note), satellite telemetry, connection/status, latest
  data time, and live UDP/WebSocket stats.
- Two view modes, toggled with the 3D/SAT control or the `V` key: the free
  orbit camera, and a satellite point-of-view mode — a 170° equidistant-fisheye
  nadir view rendered from the primary satellite, wide enough to show the
  space around Earth's limb (see
  [docs/features/FEATURE_SATVIEW.md](docs/features/FEATURE_SATVIEW.md)).
- Settings menu (persisted to `localStorage`): trails on/off + duration,
  labels on/off, sky (stars/Moon/planets) on/off, per-category visibility
  toggles, view mode, and WebSocket host/port with a reconnect button.
- Boot-selectable backend input source: the first-party OLV1 UDP protocol
  (default), or IEEE 1278.1 DIS Entity State PDUs
  (`olv_backend --input-mode dis`); the simulator can emit either
  (`olv_sim --protocol dis`). See
  [docs/PROTOCOL_DIS.md](docs/PROTOCOL_DIS.md) and
  [docs/features/FEATURE_INPUT_SOURCES.md](docs/features/FEATURE_INPUT_SOURCES.md).

## Architecture

Data flow: **simulator → UDP (binary, `OLV1`) → backend (Boost.Asio/Beast,
one thread decodes+validates UDP, a second thread broadcasts a 1 Hz
WebSocket JSON snapshot) → browser (plain HTML/CSS/JS, custom minimal WebGL
renderer)**. There is exactly one shared, mutex-protected state store between
the backend's two threads, and no TLS/auth by design (see §11).

- Full design, threading model, and the Mermaid architecture diagram:
  [`docs/PLAN.md`](docs/PLAN.md)
- Binary UDP wire format (normative): [`docs/PROTOCOL_UDP.md`](docs/PROTOCOL_UDP.md)
- DIS input mode — accepted PDU subset & mapping (normative):
  [`docs/PROTOCOL_DIS.md`](docs/PROTOCOL_DIS.md)
- WebSocket JSON format (normative): [`docs/PROTOCOL_WS.md`](docs/PROTOCOL_WS.md)

## Prerequisites

**Docker or Podman — that's the only thing you need on the host.** The C++20
toolchain, CMake, Boost ≥ 1.74 headers, and the one compiled third-party
dependency (`open-dis-cpp` v1.2.0, IEEE 1278.1 DIS support) all live inside
the `olv-builder` container image built from
[`containers/Dockerfile.builder`](containers/Dockerfile.builder); nothing
needs to be installed on the host to build, test, or run the backend and
simulator (see [Build & run](#build--run) below).

Optional, for the full development workflow:

- **Node.js ≥ 18** — runs the frontend logic tests (`node --test "frontend/tests/*.test.mjs"`)
  and the integration test's frontend-parser check. Runs on the host; never
  required to serve or run the frontend itself.
- **Python 3** — runs `scripts/serve_frontend.sh` and
  `scripts/make_example_csv.py` (stdlib only, no pip packages). Runs on
  the host.
- **cppcheck**, **clang-format** — optional lint/format tooling (see §8);
  not included in the `olv-builder` image today, so the `lint`/`format`
  CMake targets no-op unless you add them or run natively (see below).

> **Native build (advanced, unsupported):** a native C++20 toolchain, CMake
> ≥ 3.20, Boost ≥ 1.74 headers, and an OpenDIS CMake package
> (`OpenDIS::OpenDIS6`) on `CMAKE_PREFIX_PATH` also work — see
> [`containers/Dockerfile.builder`](containers/Dockerfile.builder) for the
> exact package list this repo is tested against, and
> how it builds and installs open-dis-cpp (the `OPEN_DIS_*` step). This
> path has no documented step-by-step; the container is the supported
> workflow.

## Build & run

```sh
# 1. Build the build-environment image once (or pull a prebuilt one from your
#    registry and skip straight to step 2 — see OLV_BUILDER_IMAGE in §Containers)
docker build -f containers/Dockerfile.builder -t localhost/olv-builder:latest .

# 2. Build + test inside it — binaries land in ./build-docker on the host
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/src -w /src localhost/olv-builder:latest \
  sh -c 'cmake -S . -B build-docker && cmake --build build-docker -j"$(nproc)" && ctest --test-dir build-docker --output-on-failure'
# (podman: drop --user — rootless Podman already maps the container's root to
# you; add :z to the volume flag on SELinux hosts)

# Frontend logic tests (Node >= 18, runs on the host, dev-only dependency)
node --test "frontend/tests/*.test.mjs"

# 3. Run (three processes) — the binaries need only glibc/libstdc++ (Boost is
# header-only, open-dis is linked statically), so they run directly on a
# compatible host; otherwise run them via the same `docker run` pattern
./build-docker/olv_backend --udp-port 47000 --ws-port 8765 --log-file olv_backend.log
./build-docker/tools/simulator/olv_sim --csv tools/simulator/data/example_mission.csv --rate 1 --loop
./build-docker/tools/simulator/olv_sim --generate 5000 --rate 1        # load test
scripts/serve_frontend.sh 8000                            # then open http://localhost:8000/frontend/

# Or ingest IEEE 1278.1 DIS Entity State PDUs instead of OLV1 (see
# docs/PROTOCOL_DIS.md; first uncomment dis_satellite_entity_id in the config —
# DIS mode requires it and it has no CLI flag)
./build-docker/olv_backend --config config/backend.toml --input-mode dis
./build-docker/tools/simulator/olv_sim --generate 100 --protocol dis   # emits DIS to port 47001

# Lint / format (inside the container; add cppcheck/clang-format to a derived
# image, or run natively, to make these do more than no-op):
docker run --rm -v "$PWD":/src -w /src localhost/olv-builder:latest \
  sh -c 'cmake --build build-docker --target format lint'
```

Binaries land at `build-docker/olv_backend`, `build-docker/olv_ws_probe` (both
have an explicit per-target `RUNTIME_OUTPUT_DIRECTORY` pointing at the
top-level build directory), and `build-docker/tools/simulator/olv_sim` (no
override, so it uses CMake's default per-subdirectory output location).
`build-docker/` (like any `build*/` directory) is excluded from container
build contexts by `.dockerignore`.

### One-liner (containers)

```sh
scripts/run_all.sh
```

Brings up the whole stack as containers via `containers/compose.yaml` — the
backend, the simulator (replaying `tools/simulator/data/example_mission.csv` on loop
at 1 Hz), and the frontend served by nginx. It auto-detects a compose engine
(`podman compose`, `docker compose`, `podman-compose`, or `docker-compose`;
override with `OLV_COMPOSE`), builds images on first run, waits for the
backend to accept connections, prints the URLs, then streams logs. A single
Ctrl-C stops and removes the whole stack.

```sh
scripts/run_all.sh --build                 # force an image rebuild after code changes
scripts/run_all.sh --http-port 9000        # remap a published host port (also --ws-port/--udp-port)
```

Then open <http://localhost:8000/>. (To build and run the binaries yourself
instead of the full container stack, see [Build & run](#build--run) above.)

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
ports, broadcast rate, object-expiry window, logging, and input mode
(`[input] mode = "olv1" | "dis"` plus the DIS-only `dis_*` keys — see
`docs/PROTOCOL_DIS.md` §8); the simulator's target host/port, data source
(CSV path or synthetic generation), rate, chunking, seed, and wire protocol
(`[send] protocol = "olv1" | "dis"`); and the frontend's default WebSocket
host/port and display toggles (trails, labels, trail duration).

The files are parsed by a small first-party TOML *subset* parser
(`include/olv/toml.hpp`, mirrored in `frontend/js/toml.js`): comments,
one level of `[tables]`, strings/integers/floats/booleans. Arrays, dotted
keys, dates and multi-line strings are rejected with a line-numbered error —
see the header comment in `toml.hpp` for the exact grammar.

## Simulator usage

```sh
./build-docker/tools/simulator/olv_sim [options]
```

| Flag | Purpose |
|---|---|
| `--csv PATH` | Replay a mission CSV file (see column format below) instead of synthesizing data. |
| `--generate N` | Synthesize N tracked objects (up to `kMaxTrackedObjects` = 5000) plus a primary satellite instead of reading a CSV — used for load testing. |
| `--protocol P` | Wire protocol: `olv1` (default, docs/PROTOCOL_UDP.md) or `dis` (IEEE 1278.1 Entity State PDUs, docs/PROTOCOL_DIS.md §9). With `dis` and no explicit `--port`, the destination port defaults to `47001` to match the backend's DIS default; the DIS-only settings (`dis_exercise_id`, `dis_site`, `dis_satellite_entity_id`) come from `config/simulator.toml`. |
| `--dest HOST` | UDP destination host/IP (e.g. `127.0.0.1`, or a container/compose service name such as `backend`). |
| `--port N` | UDP destination port (matches the backend's `--udp-port`; protocol default `47000`). |
| `--rate N` | Update rate in Hz (protocol target/default: 1 Hz). |
| `--loop` | Replay a `--csv` mission repeatedly instead of exiting after one pass. |
| `--chunk N` | Cap objects per UDP packet (protocol max `kMaxObjectsPerPacket` = 128); lower it to stay under one Ethernet MTU and avoid IP fragmentation. |
| `--duration N` | Generate mode only: stop sending after N seconds; `0` runs indefinitely (default `120`). |
| `--seed N` | Seed the synthetic generator (`--generate`); runs are always deterministic, default seed `1`. |
| `--quiet` | Suppress console progress output. |

### CSV column format

`tools/simulator/data/example_mission.csv` and any `--csv` input use these
columns:

```
time_s,kind,id,type,px_m,py_m,pz_m,vx_mps,vy_mps,vz_mps,confidence,intensity,flags
```

| Column | Meaning |
|---|---|
| `time_s` | Seconds from mission start; rows sharing a `time_s` are sent together as one update cycle. |
| `kind` | Row kind — the primary satellite state vs. a tracked object record. |
| `id` | Object/satellite identifier (`u32`). |
| `type` | Object type: `0`/empty unknown, `1` debris, `2` star, `3` comet, `4` satellite, `5` ground-hot (see `docs/PROTOCOL_UDP.md` §3). |
| `px_m, py_m, pz_m` | ECEF position, meters. |
| `vx_mps, vy_mps, vz_mps` | ECEF velocity, m/s (blank/omitted when no velocity is available). |
| `confidence` | 0–100 (%). |
| `intensity` | Kelvin for ground-hot objects, apparent magnitude for stars, else 0. |
| `flags` | bit0 HAS_VELOCITY, bit1 HIGHLIGHT (see `docs/PROTOCOL_UDP.md` §3). |

## Testing

```sh
docker run --rm -v "$PWD":/src -w /src localhost/olv-builder:latest \
  sh -c 'ctest --test-dir build-docker --output-on-failure'   # backend + simulator unit tests, and integration
node --test "frontend/tests/*.test.mjs"                # frontend logic (DOM-free) tests, on the host
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
docker run --rm -v "$PWD":/src -w /src localhost/olv-builder:latest \
  sh -c 'cmake --build build-docker --target format'        # clang-format, in place
docker run --rm -v "$PWD":/src -w /src localhost/olv-builder:latest \
  sh -c 'cmake --build build-docker --target format-check'  # clang-format, --dry-run --Werror
docker run --rm -v "$PWD":/src -w /src localhost/olv-builder:latest \
  sh -c 'cmake --build build-docker --target lint'           # cppcheck (warning/performance/portability)
```

Both targets no-op with a notice if the underlying tool isn't installed — the
`olv-builder` image doesn't include `clang-format`/`cppcheck` today, so add
them to a derived image (or run these targets from a native build) to get
real output. `clang-tidy` (config: `.clang-tidy`) isn't wired into a CMake
target since it needs a compilation database, and needs a native toolchain to
run via your IDE or manually (advanced):

```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build src/*.cpp tools/simulator/src/*.cpp
```

## Containers

Multi-stage, Podman-friendly images (non-root at runtime); see
`containers/Dockerfile.builder` (the Rocky Linux 10.2 build-environment image
— toolchain + Boost + open-dis-cpp, meant to be built once and pushed to an
internal registry for offline builds — this is the same image used to build
the binaries directly in [Build & run](#build--run) above), `containers/Dockerfile`
(targets `backend`, `simulator`, both Rocky Linux 10.2-minimal at runtime) and
`containers/Containerfile.frontend` for the exact commands and air-gap
notes.

```sh
podman build -f containers/Dockerfile.builder -t localhost/olv-builder:latest .
podman build -f containers/Dockerfile --target backend   -t olv-backend .
podman build -f containers/Dockerfile --target simulator -t olv-sim .
podman build -f containers/Containerfile.frontend -t olv-frontend .

# or all three together (podman compose / docker compose are equivalent):
podman compose -f containers/compose.yaml up --build
# or, with readiness wait + one-Ctrl-C teardown, the wrapper:
scripts/run_all.sh
```

Then open <http://localhost:8000/> — in the container image nginx serves the
frontend at the root (`containers/Containerfile.frontend` also `COPY`s `docs/`
into the image alongside it, so the About modal's protocol links resolve).
The frontend's default WebSocket setting
(`localhost:8765`) matches the backend's published port, so no configuration
is needed. Published host ports can be remapped without touching the
containers' internal ports via `OLV_WS_PORT` / `OLV_UDP_PORT` /
`OLV_HTTP_PORT` (which `scripts/run_all.sh` sets from its `--*-port` flags);
the build context excludes host artifacts via `.dockerignore`.

## SBOM & licenses

Hand-maintained CycloneDX 1.5 SBOMs are committed in `sbom/`: the backend
(`sbom/backend.cdx.json` — Boost and open-dis-cpp at the versions in the
`olv-builder` image) and the frontend (`sbom/frontend.cdx.json`, zero
third-party *code* components, plus two `file`-type components for the
vendored public-domain NASA image assets). Edit them directly when a
dependency, version, or asset changes. Contents and update checklist:
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
  normative ordered list (and `docs/PROTOCOL_DIS.md` §7 for the equivalent
  ordered list in DIS input mode).

## Troubleshooting

- **`Could NOT find Boost` / CMake can't find Boost** — only relevant to a
  native/advanced build (see §Prerequisites); the supported
  `containers/Dockerfile.builder` image already has Boost installed. Natively,
  this is common on immutable distros with Homebrew/Linuxbrew-installed Boost:
  ```sh
  cmake -S . -B build -DCMAKE_PREFIX_PATH=$(brew --prefix)
  ```
  (the top-level `CMakeLists.txt` also auto-adds `$HOMEBREW_PREFIX` and
  `/home/linuxbrew/.linuxbrew` to `CMAKE_PREFIX_PATH` when present.)
- **`open-dis-cpp not found` at configure time:** only relevant to a
  native/advanced build — the `olv-builder` container image already has it
  installed (built from the pinned release in the image). Building
  natively anyway, point CMake at the install with `-DCMAKE_PREFIX_PATH=DIR`
  (or `-DOpenDIS_DIR=DIR/lib64/cmake/OpenDIS`) if it isn't in `/usr/local` or
  `~/.local`.
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
├── CMakeLists.txt                       # project setup, dependencies, subdirs; format/lint targets
├── include/olv/                         # backend headers (C++20, Boost.Asio/Beast)
├── src/                                 # backend sources (olv_backend)
├── test/                                # backend unit tests + support/olv_test.hpp
├── tools/                               # ws_probe.cpp (integration-test WS client)
│   └── simulator/                       # olv_sim (C++20), standalone-configurable
├── frontend/                            # plain HTML/CSS/JS, no frameworks
├── docs/                                # PLAN, PROTOCOL_{UDP,DIS,WS}, SBOM, features/, site/
├── config/                              # commented example backend.toml / simulator.toml
├── scripts/                             # integration test, run/serve helpers         
└── containers/                          # Dockerfile.builder, Dockerfile, Containerfile.frontend, compose.yaml
```

See [`docs/PLAN.md`](docs/PLAN.md) §3 for the full annotated tree.

## License

MIT — see [`LICENSE`](LICENSE). Dependency/license details:
[`THIRD_PARTY.md`](THIRD_PARTY.md).
