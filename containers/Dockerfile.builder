# Dockerfile.builder — the Orbital LOS Viewer C++ build environment.
#
# Rocky Linux 10.2 with exactly what the backend and simulator need to
# compile, and nothing else:
#   gcc-c++, cmake, make   toolchain
#   boost-devel            Boost >= 1.74 headers (Asio/Beast) + BoostConfig.cmake
#   gtest-devel,           GoogleTest + GoogleMock (EPEL) for the unit tests
#   gmock-devel
#   open-dis-cpp v1.2.0    static, built below with upstream's own CMake
#                          project and installed to /usr/local with its
#                          package config, so the project's
#                          find_package(OpenDIS CONFIG) finds it with no hints
# The resulting binaries need only glibc + libstdc++ at runtime (open-dis is
# linked statically), which is what the runtime images
# (containers/Dockerfile.backend, containers/Dockerfile.simulator) ship.
#
# This image is the first step of the build pipeline (scripts/build_all.sh):
#   1. scripts/build_builder.sh   build this image
#   2. scripts/build.sh           docker/podman run it with the repo mounted at
#                                 /src: compile into build/container, stage
#                                 runtime files into build/dist/
#   3. scripts/test.sh            run ctest in it against build/container
#   4-5. scripts/package.sh       wrap build/dist/* into the runtime images
#
# It is meant to be built ONCE on a connected machine and pushed to an
# internal registry (GitLab container registry, Harbor, ...); steps 2-5 then
# run fully offline (build and tests use --network none):
#
#   # connected machine
#   OLV_BUILDER_IMAGE=harbor.example.com/olv/olv-builder:1.0.0 scripts/build_builder.sh
#   podman push harbor.example.com/olv/olv-builder:1.0.0
#
#   # offline machine
#   export OLV_BUILDER_IMAGE=harbor.example.com/olv/olv-builder:1.0.0
#   export OLV_RUNTIME_IMAGE=harbor.example.com/olv/rockylinux:10.2-minimal
#   scripts/build_all.sh        # skips step 1 because OLV_BUILDER_IMAGE is set
#
# Building this image needs network access twice: dnf (Rocky BaseOS/AppStream
# and EPEL) and the pinned open-dis-cpp release tarball from GitHub. On a restricted
# network, point dnf at a mirror (e.g. a derived BASE_IMAGE with mirror .repo
# files) and pass --build-arg OLV_OPEN_DIS_URL=<internal mirror of the
# tarball>; the pinned sha256 is still enforced. podman build / buildah
# accept the same flags as docker build.

ARG BASE_IMAGE=docker.io/rockylinux/rockylinux:10.2

FROM ${BASE_IMAGE}

# install_weak_deps=False keeps recommended-but-unneeded packages out.
# gtest-devel/gmock-devel are only packaged in EPEL on Rocky 10, hence
# epel-release first.
RUN dnf -y install --setopt=install_weak_deps=False --nodocs epel-release && \
    dnf -y install --setopt=install_weak_deps=False --nodocs \
        gcc-c++ \
        cmake \
        make \
        boost-devel \
        gtest-devel \
        gmock-devel && \
    dnf clean all && \
    rm -rf /var/cache/dnf

# open-dis-cpp (third-party, BSD-2-Clause, pinned v1.2.0) — not vendored. The
# release tarball is fetched (OLV_OPEN_DIS_URL overrides the GitHub URL, e.g.
# an internal mirror) and checked against the pinned sha256 before anything
# is compiled, then built verbatim (no local patches) with upstream's own
# CMake project: static (runtime images need no extra .so), Release, warnings
# silenced (auto-generated upstream code). DIS7 is built too although only
# DIS6 is used: upstream's OpenDISConfig.cmake unconditionally aliases
# OpenDIS::OpenDIS7 and fails find_package() if it is missing. Installs into
# /usr/local:
#   include/dis6/, include/dis7/     headers
#   lib64/libOpenDIS6.a, libOpenDIS7.a
#   lib64/cmake/OpenDIS/             CMake package config
#   share/doc/open-dis-cpp/          LICENSE (BSD-2-Clause) + PROVENANCE
# A version bump changes OPEN_DIS_VERSION and OPEN_DIS_SHA256 together (and
# the open-dis-cpp component in sbom/backend.cdx.json + THIRD_PARTY.md).
ARG OPEN_DIS_VERSION=1.2.0
ARG OPEN_DIS_SHA256=aa1b9b5e5f00e5b8819c111f0a5a0e56266c7ccc0ec9b9f5b9d33f7721438216
ARG OLV_OPEN_DIS_URL=
RUN set -eu; \
    url="${OLV_OPEN_DIS_URL:-https://github.com/open-dis/open-dis-cpp/archive/refs/tags/v${OPEN_DIS_VERSION}.tar.gz}"; \
    work="$(mktemp -d)"; \
    src="${work}/open-dis-cpp-${OPEN_DIS_VERSION}"; \
    doc=/usr/local/share/doc/open-dis-cpp; \
    curl -fsSL -o "${work}/open-dis-cpp.tar.gz" "${url}"; \
    echo "${OPEN_DIS_SHA256}  ${work}/open-dis-cpp.tar.gz" | sha256sum -c -; \
    tar xzf "${work}/open-dis-cpp.tar.gz" -C "${work}"; \
    cmake -S "${src}" -B "${work}/build" -Wno-dev -Wno-deprecated \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCMAKE_CXX_FLAGS=-w \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_TESTS=OFF; \
    cmake --build "${work}/build" -j "$(nproc)"; \
    cmake --install "${work}/build"; \
    install -d "${doc}"; \
    install -m 644 "${src}/LICENSE" "${doc}/LICENSE"; \
    printf '%s\n' \
        "open-dis-cpp v${OPEN_DIS_VERSION} (https://github.com/open-dis/open-dis-cpp)" \
        "License: BSD-2-Clause (LICENSE alongside this file)" \
        "Built and installed by containers/Dockerfile.builder with upstream's CMake" \
        "project: static, Release, no local modifications." \
        "Tarball sha256: ${OPEN_DIS_SHA256}" > "${doc}/PROVENANCE"; \
    rm -rf "${work}"

LABEL org.opencontainers.image.title="olv-builder" \
      org.opencontainers.image.description="Orbital LOS Viewer C++ build environment: Rocky Linux 10.2, GCC, CMake, Boost, GoogleTest/GoogleMock, open-dis-cpp 1.2.0"

WORKDIR /src
CMD ["/bin/bash"]
