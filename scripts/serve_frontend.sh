#!/usr/bin/env bash
# serve_frontend.sh — serve frontend/ as static files for local development.
#
# The frontend is plain HTML/CSS/JS with ES modules, which browsers refuse to
# load over file://, so it must be served over http:// even for local use.
# For containerized/production serving see containers/Containerfile.frontend.
#
# Usage: scripts/serve_frontend.sh [PORT]   (default: 8000)

set -euo pipefail

PORT="${1:-8000}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Serve the repo root (not frontend/) so the About modal's relative links to
# ../docs/PROTOCOL_*.md resolve; the app itself lives under /frontend/.
echo "Serving ${ROOT} at http://localhost:${PORT}/frontend/  (Ctrl-C to stop)"
exec python3 -m http.server "${PORT}" --directory "${ROOT}" --bind 0.0.0.0
