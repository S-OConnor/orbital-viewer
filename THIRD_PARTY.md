# Third-party dependencies & licenses

Orbital LOS Viewer is designed to be air-gap friendly. The backend and
simulator have two runtime third-party dependencies — Boost (header-only,
system-provided) and `open-dis-cpp` (built from a pinned upstream release and
installed into `/usr/local` by `containers/Dockerfile.builder`, statically linked;
used by `olv_backend --input-mode dis` and `olv_sim --protocol dis`) — plus a
small, build-time-only toolchain and a handful of optional developer tools. The frontend ships **zero** third-party code (it does vendor
two public-domain NASA image assets — see
[Third-party assets](#third-party-assets)).

| Dependency | Role | License | Notes |
|---|---|---|---|
| [Boost](https://www.boost.org/) ≥ 1.74 | Runtime (header-only) | [BSL-1.0](https://www.boost.org/LICENSE_1_0.txt) | Used by `olv_backend` and `olv_sim` for Asio (UDP/TCP), Beast (WebSocket/HTTP framing), and core headers. Header-only usage only — no compiled Boost libraries are linked. Not vendored; expected to be provided by the system or a toolchain package manager (see README §4 Prerequisites). Exact installed version is recorded in `sbom/backend.cdx.json` by `scripts/gen_sbom.py`. |
| [open-dis-cpp](https://github.com/open-dis/open-dis-cpp) v1.2.0 | Runtime (installed prefix, static) | [BSD-2-Clause](https://github.com/open-dis/open-dis-cpp/blob/v1.2.0/LICENSE) | IEEE 1278.1 DIS Entity State PDUs: decode for `olv_backend --input-mode dis`, encode for `olv_sim --protocol dis` (docs/PROTOCOL_DIS.md, docs/features/FEATURE_INPUT_SOURCES.md). **Not vendored**: [`containers/Dockerfile.builder`](containers/Dockerfile.builder) fetches the pinned v1.2.0 tarball (sha256-verified) and builds it with upstream's own CMake project (static, no local modifications), installing `include/dis6`+`include/dis7`, `lib64/libOpenDIS6.a`+`libOpenDIS7.a` (DIS7 is built only because upstream's package config requires it; only DIS6 is used), a `lib64/cmake/OpenDIS/` package config, and LICENSE/provenance into `/usr/local` of the `olv-builder` build-environment image (native builds replicate that step into a prefix of their choice). The project consumes it via `find_package(OpenDIS CONFIG)` / `OpenDIS::OpenDIS6`, which CMake finds automatically in `/usr/local`, and in `~/.local` when `~/.local/bin` is on `PATH`; other prefixes need `-DCMAKE_PREFIX_PATH=DIR`. The BSD-2-Clause text + provenance are installed at `/usr/local/share/doc/open-dis-cpp/`. Recorded as a `library` component in `sbom/backend.cdx.json`. |
| CMake ≥ 3.20 | Build-only | [BSD-3-Clause](https://cmake.org/licensing/) | Build system generator; not shipped with the built binaries. |
| GCC ≥ 12 or Clang ≥ 14 | Build-only | [GPLv3](https://gcc.gnu.org/) / [Apache-2.0 with LLVM exception](https://llvm.org/LICENSE.txt) | C++20 compiler; not shipped with the built binaries. |
| Node.js ≥ 18 | Dev/test-only | [MIT](https://github.com/nodejs/node/blob/main/LICENSE) | Runs `node --test frontend/tests/` and the integration test's frontend-parser check. Never required at runtime — the frontend is plain, static HTML/CSS/JS served by any HTTP server (see `scripts/serve_frontend.sh`, `containers/Containerfile.frontend`). |
| Python 3 | Scripts-only | [PSF License](https://docs.python.org/3/license.html) | Used only by repo scripts (`scripts/gen_sbom.py`, `scripts/serve_frontend.sh`'s `http.server`, `scripts/make_example_csv.py`) — stdlib only, no pip packages. Never a runtime dependency of `olv_backend` or `olv_sim`. |
| clang-format (optional) | Dev-only | [Apache-2.0 with LLVM exception](https://llvm.org/LICENSE.txt) | `cmake --build build --target format` / `format-check`; no-op with a notice if not installed. |
| clang-tidy (optional) | Dev-only | [Apache-2.0 with LLVM exception](https://llvm.org/LICENSE.txt) | Not wired into a CMake target (needs `compile_commands.json`); run manually, see `.clang-tidy` / README §8. |
| cppcheck (optional) | Dev-only | [GPLv3](https://cppcheck.sourceforge.io/) | `cmake --build build --target lint`; no-op with a notice if not installed. |

## Third-party assets

The frontend's WebGL Earth globe uses two vendored, public-domain NASA Earth
imagery textures as static image assets (`frontend/assets/`). These are data,
not code: no third-party JS/CSS/frameworks are introduced by them, and they
are served same-origin like any other static frontend file.

| Dependency | Role | License | Notes |
|---|---|---|---|
| [NASA Blue Marble: Next Generation](https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x5400x2700.jpg) (`frontend/assets/earth_day.jpg`) | Runtime (static image asset, frontend) | Public domain (NASA Media Usage Guidelines) | Dec 2004 composite, 5400×2700 equirectangular. Credit: NASA Earth Observatory / Reto Stöckli. Downsized to power-of-two texture dimensions in the browser at load time; not processed in-repo. Full provenance (retrieval date, SHA-256, byte size): [`frontend/assets/README.md`](frontend/assets/README.md). |
| [NASA Black Marble — "Earth at Night 2012"](https://eoimages.gsfc.nasa.gov/images/imagerecords/79000/79765/dnb_land_ocean_ice.2012.3600x1800.jpg) (`frontend/assets/earth_night.jpg`) | Runtime (static image asset, frontend) | Public domain (NASA Media Usage Guidelines) | Suomi NPP VIIRS, 3600×1800 equirectangular. Credit: NASA Earth Observatory / NOAA NGDC. Downsized to power-of-two texture dimensions in the browser at load time; not processed in-repo. Full provenance: [`frontend/assets/README.md`](frontend/assets/README.md). |

Both are also recorded as `file`-type components (with computed SHA-256
hashes) in the frontend SBOM — see `docs/SBOM.md`.

## First-party code that looks like a dependency

- **`test/support/olv_test.hpp`** — the in-repo C++ unit test framework
  (~120 lines, no fixtures/mocking). This is first-party code owned by this
  repository, licensed MIT under the same `LICENSE` file as everything else.
  It exists specifically so the repo does not need to vendor or depend on
  doctest/Catch2/GoogleTest (see `docs/PLAN.md` §10 for the trade-off).
- **`frontend/`** — plain HTML/CSS/JS with ES modules and a hand-rolled
  minimal WebGL renderer. No frameworks, no bundler, no `node_modules` at
  runtime. Node is used exclusively as a development-time test runner
  (`node --test frontend/tests/`); it is never required to serve or run the
  frontend.

## Regenerating the dependency/license report (SBOM)

```sh
python3 scripts/gen_sbom.py --out sbom/
```

Generates `sbom/backend.cdx.json` and `sbom/frontend.cdx.json` (CycloneDX
1.5) plus a one-line license summary per component on stdout. See
`docs/SBOM.md` for the full strategy, sample output, and how to feed these
BOMs to a vulnerability scanner.

## Air-gap notes

- **Native build:** the only external requirements are a system Boost
  (≥ 1.74, headers only) and a C++20 toolchain (CMake + GCC/Clang), both
  installable from a local/offline package mirror — see README §4
  (Prerequisites) and §12 (Troubleshooting) for per-distro install commands
  and `-DCMAKE_PREFIX_PATH` fallbacks (e.g. Homebrew/Linuxbrew on immutable
  distros). CMake fetches nothing from the network. `open-dis-cpp` must be
  built and installed once from a mirrored copy of the pinned v1.2.0 tarball,
  following the `OPEN_DIS_*` step in `containers/Dockerfile.builder` (verify
  the pinned sha256 before building).
- **Container build:** build (or pull from an internal registry) the
  `olv-builder` image once, on a connected machine — `containers/Dockerfile.builder`
  needs a mirrored `dnf` repository (for `gcc-c++ cmake make boost-devel`) and
  the open-dis-cpp tarball (pass `--build-arg OLV_OPEN_DIS_URL=<mirror-url>`).
  After that, `containers/Dockerfile` builds `backend`/`simulator` fully
  offline `FROM` that image plus a mirrored `rockylinux:10.2-minimal` runtime
  base. See the comments at the top of `containers/Dockerfile.builder` and
  `containers/Dockerfile` for exact commands.
- Neither the backend nor the simulator nor the frontend makes any outbound
  network call at runtime; all traffic is UDP/WebSocket on localhost/LAN
  between the three processes (see README §11, Security scope).
