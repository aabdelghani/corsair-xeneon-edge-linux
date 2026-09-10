#!/usr/bin/env bash
# Register a checkout build with the desktop, so the taskbar and tray show the
# real name and icon instead of a generic fallback.
#
# The .deb does this system-wide. This is for running from a source tree.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
APPS="$HOME/.local/share/applications"
ICONS="$HOME/.local/share/icons/hicolor"

install -d "$APPS"
for s in 16 24 32 48 64 128 256 512; do
  install -d "$ICONS/${s}x${s}/apps"
  install -m644 "$ROOT/packaging/icons/edgeline-$s.png" \
                "$ICONS/${s}x${s}/apps/dev.edgeline.Ctl.png"
done
install -d "$ICONS/scalable/apps"
install -m644 "$ROOT/packaging/icons/edgeline.svg" \
              "$ICONS/scalable/apps/dev.edgeline.Ctl.svg"

# Exec points into the checkout rather than /usr/bin.
sed -e "s#^Exec=.*#Exec=$ROOT/ui/node_modules/.bin/electron $ROOT/ui --class=edgeline#" \
    "$ROOT/packaging/dev.edgeline.Ctl.desktop" > "$APPS/dev.edgeline.Ctl.desktop"
chmod 644 "$APPS/dev.edgeline.Ctl.desktop"

update-desktop-database -q "$APPS" 2>/dev/null || true
gtk-update-icon-cache -q -t -f "$ICONS" 2>/dev/null || true
echo "Registered EdgeLine from $ROOT"
echo "  desktop entry: $APPS/dev.edgeline.Ctl.desktop"
echo "  icons:         $ICONS/*/apps/dev.edgeline.Ctl.png"
