#!/usr/bin/env bash
# test.sh — step 3: run the tests inside the olv-builder image against the
# build tree from scripts/build.sh (nothing is rebuilt).
#
# Usage: scripts/test.sh [--unit] [-- <extra ctest args>]
#   --unit   skip the end-to-end integration test (ctest -LE integration)
#
# On success, records checksums of the staged build/dist files so that
# scripts/package.sh can refuse to package binaries that were rebuilt after
# the last passing test run.

set -euo pipefail
# shellcheck source=scripts/_common.sh
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

CTEST_ARGS=(--output-on-failure)
while [ $# -gt 0 ]; do
  case "$1" in
    --unit) CTEST_ARGS+=(-LE integration); shift ;;
    --)     shift; CTEST_ARGS+=("$@"); break ;;
    *) olv_die "unknown argument: $1" ;;
  esac
done

[ -f "${OLV_ROOT}/${OLV_BUILD_DIR}/CTestTestfile.cmake" ] ||
  olv_die "no build tree at ${OLV_BUILD_DIR}; run scripts/build.sh first"

echo "== testing in ${OLV_BUILDER_IMAGE}: ctest ${CTEST_ARGS[*]} =="
olv_builder_run ctest --test-dir "${OLV_BUILD_DIR}" "${CTEST_ARGS[@]}"

# Stamp what was tested.
olv_dist_checksums > "${OLV_ROOT}/${OLV_DIST_DIR}/.tested"
echo "== tests passed; stamped ${OLV_DIST_DIR}/.tested =="
