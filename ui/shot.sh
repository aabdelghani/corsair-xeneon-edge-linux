#!/usr/bin/env bash
# Render the UI on a private X display and capture it.
#
# Never uses :0. The app is a desktop window that takes focus, and stealing
# focus from whatever the user is doing is not an acceptable way to check a
# stylesheet.
#
# usage: ./shot.sh <out.png> [tab] [theme] [waitSeconds]
set -euo pipefail
cd "$(dirname "$0")"

OUT="${1:?usage: shot.sh out.png [tab] [theme] [wait]}"
TAB="${2:-picture}"
THEME="${3:-ubuntu-dark}"
WAIT="${4:-4}"
HEIGHT="${5:-760}"
DISP=":99"

export NVM_DIR="$HOME/.nvm"; . "$NVM_DIR/nvm.sh" >/dev/null; nvm use 20 >/dev/null

DISPLAY=$DISP ./node_modules/.bin/electron . \
  --class=edgeline --edgeline-tab="$TAB" --edgeline-theme="$THEME" --edgeline-height="$HEIGHT" >/dev/null 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true; wait $PID 2>/dev/null || true' EXIT

for _ in $(seq 1 100); do
  if DISPLAY=$DISP xdotool search --name '^EdgeLine$' >/dev/null 2>&1; then break; fi
  sleep 0.2
done
sleep "$WAIT"

W=$(DISPLAY=$DISP xdotool search --name '^EdgeLine$' | head -1)
DISPLAY=$DISP import -window "$W" "$OUT"
echo "captured $OUT (tab=$TAB theme=$THEME)"
