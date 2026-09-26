#!/usr/bin/env bash
# build_builder.sh — step 1: build the olv-builder image (toolchain, Boost,
# GoogleTest, open-dis-cpp) from containers/Dockerfile.builder.
#
# Usage: scripts/build_builder.sh [extra build args, e.g. --build-arg X=Y]
#
# Tags the result as $OLV_BUILDER_IMAGE (default localhost/olv-builder:latest).
# Needs network access (dnf + the pinned open-dis-cpp tarball); everything
# after this step runs offline. See containers/Dockerfile.builder for mirror
# and registry options.

set -euo pipefail
# shellcheck source=scripts/_common.sh
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

echo "== building ${OLV_BUILDER_IMAGE} (containers/Dockerfile.builder) =="
# The builder COPYs nothing, so containers/ is a small, sufficient context.
"${ENGINE}" build -f "${OLV_ROOT}/containers/Dockerfile.builder" \
  -t "${OLV_BUILDER_IMAGE}" "$@" "${OLV_ROOT}/containers"
