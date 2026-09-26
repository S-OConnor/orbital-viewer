# _common.sh — shared settings for the container build/run scripts. Sourced,
# not executed:
#   build_builder.sh  1. build the olv-builder image (containers/Dockerfile.builder)
#   build.sh          2. compile backend + simulator inside olv-builder -> build/
#   test.sh           3. run the unit + integration tests inside olv-builder
#   package.sh        4-5. wrap build/dist/* into the backend / simulator images
#   build_all.sh      1-5 in order, stopping at the first failure
#   run_all.sh        6. compose up (simulator only with --sim)
#
# Environment overrides (all optional):
#   OLV_ENGINE          container engine: podman | docker (default: podman if
#                       installed, else docker)
#   OLV_COMPOSE         compose command, e.g. "docker compose"
#   OLV_BUILDER_IMAGE   build environment image  (localhost/olv-builder:latest)
#   OLV_RUNTIME_IMAGE   runtime base image        (rockylinux:10.2-minimal)
#   OLV_BACKEND_IMAGE   backend runtime image     (localhost/olv-backend:latest)
#   OLV_SIM_IMAGE       simulator runtime image   (localhost/olv-sim:latest)

OLV_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# CMake build tree and staged install trees. The repo is always mounted at
# /src inside olv-builder, so these paths are the same on both sides of the
# mount and the CMake cache stays valid between runs.
OLV_BUILD_DIR="build/container"
OLV_DIST_DIR="build/dist"

# Exported so compose.yaml sees the same image names the scripts build.
export OLV_BUILDER_IMAGE="${OLV_BUILDER_IMAGE:-localhost/olv-builder:latest}"
export OLV_RUNTIME_IMAGE="${OLV_RUNTIME_IMAGE:-docker.io/rockylinux/rockylinux:10.2-minimal}"
export OLV_BACKEND_IMAGE="${OLV_BACKEND_IMAGE:-localhost/olv-backend:latest}"
export OLV_SIM_IMAGE="${OLV_SIM_IMAGE:-localhost/olv-sim:latest}"

olv_die() { echo "$(basename "$0"): $*" >&2; exit 1; }

# --- container engine -------------------------------------------------------
if [ -n "${OLV_ENGINE:-}" ]; then
  ENGINE="${OLV_ENGINE}"
elif command -v podman >/dev/null 2>&1; then
  ENGINE=podman
elif command -v docker >/dev/null 2>&1; then
  ENGINE=docker
else
  olv_die "no container engine found (install podman or docker, or set OLV_ENGINE)"
fi
command -v "${ENGINE}" >/dev/null 2>&1 || olv_die "container engine '${ENGINE}' not found"

# True when the engine is podman (including docker -> podman shims).
olv_engine_is_podman() { "${ENGINE}" --version 2>/dev/null | grep -qi podman; }

# Checksums of every staged file under build/dist (the .tested stamp itself
# excluded). scripts/test.sh records this; scripts/package.sh compares it.
olv_dist_checksums() {
  (cd "${OLV_ROOT}/${OLV_DIST_DIR}" && find . -type f ! -name .tested -print0 |
     sort -z | xargs -0 sha256sum)
}

olv_image_exists() { "${ENGINE}" image inspect "$1" >/dev/null 2>&1; }

# Run a command inside olv-builder with the repo bind-mounted at /src.
#  - --network none: the build and tests are fully offline (the integration
#    test only uses loopback).
#  - label=disable: lets the container read the bind mount on SELinux hosts
#    without relabeling the checkout (:Z would rewrite the repo's labels).
#  - rootless podman already maps container root to the invoking user; docker
#    needs --user so build/ isn't filled with root-owned files.
olv_builder_run() {
  local args=(run --rm --network none --security-opt label=disable
              -v "${OLV_ROOT}:/src" -w /src)
  if ! olv_engine_is_podman || [ "$(id -u)" -eq 0 ]; then
    args+=(--user "$(id -u):$(id -g)" -e HOME=/tmp)
  fi
  "${ENGINE}" "${args[@]}" "${OLV_BUILDER_IMAGE}" "$@"
}

# --- compose ----------------------------------------------------------------
# Sets COMPOSE=(...) to a compose command bound to this project, preferring the
# one that matches ENGINE so compose sees the images the scripts built.
olv_find_compose() {
  COMPOSE=()
  if [ -n "${OLV_COMPOSE:-}" ]; then
    # shellcheck disable=SC2206
    COMPOSE=(${OLV_COMPOSE})
  elif [ "${ENGINE}" = podman ] && podman compose version >/dev/null 2>&1; then
    COMPOSE=(podman compose)
  elif [ "${ENGINE}" = podman ] && command -v podman-compose >/dev/null 2>&1; then
    COMPOSE=(podman-compose)
  elif docker compose version >/dev/null 2>&1; then
    COMPOSE=(docker compose)
  elif command -v docker-compose >/dev/null 2>&1; then
    COMPOSE=(docker-compose)
  else
    return 1
  fi
  COMPOSE=("${COMPOSE[@]}" -p olv -f "${OLV_ROOT}/containers/compose.yaml")
}
