#!/usr/bin/env bash
# install_open_dis.sh — build the third-party open-dis-cpp DIS library (pinned
# upstream release v1.2.0, BSD-2-Clause) with upstream's own CMake project and
# install it into a prefix, instead of vendoring its sources in this repository.
#
#   scripts/install_open_dis.sh [--prefix DIR] [--src DIR]
#
# Installs (upstream `cmake --install`, static libraries):
#   $PREFIX/include/dis6/, include/dis7/     headers
#   $PREFIX/lib*/libOpenDIS6.a, libOpenDIS7.a
#   $PREFIX/lib*/cmake/OpenDIS/              CMake package config
#   $PREFIX/share/doc/open-dis-cpp/          LICENSE (BSD-2-Clause) + PROVENANCE
#
# The project consumes it with `find_package(OpenDIS CONFIG)` and links
# OpenDIS::OpenDIS6. /usr/local is found automatically, as is ~/.local when
# ~/.local/bin is on PATH; any other prefix needs -DCMAKE_PREFIX_PATH=DIR.
# containers/Dockerfile.builder runs this script to bake the library into the
# build-environment image.
#
# Source acquisition, in order of preference:
#   --src DIR                    an already-unpacked open-dis-cpp source tree
#   $OLV_OPEN_DIS_TARBALL        path to a pre-fetched release tarball (air gap:
#                                mirror the tarball like the OS packages)
#   $OLV_OPEN_DIS_URL            alternate download URL (e.g. an internal
#                                mirror); the pinned sha256 is still enforced
#   (otherwise)                  download the pinned tag from GitHub
# The downloaded/pre-fetched tarball is checked against the pinned sha256
# before anything is compiled. Upstream sources are compiled verbatim — no
# local patches; any fix goes upstream or waits for a new pinned tag.

set -euo pipefail

VERSION="1.2.0"
TARBALL_URL="https://github.com/open-dis/open-dis-cpp/archive/refs/tags/v${VERSION}.tar.gz"
TARBALL_SHA256="aa1b9b5e5f00e5b8819c111f0a5a0e56266c7ccc0ec9b9f5b9d33f7721438216"

PREFIX="/usr/local"
SRC_DIR=""

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2 ;;
    --src)    SRC_DIR="$2"; shift 2 ;;
    -h|--help) sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "install_open_dis.sh: unknown argument: $1" >&2; exit 1 ;;
  esac
done

command -v cmake >/dev/null || { echo "install_open_dis.sh: cmake not found" >&2; exit 1; }
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

# ---- Obtain the pinned source tree ----------------------------------------
if [ -z "${SRC_DIR}" ]; then
  TARBALL="${OLV_OPEN_DIS_TARBALL:-}"
  if [ -z "${TARBALL}" ]; then
    TARBALL="${WORK}/open-dis-cpp-v${VERSION}.tar.gz"
    URL="${OLV_OPEN_DIS_URL:-${TARBALL_URL}}"
    echo "fetching ${URL}"
    curl -fsSL -o "${TARBALL}" "${URL}"
  fi
  echo "${TARBALL_SHA256}  ${TARBALL}" | sha256sum -c - >/dev/null
  tar xzf "${TARBALL}" -C "${WORK}"
  SRC_DIR="${WORK}/open-dis-cpp-${VERSION}"
fi

[ -f "${SRC_DIR}/CMakeLists.txt" ] || { echo "install_open_dis.sh: ${SRC_DIR}/CMakeLists.txt not found" >&2; exit 1; }

# ---- Build + install with upstream's own CMake project -----------------------
# Static (so runtime images need no extra .so), Release, warnings silenced
# (auto-generated upstream code). DIS7 is built too although we only use DIS6:
# upstream's OpenDISConfig.cmake unconditionally aliases OpenDIS::OpenDIS7 and
# fails find_package() if it is missing.
echo "building open-dis-cpp v${VERSION} (static, Release)"
cmake -S "${SRC_DIR}" -B "${WORK}/build" -Wno-dev -Wno-deprecated \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
  -DCMAKE_CXX_FLAGS=-w \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_TESTS=OFF >/dev/null
cmake --build "${WORK}/build" -j "$(nproc 2>/dev/null || echo 4)" >/dev/null

echo "installing to ${PREFIX}"
cmake --install "${WORK}/build" >/dev/null
install -d "${PREFIX}/share/doc/open-dis-cpp"
install -m 644 "${SRC_DIR}/LICENSE" "${PREFIX}/share/doc/open-dis-cpp/LICENSE"
cat > "${PREFIX}/share/doc/open-dis-cpp/PROVENANCE" <<EOF2
open-dis-cpp v${VERSION} (https://github.com/open-dis/open-dis-cpp)
License: BSD-2-Clause (LICENSE alongside this file)
Built and installed by scripts/install_open_dis.sh with upstream's CMake
project: static, Release, no local modifications.
Tarball sha256: ${TARBALL_SHA256}
EOF2
echo "done: find_package(OpenDIS CONFIG) -> OpenDIS::OpenDIS6 under ${PREFIX}"
