# Dockerfile — multi-stage build for olv_backend and olv_sim on Rocky Linux 10.2.
#
# The compile stage starts FROM the prebuilt build-environment image
# (containers/Dockerfile.builder: toolchain + Boost + open-dis-cpp), so this
# file installs nothing and fetches nothing — it builds offline as long as the
# two base images below are available locally or from your registry.
#
# Build (repo root is the build context; the default target is `backend`):
#   docker build -f containers/Dockerfile.builder -t localhost/olv-builder:latest .
#   docker build -f containers/Dockerfile --target backend   -t olv-backend .
#   docker build -f containers/Dockerfile --target simulator -t olv-sim .
# (podman build / buildah accept the same flags.)
#
# Offline / internal registry (GitLab, Harbor): push olv-builder and a copy of
# the runtime base image once, then point the build at them:
#   --build-arg OLV_BUILDER_IMAGE=harbor.example.com/olv/olv-builder:1.0.0
#   --build-arg OLV_RUNTIME_IMAGE=harbor.example.com/olv/rockylinux:10.2-minimal
# and push the results as usual:
#   docker tag  olv-backend harbor.example.com/olv/olv-backend:1.0.0
#   docker push harbor.example.com/olv/olv-backend:1.0.0
#
# Run:
#   docker run --rm -p 8765:8765 -p 47000:47000/udp olv-backend
#   docker run --rm --network host olv-sim --dest 127.0.0.1 --port 47000
#
# Runtime images are rocky 10.2-minimal: the binaries need only glibc and
# libstdc++ (Boost is header-only here, open-dis is linked statically), both
# already in the minimal image, so no packages are installed at runtime and
# no component reaches the network at runtime.

ARG OLV_BUILDER_IMAGE=localhost/olv-builder:latest
ARG OLV_RUNTIME_IMAGE=docker.io/rockylinux/rockylinux:10.2-minimal

# ---------------------------------------------------------------------------
# Stage: build — compiles olv_backend and olv_sim with CMake.
# ---------------------------------------------------------------------------
FROM ${OLV_BUILDER_IMAGE} AS build

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOLV_BUILD_TESTS=OFF && \
    cmake --build build -j "$(nproc)" --target olv_backend olv_sim

# ---------------------------------------------------------------------------
# Target: simulator — minimal runtime image running as a non-root user.
# Includes tools/simulator/data so --csv replay works without a bind mount.
# ---------------------------------------------------------------------------
FROM ${OLV_RUNTIME_IMAGE} AS simulator

RUN groupadd --gid 10001 olv && \
    useradd --uid 10001 --gid olv --no-create-home --shell /usr/sbin/nologin olv

COPY --from=build /src/build/tools/simulator/olv_sim /app/olv_sim
COPY tools/simulator/data /app/data

USER 10001
WORKDIR /app

ENTRYPOINT ["/app/olv_sim"]
CMD ["--generate", "1000", "--dest", "backend", "--port", "47000", "--rate", "1", "--duration", "0"]

# ---------------------------------------------------------------------------
# Target: backend — minimal runtime image running as a non-root user. Last
# stage, so it is also what a plain `docker build` without --target produces.
# ---------------------------------------------------------------------------
FROM ${OLV_RUNTIME_IMAGE} AS backend

RUN groupadd --gid 10001 olv && \
    useradd --uid 10001 --gid olv --no-create-home --shell /usr/sbin/nologin olv

COPY --from=build /src/build/olv_backend /app/olv_backend

USER 10001
WORKDIR /app

EXPOSE 8765
EXPOSE 47000/udp

ENTRYPOINT ["/app/olv_backend"]
CMD ["--udp-port", "47000", "--ws-port", "8765", "--log-file", "/tmp/olv_backend.log"]
