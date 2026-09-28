#!/usr/bin/env bash
# Regenerate the README's dashboard images from a real `make dashboard` run,
# with headless Chrome and the dashboard's deep links (#index=...&frame=...).
#
#   tools/screenshots.sh            # needs dashboard.html; run `make dashboard` first
#   CHROME=/path/to/chrome tools/screenshots.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CHROME="${CHROME:-}"
for c in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
         /usr/bin/google-chrome /usr/bin/chromium /usr/bin/chromium-browser; do
  [ -z "$CHROME" ] && [ -x "$c" ] && CHROME="$c"
done
[ -n "$CHROME" ] || { echo "no Chrome/Chromium found; set CHROME=..."; exit 1; }
[ -f dashboard.html ] || { echo "dashboard.html missing; run: make dashboard"; exit 1; }

mkdir -p docs/images
page="file://$PWD/dashboard.html"
shot() {  # name, hash
  "$CHROME" --headless --disable-gpu --hide-scrollbars --window-size=1600,1000 \
    --virtual-time-budget=4000 --screenshot="$PWD/docs/images/$1.png" "$page#$2" >/dev/null 2>&1
  echo "  docs/images/$1.png"
}
shot dashboard-main     "frame=420&pause"
shot dashboard-quadtree "index=quadtree&frame=420&pause"
shot dashboard-rtree    "index=rtree&frame=420&pause"
