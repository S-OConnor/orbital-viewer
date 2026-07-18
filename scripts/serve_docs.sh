#!/usr/bin/env bash
# Serve the static documentation site (docs/site/) over HTTP and open it in
# the default browser.
#
# Usage: scripts/serve_docs.sh [PORT]        (default port: 8090)
#
# Python 3 (stdlib) is the only requirement. Ctrl-C stops the server.
set -euo pipefail

PORT="${1:-8090}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SITE="$ROOT/docs/site"

if [ ! -f "$SITE/index.html" ]; then
  echo "error: $SITE/index.html not found" >&2
  exit 1
fi

# Refresh the search index if the generator is available (best-effort).
if command -v python3 >/dev/null 2>&1 && [ -f "$SITE/tools/build_search_index.py" ]; then
  python3 "$SITE/tools/build_search_index.py" || true
fi

URL="http://localhost:${PORT}/index.html"
echo "[serve_docs] serving $SITE at $URL (Ctrl-C to stop)"

# Launch the browser once the server is listening.
(
  for _ in $(seq 1 50); do
    if (exec 3<>"/dev/tcp/127.0.0.1/${PORT}") 2>/dev/null; then
      exec 3>&- 3<&-
      break
    fi
    sleep 0.1
  done
  if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$URL" >/dev/null 2>&1 || true
  elif command -v open >/dev/null 2>&1; then
    open "$URL" >/dev/null 2>&1 || true
  else
    echo "[serve_docs] open $URL manually (no xdg-open/open found)"
  fi
) &

exec python3 -m http.server "$PORT" --directory "$SITE" --bind 0.0.0.0
