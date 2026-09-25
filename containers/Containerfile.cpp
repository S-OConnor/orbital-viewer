# Containerfile.cpp — multi-stage build for olv_backend and olv_sim.
#
# Build:
#   podman build -f containers/Containerfile.cpp --target backend   -t olv-backend .
#   podman build -f containers/Containerfile.cpp --target simulator -t olv-sim .
# (docker build works identically; substitute `docker` for `podman`.)
#
# Run:
#   podman run --rm -p 8765:8765 -p 47000:47000/udp olv-backend
#   podman run --rm --network host olv-sim --dest 127.0.0.1 --port 47000
#
# Air-gap notes:
#   - The build stage needs `apt-get` access to a Debian package repository
#     (base OS packages + libboost-dev). In an air-gapped environment, point
#     apt at a local/mirrored repository (e.g. via /etc/apt/sources.list or an
#     apt-cacher-ng mirror) before running `podman build`.
#   - The build stage also fetches the pinned open-dis-cpp v1.2.0 source
#     tarball (sha256-verified; see scripts/install_open_dis.sh) and compiles
#     it into /usr/local — the library is NOT vendored in this repository. In
#     an air-gapped environment, mirror that tarball internally and pass
#     `--build-arg OLV_OPEN_DIS_URL=<mirror-url>`; the pinned sha256 is still
#     enforced. CMake itself fetches nothing.
#   - The base image (docker.io/library/debian:bookworm-slim) must be
#     pre-pulled or mirrored into a local registry ahead of time.
#   - No component here reaches the network at runtime.

# ---------------------------------------------------------------------------
# Stage: build — compiles olv_backend and olv_sim with CMake.
# ---------------------------------------------------------------------------
FROM docker.io/library/debian:bookworm-slim AS build

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        g++ \
        cmake \
        make \
        libboost-dev \
        curl \
        ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# open-dis-cpp (third-party, BSD-2-Clause, pinned v1.2.0): built from the
# upstream release straight into /usr/local. Runs before COPY . . so the
# layer (including the download) is cached across ordinary source changes.
ARG OLV_OPEN_DIS_URL=
COPY scripts/install_open_dis.sh /tmp/install_open_dis.sh
RUN OLV_OPEN_DIS_URL="${OLV_OPEN_DIS_URL}" /tmp/install_open_dis.sh --prefix /usr/local && \
    rm /tmp/install_open_dis.sh

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOLV_BUILD_TESTS=OFF && \
    cmake --build build -j "$(nproc)" --target olv_backend olv_sim

# ---------------------------------------------------------------------------
# Target: backend — minimal runtime image running as a non-root user.
# ---------------------------------------------------------------------------
FROM docker.io/library/debian:bookworm-slim AS backend

RUN groupadd --gid 10001 olv && \
    useradd --uid 10001 --gid olv --no-create-home --shell /usr/sbin/nologin olv

COPY --from=build /src/build/olv_backend /app/olv_backend

USER 10001
WORKDIR /app

EXPOSE 8765
EXPOSE 47000/udp

ENTRYPOINT ["/app/olv_backend"]
CMD ["--udp-port", "47000", "--ws-port", "8765", "--log-file", "/tmp/olv_backend.log"]

# ---------------------------------------------------------------------------
# Target: simulator — minimal runtime image running as a non-root user.
# Includes tools/simulator/data so --csv replay works without a bind mount.
# ---------------------------------------------------------------------------
FROM docker.io/library/debian:bookworm-slim AS simulator

RUN groupadd --gid 10001 olv && \
    useradd --uid 10001 --gid olv --no-create-home --shell /usr/sbin/nologin olv

COPY --from=build /src/build/tools/simulator/olv_sim /app/olv_sim
COPY tools/simulator/data /app/data

USER 10001
WORKDIR /app

ENTRYPOINT ["/app/olv_sim"]
CMD ["--generate", "1000", "--dest", "backend", "--port", "47000", "--rate", "1", "--duration", "0"]
