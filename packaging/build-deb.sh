#!/bin/sh
# Builds a .deb of HiTuxShare into dist/.
#
# Deliberately plain dpkg-deb over a staged CMake install rather than a full
# debian/ source package: this produces a binary anyone can install now, without
# committing to Debian packaging policy for a project at version 0.1.
#
# Runtime dependencies are computed with dpkg-shlibdeps rather than written by
# hand, so they stay correct when the Qt modules we link against change.

set -e

PROJECT_ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(sed -n 's/.*HITUX_SHARE_VERSION_STRING *"\([^"]*\)".*/\1/p' \
    "$PROJECT_ROOT/core/HiTuxShareVersion.h")
ARCHITECTURE=$(dpkg --print-architecture)

BUILD_DIR="$PROJECT_ROOT/build-deb"
STAGE_DIR="$PROJECT_ROOT/build-deb/stage"
OUTPUT_DIR="$PROJECT_ROOT/dist"

echo "Building HiTuxShare $VERSION for $ARCHITECTURE"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/DEBIAN" "$OUTPUT_DIR"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr > /dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)" > /dev/null
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR" > /dev/null

# Symbols are worth keeping out of the package but worth keeping somewhere:
# a crash report from an installed build is unreadable without them.
cp "$STAGE_DIR/usr/bin/hitux" "$OUTPUT_DIR/hitux-$VERSION.debug"
strip --strip-unneeded "$STAGE_DIR/usr/bin/hitux"

# dpkg-shlibdeps insists on being run from a directory containing debian/control.
mkdir -p "$BUILD_DIR/debian"
cat > "$BUILD_DIR/debian/control" <<CONTROL
Source: hituxshare
Package: hituxshare
Architecture: $ARCHITECTURE
CONTROL

DEPENDS=$(cd "$BUILD_DIR" && dpkg-shlibdeps -O --ignore-missing-info \
    "$STAGE_DIR/usr/bin/hitux" 2>/dev/null \
    | sed 's/^shlibs:Depends=//')

if [ -z "$DEPENDS" ]; then
    echo "warning: could not compute dependencies; falling back to a fixed list" >&2
    DEPENDS="libqt6widgets6, libqt6gui6, libqt6core6, libqt6dbus6, zlib1g, libc6"
fi

INSTALLED_SIZE=$(du -ks "$STAGE_DIR/usr" | cut -f1)

cat > "$STAGE_DIR/DEBIAN/control" <<CONTROL
Package: hituxshare
Version: $VERSION
Section: net
Priority: optional
Architecture: $ARCHITECTURE
Depends: $DEPENDS
Installed-Size: $INSTALLED_SIZE
Maintainer: Kevin Adams <kevinadams05@gmail.com>
Homepage: https://github.com/KevinAdams05/HiTuxShare
Description: Chat and share files on the BeShare network
 HiTuxShare is a native Linux client for the BeShare file-sharing network,
 speaking the MUSCLE protocol.  It joins the same chat rooms, user lists and
 file searches as the Haiku and BeOS clients it was ported from.
 .
 File searches are live subscriptions rather than snapshots, and transfers go
 directly between peers rather than through the server.
CONTROL

PACKAGE="$OUTPUT_DIR/hituxshare_${VERSION}_${ARCHITECTURE}.deb"
fakeroot dpkg-deb --build "$STAGE_DIR" "$PACKAGE" > /dev/null

echo "Built $PACKAGE"
dpkg-deb --info "$PACKAGE" | sed -n '2,12p'
echo
echo "Contents:"
dpkg-deb --contents "$PACKAGE" | awk '{print "  " $6}' | grep -v '/$' | head -20
