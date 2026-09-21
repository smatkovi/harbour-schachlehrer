#!/usr/bin/env python3
"""Silica page properties that com.nokia.meego spells differently.

Run after the sed pass of meego/port-qml.sh, on meego/qml only.
"""
import re
import sys

SUBST = [
    # Silica lets a page list the orientations it allows; a MeeGo Page locks
    # to one instead, so "all" becomes "follow the device".
    (r'allowedOrientations:\s*Orientation\.All\b',
     'orientationLock: PageOrientation.Automatic'),
    (r'allowedOrientations:\s*Orientation\.Portrait(\s*\|\s*Orientation\.PortraitInverted)?',
     'orientationLock: PageOrientation.LockPortrait'),
    (r'allowedOrientations:\s*Orientation\.Landscape(\s*\|\s*Orientation\.LandscapeInverted)?',
     'orientationLock: PageOrientation.LockLandscape'),
    # ApplicationWindow.defaultAllowedOrientations has no counterpart at all;
    # the window follows the device and each page may still lock itself.
    (r'^\s*allowedOrientations:\s*defaultAllowedOrientations\s*$\n', ''),
    (r'^\s*defaultAllowedOrientations:.*$\n', ''),
    # Silica's cover; Harmattan has no cover pages.
    (r'^\s*cover:\s*Qt\.resolvedUrl\([^)]*\)\s*$\n', ''),
    # Silica's BusyIndicator sizes itself from an enum that does not exist
    # here; a plain size does the same job.
    (r'^(\s*)size:\s*BusyIndicatorSize\.\w+\s*$',
     r'\1width: AppTheme.itemSizeSmall\n\1height: AppTheme.itemSizeSmall'),
    # Qt.application has no "state" in Qt 4.7, only "active", so the handler
    # for coming back from the background has nothing to bind to. Declaring
    # the Connections tolerant leaves it inert here instead of failing the
    # whole page.
    (r'^(\s*)(target:\s*Qt\.application\s*)$',
     r'\1\2\n\1ignoreUnknownSignals: true'),
    # A plain QtQuick 1.1 Flickable never gets to flick when the drag starts
    # on a child MouseArea -- a button or a list row -- because the child
    # takes the press at once. pressDelay lets the Flickable steal it back,
    # which is what SilicaFlickable does for itself. Without this the pages
    # simply do not scroll.
    (r'^(\s*)Flickable \{\s*$\n(\s*)(anchors\.fill: parent)',
     r'\1Flickable {\n\2\3\n\2pressDelay: 150'),
    # Silica's Slider reports the end of a drag; the MeeGo one does not.
    (r'^(\s*)onReleased:', r'\1onPressedChanged: if (!pressed)'),
    # Silica's Label fades a truncated line; a QtQuick 1.1 Text elides.
    (r'^(\s*)truncationMode:\s*TruncationMode\.\w+\s*$', r'\1elide: Text.ElideRight'),
]

# Board.qml turns the board by reversing both axes of its GridView, and
# QtQuick 1.1 has no verticalLayoutDirection. The turn moves into
# squareOfCell() instead, which puts the same square in the same place. It is
# done here rather than in sailfish/Board.qml so that the Sailfish build is
# not touched at all.
BOARD_FROM = '''    function squareOfCell(cell) {
        var row = Math.floor(cell / 8)          // 0 at the top
        var col = cell % 8
        return (7 - row) * 8 + col
    }'''

BOARD_TO = '''    function squareOfCell(cell) {
        var row = Math.floor(cell / 8)          // 0 at the top
        var col = cell % 8
        // QtQuick 1.1 has no verticalLayoutDirection, so the turn of the
        // board happens here instead of by reversing the view's axes. The
        // square that lands on a given screen cell is the same either way.
        if (flipped) {
            row = 7 - row
            col = 7 - col
        }
        return (7 - row) * 8 + col
    }'''

BOARD_AXES_FROM = '''            // The whole turn of the board: both axes reversed, which is a
            // 180-degree rotation of the layout without rotating the pieces.
            layoutDirection: board.flipped ? Qt.RightToLeft : Qt.LeftToRight
            verticalLayoutDirection: board.flipped ? GridView.BottomToTop
                                                   : GridView.TopToBottom
'''

BOARD_AXES_TO = '''            // The turn is done in squareOfCell() above.
'''

# QtQuick 2 accepts a statement block as a property binding; QtQuick 1.1
# wants an expression, so the block becomes an immediately-called
# function. Only RoutinePanel does this, and the transform is anchored to
# that text rather than applied by pattern, so a new one elsewhere fails
# loudly instead of being missed.
ROUTINE_FROM = '''    property variant shown: {
        var out = []'''

ROUTINE_TO = '''    property variant shown: (function() {
        var out = []'''



def main(paths):
    changed = 0
    for path in paths:
        with open(path, encoding='utf-8') as fh:
            before = fh.read()
        after = before
        for pattern, repl in SUBST:
            after = re.sub(pattern, repl, after, flags=re.M)
        if path.endswith('RoutinePanel.qml'):
            if ROUTINE_FROM not in after:
                sys.stderr.write('RoutinePanel.qml: expected block not found\n')
                raise SystemExit(1)
            after = after.replace(ROUTINE_FROM, ROUTINE_TO)
            after = re.sub(r'(\n        return out\n)    \}', r'\1    })()',
                           after, count=1)
        if path.endswith('Board.qml'):
            for src, dst in ((BOARD_FROM, BOARD_TO), (BOARD_AXES_FROM, BOARD_AXES_TO)):
                if src not in after:
                    sys.stderr.write('Board.qml: expected block not found, '
                                     'the source moved under the port\n')
                    raise SystemExit(1)
                after = after.replace(src, dst)
        if after != before:
            with open(path, 'w', encoding='utf-8') as fh:
                fh.write(after)
            changed += 1
    print("silica: %d of %d files rewritten" % (changed, len(paths)))


if __name__ == '__main__':
    main(sys.argv[1:])
