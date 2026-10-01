#!/usr/bin/env bash
# Build the EdgeLine AppImage so it runs beyond the machine that built it.
#
# electron-builder's AppImage carried the agent as this machine built it:
# linked against the system's Qt 6, ICU 74 and glibc 2.39, so on any distro
# without those exact libraries the window opened and the agent never ran.
# The AppImage catalog's test also noted an old runtime that needs libfuse2
# and no update information (appimage.github.io PR #6774).
#
# So the agent is built here on Ubuntu 22.04 (glibc 2.35, Qt 6.2) in a
# container, its libraries and Qt's TLS plugin are bundled beside it with an
# $ORIGIN rpath, and the AppDir is packed again by appimagetool with the
# static type 2 runtime (no libfuse2) and GitHub update information, which
# also writes the .zsync file AppImageUpdate reads. Libraries every Linux
# desktop has, and must keep its own copy of, are left to the system: glibc,
# libstdc++, libgcc, X11, libudev and libdbus.
#
#   packaging/appimage/build.sh          -> dist/EdgeLine-<v>-x86_64.AppImage(.zsync)
#
# Needs docker, node 20 (nvm use 20) and network for the image and the tool.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/dist"
WORK="$ROOT/build-appimage"
VERSION="$(node -p "require('$ROOT/ui/package.json').version")"
NAME="EdgeLine-${VERSION}-x86_64.AppImage"
UPDATE="gh-releases-zsync|aabdelghani|corsair-xeneon-edge-linux|latest|EdgeLine-*-x86_64.AppImage.zsync"
IMAGE=ubuntu:22.04

echo "==> EdgeLine $VERSION AppImage"
rm -rf "$WORK"
mkdir -p "$WORK" "$OUT"

echo "==> ui (electron-builder, for its AppRun and desktop entry)"
( cd "$ROOT/ui" && npx --no-install electron-builder --linux AppImage --publish never >/dev/null )
cp "$ROOT/ui/dist/$NAME" "$WORK/electron.AppImage"
( cd "$WORK" && ./electron.AppImage --appimage-extract >/dev/null && mv squashfs-root AppDir )

echo "==> agent on $IMAGE, with its libraries"
docker run --rm -v "$ROOT:/src:ro" -v "$WORK:/work" -e VERSION="$VERSION" -e UPDATE="$UPDATE" -e NAME="$NAME" \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" "$IMAGE" bash -euo pipefail -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq --no-install-recommends build-essential cmake pkg-config \
      qt6-base-dev libhidapi-dev libx11-dev libxi-dev libdbus-1-dev \
      patchelf zsync file ca-certificates curl libssl3 >/dev/null
  cmake -S /src/agent -B /tmp/agent -DCMAKE_BUILD_TYPE=Release -DEDGELINE_VERSION_STRING="$VERSION" >/dev/null
  cmake --build /tmp/agent -j"$(nproc)" >/dev/null
  ( cd /tmp/agent && ctest --output-on-failure >/dev/null ) && echo "  tests pass on 22.04"

  A=/work/AppDir/resources/agent
  rm -rf "$A"; mkdir -p "$A/lib" "$A/plugins/tls"
  cp /tmp/agent/edgeline-agent /tmp/agent/edgeline "$A/"

  # What every desktop has and must supply itself.
  keep_system() {
    case "$1" in
      ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|libutil.so*) return 0 ;;
      libstdc++.so*|libgcc_s.so*) return 0 ;;
      libX11.so*|libX11-xcb.so*|libxcb.so*|libXau.so*|libXdmcp.so*|libXext.so*|libXi.so*) return 0 ;;
      libudev.so*|libdbus-1.so*|libsystemd.so*) return 0 ;;
    esac
    return 1
  }
  bundle() {
    ldd "$1" | awk "/=> \\//{print \$3}" | while read -r lib; do
      base="$(basename "$lib")"
      keep_system "$base" && continue
      [ -e "$A/lib/$base" ] || cp -L "$lib" "$A/lib/"
    done
  }
  QTPLUG="$(dirname "$(dirname "$(readlink -f /usr/lib/x86_64-linux-gnu/libQt6Core.so.6)")")/qt6/plugins"
  [ -d "$QTPLUG/tls" ] || QTPLUG=/usr/lib/x86_64-linux-gnu/qt6/plugins
  cp "$QTPLUG"/tls/libqopensslbackend.so "$A/plugins/tls/" 2>/dev/null || true
  # Qt loads OpenSSL by name at run time, so ldd cannot see it.
  cp -L /usr/lib/x86_64-linux-gnu/libssl.so.3 /usr/lib/x86_64-linux-gnu/libcrypto.so.3 "$A/lib/"
  for f in "$A/edgeline-agent" "$A/edgeline" "$A"/plugins/tls/*.so; do bundle "$f"; done
  for l in "$A"/lib/*.so*; do bundle "$l"; done
  patchelf --set-rpath "\$ORIGIN/lib" "$A/edgeline-agent" "$A/edgeline"
  for l in "$A"/lib/*.so*; do patchelf --set-rpath "\$ORIGIN" "$l"; done
  for p in "$A"/plugins/tls/*.so; do patchelf --set-rpath "\$ORIGIN/../../lib" "$p"; done
  printf "[Paths]\nPrefix = .\nPlugins = plugins\n" > "$A/qt.conf"
  echo "  bundled $(ls "$A/lib" | wc -l) libraries ($(du -sh "$A/lib" | cut -f1)), max glibc $(objdump -T "$A/edgeline-agent" "$A"/lib/*.so* 2>/dev/null | grep -o "GLIBC_2\.[0-9]*" | sort -t. -k2 -n | tail -1)"

  echo "==> pack with appimagetool (static runtime, update information)"
  curl -fsSL -o /tmp/appimagetool https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x /tmp/appimagetool
  cd /work
  ARCH=x86_64 /tmp/appimagetool --appimage-extract-and-run -u "$UPDATE" AppDir "$NAME" >/tmp/tool.log 2>&1 || { cat /tmp/tool.log; exit 1; }
  chown "$HOST_UID:$HOST_GID" -R /work
'
mv "$WORK/$NAME" "$WORK/$NAME.zsync" "$OUT/"
echo "==> $OUT/$NAME"
echo "==> $OUT/$NAME.zsync"
