#!/bin/sh
# Converts the Sailfish QML to QtQuick 1.1 + com.nokia.meego in meego/qml/.
#
# sailfish/ is copied, never edited: the Sailfish build keeps QtQuick 2.6 and
# Silica. What changes mechanically:
#
#   import QtQuick 2.6        ->  import QtQuick 1.1
#   import Sailfish.Silica    ->  import com.nokia.meego 1.0
#   property var              ->  property variant
#   readonly property         ->  property
#   Theme.                    ->  AppTheme.     (com.nokia.meego has a Theme of
#                                                its own that beats a context
#                                                property of that name)
#   SilicaFlickable           ->  Flickable
#
# The Silica types that have no counterpart are written out as local
# components in meego/qml (PageHeader, SectionHeader, TextSwitch, ...), so the
# pages themselves keep their spelling.
#
# "pragma Singleton" does not exist in QtQuick 1.1; meego/main.cpp instantiates
# the five singletons and puts them in the root context under the same names.
set -e
cd "$(dirname "$0")/.."
OUT=meego/qml

for f in sailfish/*.qml; do
    name=$(basename "$f")
    case "$name" in
        Style.qml|Prefs.qml|Dimensions.qml|PieceCode.qml|MoveList.qml) dst=$OUT/context/$name ;;
        *) dst=$OUT/$name ;;
    esac
    sed -e 's/^pragma Singleton$//' \
        -e 's/^import QtQuick 2\.[0-9]*$/import QtQuick 1.1/' \
        -e 's/^import Sailfish\.Silica 1\.0$/import com.nokia.meego 1.0/' \
        -e 's/\breadonly property /property /g' \
        -e 's/\bproperty var /property variant /g' \
        -e 's/\bTheme\./AppTheme./g' \
        -e 's/\bSilicaFlickable\b/Flickable/g' \
        "$f" > "$dst"
done

# Qt 4.7 pushes the source text of a translation through Latin-1; only qsTr
# keys are rewritten, plain literals keep their typography.
# Silica page properties com.nokia.meego spells differently.
python3 meego/fix-silica.py "$OUT"/*.qml

python3 meego/ascii-qstr.py qml "$OUT"/*.qml "$OUT"/context/*.qml

# The app's source language is German, and Qt 4.7 loses an umlaut in a qsTr
# key. The keys become ASCII and the German moves into a generated catalogue,
# where it survives intact.
python3 meego/german-catalogue.py "$OUT"/*.qml

echo "== $(ls sailfish/*.qml | wc -l) files -> $OUT"
