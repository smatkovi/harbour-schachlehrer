#!/usr/bin/env python3
"""Makes qsTr()/tr() keys Latin-1-safe for the Qt 4.7 build.

Measured on an N950: a plain QML string literal keeps "—" intact (charCode
8212), but the same character inside qsTr() comes back as charCode 20. Qt 4.7
puts the source text of a translation through Latin-1, and anything outside it
is lost. Card suits, arrows and bullets are plain literals and are untouched;
only translated text is rewritten.

The same substitution is applied to the .ts catalogues before lrelease, so the
keys still match and the German translation keeps working.

    meego/ascii-qstr.py qml  meego/qml/*.qml
    meego/ascii-qstr.py ts   build/meego/arm/harbour-tarock-de.ts
"""
import re
import sys

SUBST = {
    "—": "-",    # em dash
    "–": "-",    # en dash
    # Single quotes, never double: the replacement lands inside a
    # double-quoted QML literal and a " there ends the string early. That is
    # what broke MainPage.qml on the first run ("Expected token `)'"), so this
    # is not a matter of taste.
    "“": "'", "”": "'",       # English quotes
    "„": "'", "‚": "'",       # German low quotes
    "‘": "'", "’": "'",
    "…": "...",
    " ": " ",
}


def fix(text):
    for src, dst in SUBST.items():
        text = text.replace(src, dst)
    return text


def fix_qml(text):
    # Only the literal inside qsTr("...") / qsTr('...'), nothing else.
    def repl(m):
        return m.group(1) + fix(m.group(2)) + m.group(3)
    text = re.sub(r'(qsTr\(\s*")([^"\\]*(?:\\.[^"\\]*)*)(")', repl, text)
    text = re.sub(r"(qsTr\(\s*')([^'\\]*(?:\\.[^'\\]*)*)(')", repl, text)
    return text


def fix_ts(text):
    # <source>…</source> carries the key; <translation> is the output and
    # must keep its real typography.
    return re.sub(r'(<source>)(.*?)(</source>)',
                  lambda m: m.group(1) + fix(m.group(2)) + m.group(3),
                  text, flags=re.S)


def main(argv):
    if len(argv) < 3 or argv[1] not in ("qml", "ts"):
        sys.stderr.write(__doc__)
        return 2
    handler = fix_qml if argv[1] == "qml" else fix_ts
    changed = 0
    for path in argv[2:]:
        with open(path, encoding="utf-8") as fh:
            before = fh.read()
        after = handler(before)
        if after != before:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(after)
            changed += 1
    print("%s: %d of %d files rewritten" % (argv[1], changed, len(argv) - 2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
