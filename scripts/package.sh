#!/usr/bin/env bash
# package.sh — steps 4-5: build the backend and simulator runtime images from
# the artifacts staged by scripts/build.sh. Nothing is compiled here.
#
# Usage: scripts/package.sh [--untested] [backend|simulator ...]
#   --untested   package even if build/dist doesn't match the last passing
#                scripts/test.sh run
#   (no names)   build both images
#
# Each image's build context is its own build/dist/<component>/ directory, so
# only the files that belong in that image are sent to the engine. Tags:
# $OLV_BACKEND_IMAGE, $OLV_SIM_IMAGE (default localhost/olv-backend:latest,
# localhost/olv-sim:latest), labelled with the git revision.

set -euo pipefail
# shellcheck source=scripts/_common.sh
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

REQUIRE_TESTED=1
TARGETS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --untested)          REQUIRE_TESTED=0; shift ;;
    backend|simulator)   TARGETS+=("$1"); shift ;;
    *) olv_die "unknown argument: $1" ;;
  esac
done
[ ${#TARGETS[@]} -gt 0 ] || TARGETS=(backend simulator)

DIST="${OLV_ROOT}/${OLV_DIST_DIR}"
[ -d "${DIST}" ] || olv_die "no ${OLV_DIST_DIR}; run scripts/build.sh first"

if [ "${REQUIRE_TESTED}" -eq 1 ]; then
  [ -f "${DIST}/.tested" ] ||
    olv_die "${OLV_DIST_DIR} has not passed scripts/test.sh (use --untested to override)"
  olv_dist_checksums | cmp -s - "${DIST}/.tested" ||
    olv_die "${OLV_DIST_DIR} changed since the last passing scripts/test.sh (use --untested to override)"
fi

REVISION="$(git -C "${OLV_ROOT}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [ "${REVISION}" != unknown ] && [ -n "$(git -C "${OLV_ROOT}" status --porcelain 2>/dev/null)" ]; then
  REVISION="${REVISION}-dirty"
fi

for target in "${TARGETS[@]}"; do
  case "${target}" in
    backend)   image="${OLV_BACKEND_IMAGE}"; file=Dockerfile.backend ;;
    simulator) image="${OLV_SIM_IMAGE}";     file=Dockerfile.simulator ;;
  esac
  echo "== packaging ${image} (containers/${file}, ${OLV_DIST_DIR}/${target}) =="
  "${ENGINE}" build -f "${OLV_ROOT}/containers/${file}" \
    --build-arg OLV_BUILDER_IMAGE="${OLV_BUILDER_IMAGE}" \
    --build-arg OLV_RUNTIME_IMAGE="${OLV_RUNTIME_IMAGE}" \
    --label org.opencontainers.image.revision="${REVISION}" \
    -t "${image}" "${DIST}/${target}"
done
