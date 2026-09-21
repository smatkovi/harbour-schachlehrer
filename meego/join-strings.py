#!/usr/bin/env python3
"""Merges adjacent string literals, which Qt 4.7's QML parser rejects.

The Sailfish sources split long German sentences the C way:

    qsTr("first part "
         "second part")

Qt 5's QML engine accepts that; Qt 4.7 stops at "Expected token `)'". The two
literals are merged into one, which also lets the catalogue generator see the
whole sentence as a single key.

Only whitespace may stand between the two literals, so a list like
["a",\\n "b"] and two separate properties on consecutive lines are left alone.
"""
import re
import sys

PAIR = re.compile(r'("(?:[^"\\\n]|\\.)*)"[ \t]*\n[ \t]*"')


def merge(text):
    while True:
        merged = PAIR.sub(r'\1', text)
        if merged == text:
            return text
        text = merged


def main(paths):
    changed = 0
    for path in paths:
        with open(path, encoding='utf-8') as fh:
            before = fh.read()
        after = merge(before)
        if after != before:
            with open(path, 'w', encoding='utf-8') as fh:
                fh.write(after)
            changed += 1
    print("join: %d of %d files rewritten" % (changed, len(paths)))


if __name__ == '__main__':
    main(sys.argv[1:])
