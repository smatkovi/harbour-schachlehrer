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
]


def main(paths):
    changed = 0
    for path in paths:
        with open(path, encoding='utf-8') as fh:
            before = fh.read()
        after = before
        for pattern, repl in SUBST:
            after = re.sub(pattern, repl, after, flags=re.M)
        if after != before:
            with open(path, 'w', encoding='utf-8') as fh:
                fh.write(after)
            changed += 1
    print("silica: %d of %d files rewritten" % (changed, len(paths)))


if __name__ == '__main__':
    main(sys.argv[1:])
