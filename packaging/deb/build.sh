#!/usr/bin/env bash
# Build the EdgeLine .deb.
#
# Follows the shape that already works for the sibling project on this machine:
# the Electron app is built unpacked and dropped whole into /opt, the C++ agent
# and CLI go to /usr/bin, and a tiny shim launches the app. electron-builder's
# own deb target is not used, so the payload layout stays under our control.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/dist"
PKG="$OUT/deb"
APP=edgeline

VERSION="$(node -p "require('$ROOT/ui/package.json').version")"
ARCH="$(dpkg --print-architecture)"
DEB="$OUT/${APP}_${VERSION}_${ARCH}.deb"

echo "==> EdgeLine $VERSION ($ARCH)"
rm -rf "$PKG"
mkdir -p "$OUT"

echo "==> agent"
# The version is passed in explicitly rather than left to the CMake project()
# line. EDGELINE_VERSION_STRING is a cache variable, so an existing build
# directory keeps whatever it was first configured with: bumping the project
# version alone shipped a 0.4.1 package whose binaries reported 0.4.0.
cmake -S "$ROOT/agent" -B "$ROOT/agent/build" \
      -DCMAKE_BUILD_TYPE=Release \
      -DEDGELINE_VERSION_STRING="$VERSION" >/dev/null
cmake --build "$ROOT/agent/build" -j"$(nproc)" >/dev/null
# The suites must pass in the same configuration that ships.
( cd "$ROOT/agent/build" && ctest --output-on-failure >/dev/null )

echo "==> ui"
( cd "$ROOT/ui" && npx --no-install electron-builder --linux dir --publish never >/dev/null )
UNPACKED="$ROOT/ui/dist/linux-unpacked"
[ -d "$UNPACKED" ] || { echo "electron-builder produced no linux-unpacked"; exit 1; }

echo "==> tree"
install -d "$PKG/DEBIAN" "$PKG/opt/$APP" "$PKG/usr/bin" \
           "$PKG/usr/share/applications" "$PKG/usr/share/metainfo" \
           "$PKG/usr/lib/udev/rules.d" "$PKG/usr/share/doc/$APP"
cp -a "$UNPACKED/." "$PKG/opt/$APP/"

install -m755 "$ROOT/agent/build/edgeline-agent" "$PKG/usr/bin/edgeline-agent"
install -m755 "$ROOT/agent/build/edgeline"       "$PKG/usr/bin/edgeline"

# The unpacked Electron binary cannot live on $PATH directly: it resolves its
# resources relative to itself.
cat > "$PKG/usr/bin/edgeline-ui" <<'EOF'
#!/bin/sh
exec /opt/edgeline/edgeline-ui "$@"
EOF
chmod 755 "$PKG/usr/bin/edgeline-ui"

install -m644 "$ROOT/udev/60-corsair-xeneon.rules" "$PKG/usr/lib/udev/rules.d/"
install -m644 "$ROOT/packaging/dev.edgeline.Ctl.desktop" "$PKG/usr/share/applications/"
install -m644 "$ROOT/packaging/dev.edgeline.Ctl.metainfo.xml" "$PKG/usr/share/metainfo/"
install -m644 "$ROOT/LICENSE" "$PKG/usr/share/doc/$APP/copyright"

for s in 16 24 32 48 64 128 256 512; do
  install -d "$PKG/usr/share/icons/hicolor/${s}x${s}/apps"
  install -m644 "$ROOT/packaging/icons/edgeline-$s.png" \
                "$PKG/usr/share/icons/hicolor/${s}x${s}/apps/dev.edgeline.Ctl.png"
done
install -d "$PKG/usr/share/icons/hicolor/scalable/apps"
install -m644 "$ROOT/packaging/icons/edgeline.svg" \
              "$PKG/usr/share/icons/hicolor/scalable/apps/dev.edgeline.Ctl.svg"

SIZE="$(du -sk "$PKG" | cut -f1)"
cat > "$PKG/DEBIAN/control" <<EOF
Package: $APP
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Depends: libc6, libstdc++6, libqt6core6, libqt6network6, libhidapi-hidraw0,
 libx11-6, libxi6, ddcutil, libgtk-3-0, libnotify4, libnss3, libxss1, libxtst6,
 xdg-utils, libatspi2.0-0, libuuid1, libsecret-1-0, libgbm1,
 libasound2 | libasound2t64, udev
Recommends: x11-xserver-utils, colord
Installed-Size: $SIZE
Maintainer: Ahmed Abdelghany <ahmedabdelghany15@gmail.com>
Homepage: https://github.com/aabdelghani/corsair-xeneon-edge-linux
Description: Native Linux control for the Corsair Xeneon Edge
 Picture control over DDC/CI, four touch modes with five point calibration,
 a dashboard on the panel, profiles and per application rules.
 .
 No kernel module and no root daemon: DDC goes through ddcutil, touch through
 xinput, and the agent runs as your own user.
EOF

cat > "$PKG/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
# Electron's sandbox helper must be setuid root or Chromium refuses to start
# with a namespace error that says nothing useful.
chmod 4755 /opt/edgeline/chrome-sandbox 2>/dev/null || true
udevadm control --reload-rules 2>/dev/null || true
udevadm trigger --subsystem-match=hidraw --action=add 2>/dev/null || true
update-desktop-database -q /usr/share/applications 2>/dev/null || true
gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor 2>/dev/null || true
# Picture control needs i2c access, which is group membership, not a udev rule.
# The package cannot add a user to a group on their behalf.
if ! id -nG "${SUDO_USER:-$USER}" 2>/dev/null | grep -qw i2c; then
  echo ""
  echo "EdgeLine: to control the panel's picture, add yourself to the i2c group:"
  echo "    sudo usermod -aG i2c ${SUDO_USER:-$USER}"
  echo "  then log out and back in."
  echo ""
fi
exit 0
EOF

cat > "$PKG/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
update-desktop-database -q /usr/share/applications 2>/dev/null || true
gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor 2>/dev/null || true
exit 0
EOF
chmod 755 "$PKG/DEBIAN/postinst" "$PKG/DEBIAN/postrm"

echo "==> pack"
dpkg-deb --build --root-owner-group "$PKG" "$DEB" >/dev/null
rm -rf "$PKG"
echo "==> $DEB"
