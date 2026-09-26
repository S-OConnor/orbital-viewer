#!/usr/bin/env bash
# build.sh — step 2: compile the backend, simulator and tests inside the
# olv-builder image, then stage the runtime files for packaging.
#
# Usage: scripts/build.sh [--clean] [--debug]
#   --clean   delete build/container and build/dist first
#   --debug   CMAKE_BUILD_TYPE=Debug (default Release)
#
# Output (on the host, via the bind mount):
#   build/container/           CMake build tree (incremental across runs)
#   build/dist/backend/        cmake --install --component backend
#   build/dist/simulator/      cmake --install --component simulator
# The dist trees are the build contexts for scripts/package.sh.

set -euo pipefail
# shellcheck source=scripts/_common.sh
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

BUILD_TYPE=Release
while [ $# -gt 0 ]; do
  case "$1" in
    --clean) rm -rf "${OLV_ROOT:?}/${OLV_BUILD_DIR}" "${OLV_ROOT:?}/${OLV_DIST_DIR}"; shift ;;
    --debug) BUILD_TYPE=Debug; shift ;;
    *) olv_die "unknown argument: $1" ;;
  esac
done

olv_image_exists "${OLV_BUILDER_IMAGE}" || [[ "${OLV_BUILDER_IMAGE}" != localhost/* ]] ||
  olv_die "${OLV_BUILDER_IMAGE} not found; run scripts/build_builder.sh first"

echo "== building in ${OLV_BUILDER_IMAGE} -> ${OLV_BUILD_DIR} (${BUILD_TYPE}) =="
olv_builder_run bash -euc '
  cmake -S . -B "$1" -DCMAKE_BUILD_TYPE="$3" -DOLV_BUILD_TESTS=ON
  cmake --build "$1" -j "$(nproc)"
  rm -rf "$2"
  for c in backend simulator; do
    cmake --install "$1" --component "$c" --prefix "$2/$c"
  done
' build "${OLV_BUILD_DIR}" "${OLV_DIST_DIR}" "${BUILD_TYPE}"

echo "== staged artifacts =="
(cd "${OLV_ROOT}" && find "${OLV_DIST_DIR}" -type f | sort)
