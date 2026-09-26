# Dockerfile.builder — the Orbital LOS Viewer C++ build environment.
#
# Rocky Linux 10.2 with exactly what the backend and simulator need to
# compile, and nothing else:
#   gcc-c++, cmake, make   toolchain
#   boost-devel            Boost >= 1.74 headers (Asio/Beast) + BoostConfig.cmake
#   gtest-devel,           GoogleTest + GoogleMock (EPEL) for the unit tests
#   gmock-devel
#   open-dis-cpp v1.2.0    static, installed to /usr/local by
#                          scripts/install_open_dis.sh with upstream's own
#                          CMake package config, so the project's
#                          find_package(OpenDIS CONFIG) finds it with no hints
# The resulting binaries need only glibc + libstdc++ at runtime (open-dis is
# linked statically), which is what containers/Dockerfile's runtime stages
# ship.
#
# The image is meant to be built ONCE on a connected machine, pushed to an
# internal registry (GitLab container registry, Harbor, ...), and then used to
# build the project fully offline:
#
#   # connected machine (repo root is the build context)
#   docker build -f containers/Dockerfile.builder \
#       -t harbor.example.com/olv/olv-builder:1.0.0 .
#   docker push harbor.example.com/olv/olv-builder:1.0.0
#
#   # offline machine: container images (backend/simulator targets)
#   docker build -f containers/Dockerfile --target backend \
#       --build-arg OLV_BUILDER_IMAGE=harbor.example.com/olv/olv-builder:1.0.0 \
#       --build-arg OLV_RUNTIME_IMAGE=harbor.example.com/olv/rockylinux:10.2-minimal \
#       -t harbor.example.com/olv/olv-backend:1.0.0 .
#
#   # offline machine: plain host binaries from a bind-mounted checkout
#   docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/src -w /src \
#       harbor.example.com/olv/olv-builder:1.0.0 \
#       sh -c 'cmake -S . -B build-rocky && cmake --build build-rocky -j"$(nproc)"'
#   (podman: drop --user, rootless podman already maps root to you; add :Z to
#   the volume on SELinux hosts.)
#
# Building this image needs network access twice: dnf (Rocky BaseOS/AppStream
# and EPEL) and the pinned open-dis-cpp release tarball from GitHub. On a restricted
# network, point dnf at a mirror (e.g. a derived BASE_IMAGE with mirror .repo
# files) and pass --build-arg OLV_OPEN_DIS_URL=<internal mirror of the
# tarball>; its sha256 is still enforced by install_open_dis.sh. podman
# build / buildah accept the same flags as docker build.

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

# open-dis-cpp (third-party, BSD-2-Clause, pinned v1.2.0): fetched,
# sha256-verified, built with upstream's CMake and installed into /usr/local
# (headers, libOpenDIS6.a/libOpenDIS7.a, lib64/cmake/OpenDIS, LICENSE).
ARG OLV_OPEN_DIS_URL=
COPY scripts/install_open_dis.sh /tmp/install_open_dis.sh
RUN OLV_OPEN_DIS_URL="${OLV_OPEN_DIS_URL}" /tmp/install_open_dis.sh --prefix /usr/local && \
    rm /tmp/install_open_dis.sh

LABEL org.opencontainers.image.title="olv-builder" \
      org.opencontainers.image.description="Orbital LOS Viewer C++ build environment: Rocky Linux 10.2, GCC, CMake, Boost, GoogleTest/GoogleMock, open-dis-cpp 1.2.0"

WORKDIR /src
CMD ["/bin/bash"]
