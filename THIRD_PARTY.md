# Third-party dependencies & licenses

Orbital LOS Viewer is designed to be air-gap friendly: it has exactly one
runtime third-party dependency (Boost, header-only), plus a small,
build-time-only toolchain and a handful of optional developer tools. The
frontend ships **zero** third-party code.

| Dependency | Role | License | Notes |
|---|---|---|---|
| [Boost](https://www.boost.org/) ≥ 1.74 | Runtime (header-only) | [BSL-1.0](https://www.boost.org/LICENSE_1_0.txt) | Used by `olv_backend` and `olv_sim` for Asio (UDP/TCP), Beast (WebSocket/HTTP framing), and core headers. Header-only usage only — no compiled Boost libraries are linked. Not vendored; expected to be provided by the system or a toolchain package manager (see README §4 Prerequisites). Exact installed version is recorded in `sbom/backend.cdx.json` by `scripts/gen_sbom.py`. |
| CMake ≥ 3.20 | Build-only | [BSD-3-Clause](https://cmake.org/licensing/) | Build system generator; not shipped with the built binaries. |
| GCC ≥ 12 or Clang ≥ 14 | Build-only | [GPLv3](https://gcc.gnu.org/) / [Apache-2.0 with LLVM exception](https://llvm.org/LICENSE.txt) | C++20 compiler; not shipped with the built binaries. |
| Node.js ≥ 18 | Dev/test-only | [MIT](https://github.com/nodejs/node/blob/main/LICENSE) | Runs `node --test frontend/tests/` and the integration test's frontend-parser check. Never required at runtime — the frontend is plain, static HTML/CSS/JS served by any HTTP server (see `scripts/serve_frontend.sh`, `containers/Containerfile.frontend`). |
| Python 3 | Scripts-only | [PSF License](https://docs.python.org/3/license.html) | Used only by repo scripts (`scripts/gen_sbom.py`, `scripts/serve_frontend.sh`'s `http.server`, `scripts/make_example_csv.py`) — stdlib only, no pip packages. Never a runtime dependency of `olv_backend` or `olv_sim`. |
| clang-format (optional) | Dev-only | [Apache-2.0 with LLVM exception](https://llvm.org/LICENSE.txt) | `cmake --build build --target format` / `format-check`; no-op with a notice if not installed. |
| clang-tidy (optional) | Dev-only | [Apache-2.0 with LLVM exception](https://llvm.org/LICENSE.txt) | Not wired into a CMake target (needs `compile_commands.json`); run manually, see `.clang-tidy` / README §8. |
| cppcheck (optional) | Dev-only | [GPLv3](https://cppcheck.sourceforge.io/) | `cmake --build build --target lint`; no-op with a notice if not installed. |

## First-party code that looks like a dependency

- **`tests/support/olv_test.hpp`** — the in-repo C++ unit test framework
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
  distros). CMake fetches nothing from the network.
- **Container build:** additionally needs a mirrored `apt` repository (for
  `g++ cmake make libboost-dev` in the build stage of
  `containers/Containerfile.cpp`) and pre-pulled/mirrored base images
  (`debian:bookworm-slim`, `nginx:alpine-slim`). See the comments at the top
  of each `containers/Containerfile.*` for exact commands.
- Neither the backend nor the simulator nor the frontend makes any outbound
  network call at runtime; all traffic is UDP/WebSocket on localhost/LAN
  between the three processes (see README §11, Security scope).
