#!/usr/bin/env bash
# build_all.sh — steps 1-5 in order, stopping at the first failure:
#   build_builder.sh -> build.sh -> test.sh -> package.sh
#
# Usage: scripts/build_all.sh [--clean] [--unit]
#   --clean   passed to build.sh (fresh CMake tree)
#   --unit    passed to test.sh (skip the integration test)
#
# The builder image is only (re)built when OLV_BUILDER_IMAGE is the local
# default; set it to a registry image to use a prebuilt build environment.

set -euo pipefail
SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_ARGS=()
TEST_ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --clean) BUILD_ARGS+=(--clean); shift ;;
    --unit)  TEST_ARGS+=(--unit);   shift ;;
    *) echo "build_all.sh: unknown argument: $1" >&2; exit 1 ;;
  esac
done

if [ -z "${OLV_BUILDER_IMAGE:-}" ]; then
  "${SCRIPTS}/build_builder.sh"
fi
"${SCRIPTS}/build.sh" "${BUILD_ARGS[@]}"
"${SCRIPTS}/test.sh" "${TEST_ARGS[@]}"
"${SCRIPTS}/package.sh"
