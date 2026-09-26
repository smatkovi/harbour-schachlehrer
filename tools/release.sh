#!/bin/sh
# Publish the packages of a version as a release of this repository.
#
#   tools/release.sh <version> <package> [<package> ...]
#
# Any package file goes: the two Sailfish RPMs, and the Harmattan .deb for the
# Nokia N9 once it is built. Running it again with more files adds them to the
# release that is already there.
#
# gh is logged in on the Arch machine, so the files go there first. A copy also
# lands in ~/ps/rpms/schachlehrer/, like the other apps keep theirs.
set -e
VERSION=$1
shift 2>/dev/null || true
if [ -z "$VERSION" ] || [ $# -eq 0 ]; then
    echo "usage: tools/release.sh <version> <paket> [<paket> ...]" >&2
    exit 2
fi
for FILE in "$@"; do
    [ -f "$FILE" ] || { echo "no such file: $FILE" >&2; exit 2; }
done

if [ -n "$BUILD_HOST" ]; then
    HOST=$BUILD_HOST
elif ssh -o BatchMode=yes -o ConnectTimeout=4 sebastian@192.168.1.21 true 2>/dev/null; then
    HOST=sebastian@192.168.1.21
else
    HOST=arch
fi
REPO=${REPO:-smatkovi/harbour-schachlehrer}
TAG=v$VERSION
WORK=/tmp/schachlehrer-release

# Keep a local copy next to the other apps' packages — unless the file that was
# handed in already is that copy.
mkdir -p "$HOME/ps/rpms/schachlehrer"
for FILE in "$@"; do
    DEST="$HOME/ps/rpms/schachlehrer/$(basename "$FILE")"
    if [ "$(readlink -f "$FILE")" != "$(readlink -f "$DEST")" ]; then
        cp "$FILE" "$DEST"
    fi
done

NOTES=$(mktemp)
cat > "$NOTES" <<NOTE
Schachlehrer $VERSION

Partie gegen ein zweites Gerät, über WLAN oder Bluetooth.

Pakete für Sailfish OS (\`.rpm\`, aarch64 und armv7hl) und für MeeGo
Harmattan auf dem Nokia N9/N950 (\`.deb\`, armel).
NOTE

ssh "$HOST" "rm -rf $WORK && mkdir -p $WORK"
scp "$@" "$NOTES" "$HOST:$WORK/"
rm -f "$NOTES"
NOTENAME=$(basename "$NOTES")

# Exactly the files that were handed in — a glob here would quietly drop the
# .deb and upload only the RPMs.
NAMES=""
for FILE in "$@"; do
    NAMES="$NAMES $(basename "$FILE")"
done

ssh "$HOST" "cd $WORK && \
    if gh release view $TAG --repo $REPO >/dev/null 2>&1; then \
        gh release upload $TAG$NAMES --repo $REPO --clobber; \
    else \
        gh release create $TAG$NAMES --repo $REPO --title 'Schachlehrer $VERSION' --notes-file $NOTENAME; \
    fi"
echo "released $TAG to $REPO, copy in ~/ps/rpms/schachlehrer/"
