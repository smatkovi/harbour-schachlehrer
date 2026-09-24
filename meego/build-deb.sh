#!/bin/sh
# Packages the ARM build as a Harmattan .deb. Runs on the build machine
# after "meego/build.sh arm":
#
#   meego/build-deb.sh                # -> build/meego/harbour-schachlehrer_<version>_armel.deb
#   VERSION=1.1.1 meego/build-deb.sh
#
# Installed layout on the N9:
#   /opt/harbour-schachlehrer/bin/harbour-schachlehrer          the game
#   /opt/harbour-schachlehrer/{qml,assets,icons,translations}
#   /usr/share/applications/harbour-schachlehrer.desktop
#   /usr/share/themes/base/meegotouch/icons/harbour-schachlehrer-80.png
# The .deb is written by mkdeb.py (no dpkg-deb needed), as in nfsshift-sfos.
set -e

HERE=$(cd "$(dirname "$0")/.." && pwd)
PKG=$HERE/meego
OUT=$HERE/build/meego
BIN=$OUT/arm/harbour-schachlehrer
XGCC=${XGCC:-/tmp/xgcc-harmattan}
VERSION=${VERSION:-$(sed -n 's/^Version: *//p' "$HERE/rpm/harbour-schachlehrer.spec" | head -1)}

[ -x "$BIN" ] || { echo "ARM binary missing: $BIN (run meego/build.sh arm)" >&2; exit 1; }
[ -f "$OUT/arm/translations/harbour-schachlehrer-de.qm" ] || { echo "translations missing in $OUT/arm" >&2; exit 1; }

STAGE=$OUT/stage
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$STAGE/opt/harbour-schachlehrer/bin" "$STAGE/opt/harbour-schachlehrer/icons" \
         "$STAGE/usr/share/applications" "$STAGE/usr/share/themes/base/meegotouch/icons" \
         "$STAGE/usr/share/doc/harbour-schachlehrer"

# --- program and data -------------------------------------------------------
cp "$BIN" "$STAGE/opt/harbour-schachlehrer/bin/harbour-schachlehrer"
"$XGCC/bin/arm-none-linux-gnueabi-strip" "$STAGE/opt/harbour-schachlehrer/bin/harbour-schachlehrer"
chmod 755 "$STAGE/opt/harbour-schachlehrer/bin/harbour-schachlehrer"
cp -a "$PKG/qml" "$STAGE/opt/harbour-schachlehrer/qml"
# Everything meego/main.cpp looks for, at the paths it looks at: the engine
# and its network, the two item banks plus the theme table, the endgame
# tablebases and the piece artwork.
OPT=$STAGE/opt/harbour-schachlehrer
mkdir -p "$OPT/items" "$OPT/net" "$OPT/assets/pieces" "$OPT/syzygy"
[ -x "$HERE/build/meego/stockfish" ] || {
    echo "build/meego/stockfish missing -- run meego/build-stockfish.sh" >&2; exit 1; }
install -m 755 "$HERE/build/meego/stockfish" "$OPT/bin/stockfish"
install -m 644 "$HERE/assets/nn-37f18f62d772.nnue" "$OPT/net/"
cp -a "$HERE/assets/items/." "$OPT/items/"
cp -a "$HERE/assets/pieces/." "$OPT/assets/pieces/"
cp -a "$HERE/assets/syzygy/." "$OPT/syzygy/"
cp -a "$HERE/sailfish/icons" "$OPT/qml/icons"
cp -a "$OUT/arm/translations" "$STAGE/opt/harbour-schachlehrer/translations"
cp "$HERE/sailfish/icons/icon-256.png" "$STAGE/opt/harbour-schachlehrer/icons/icon-256.png"

# --- icons: 80x80 for the launcher, 64x64 base64 for the package manager ---
# These come from meego/icons/, cut to the exact squircle of the stock apps
# (meego-icon-tool/squircle.py --fill); do not re-scale the Sailfish icon here.
# The launcher looks in hicolor, not in the meegotouch theme: every
# working third-party app on the device puts its icon here.
mkdir -p "$STAGE/usr/share/icons/hicolor/80x80/apps"
cp "$HERE/meego/icons/icon-80.png" "$STAGE/usr/share/icons/hicolor/80x80/apps/harbour-schachlehrer.png"
cp "$HERE/meego/icons/icon-80.png" "$STAGE/usr/share/themes/base/meegotouch/icons/harbour-schachlehrer-80.png"
cp "$HERE/meego/icons/icon-64.png" "$OUT/icon-64.png"

cp "$PKG/harbour-schachlehrer.desktop" "$STAGE/usr/share/applications/harbour-schachlehrer.desktop"
gzip -9nc "$PKG/changelog" > "$STAGE/usr/share/doc/harbour-schachlehrer/changelog.gz"
find "$STAGE" -type f ! -path "*/bin/*" -exec chmod 644 {} +
chmod 755 "$OPT/bin/stockfish"
find "$STAGE" -type d -exec chmod 755 {} +

# --- control from control.in --------------------------------------------------
# XB-Maemo-Icon-26 is the 64x64 PNG as base64, continuation lines indented by
# one space; without it the application manager shows no icon.
VERSION="$VERSION" ICON="$OUT/icon-64.png" python3 - "$PKG/control.in" "$STAGE/DEBIAN/control" <<'PY'
import base64, os, sys, textwrap
src, dst = sys.argv[1], sys.argv[2]
with open(os.environ["ICON"], "rb") as f:
    b64 = base64.b64encode(f.read()).decode("ascii")
icon = "\n".join(" " + line for line in textwrap.wrap(b64, 76))
with open(src, "r", encoding="utf-8") as f:
    ctl = f.read()
ctl = ctl.replace("@VERSION@", os.environ["VERSION"]).replace("@ICON@", icon)
with open(dst, "w", encoding="utf-8") as f:
    f.write(ctl)
PY

DEB="$OUT/harbour-schachlehrer_${VERSION}_armel.deb"
python3 "$PKG/mkdeb.py" "$STAGE" "$DEB"
python3 "$PKG/mkdeb.py" --info "$DEB" | head -40
echo "== $DEB"
