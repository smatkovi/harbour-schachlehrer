#!/usr/bin/env python3
"""Moves the German UI text out of the qsTr keys and into a catalogue.

Qt 4.7 puts the source text of a translation through Latin-1, so an umlaut in
a qsTr() key comes back mangled -- measured on an N950. The app's source
language is German, so ASCII-ising the keys would mangle the interface itself.

A translated string does not take that path: it comes out of the .qm as a
QString and arrives intact, which is why the Hungarian and German text of the
Tarock port displays correctly.

So the keys become ASCII and the German moves into a generated de catalogue
whose <source> is the ASCII key and whose <translation> is the original text.
The interface reads exactly as before, with the umlauts back.

    meego/german-catalogue.py meego/qml/*.qml   -> rewrites them in place and
                                                   writes the .ts next to them
"""
import os
import re
import sys
from xml.sax.saxutils import escape

# Transliteration, not deletion: the key stays readable for anyone who has to
# match it up with the catalogue.
MAP = {
    "ä": "ae", "ö": "oe", "ü": "ue",
    "Ä": "Ae", "Ö": "Oe", "Ü": "Ue",
    "ß": "ss",
    "é": "e", "è": "e", "á": "a", "í": "i", "ó": "o", "ú": "u",
}

QSTR = re.compile(r'qsTr\(\s*"((?:[^"\\]|\\.)*)"')


def asciify(text):
    for src, dst in MAP.items():
        text = text.replace(src, dst)
    # Anything still outside ASCII would be lost by Qt 4 anyway; drop it so
    # the key is stable rather than half-mangled.
    return "".join(c for c in text if ord(c) < 128)


def main(paths):
    contexts = {}
    for path in paths:
        name = os.path.basename(path)
        if not name.endswith(".qml"):
            continue
        context = name[:-4]
        with open(path, encoding="utf-8") as fh:
            before = fh.read()

        messages = []

        def repl(match):
            original = match.group(1)
            if all(ord(c) < 128 for c in original):
                return match.group(0)
            key = asciify(original)
            messages.append((key, original))
            return 'qsTr("%s"' % key

        after = QSTR.sub(repl, before)
        if messages:
            contexts.setdefault(context, []).extend(messages)
        if after != before:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(after)

    out = os.path.join(os.path.dirname(paths[0]) or ".", "..",
                       "harbour-schachlehrer-de.ts")
    total = 0
    with open(out, "w", encoding="utf-8") as fh:
        fh.write('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n'
                 '<TS version="2.0" language="de_DE">\n')
        for context in sorted(contexts):
            seen = set()
            fh.write("<context>\n    <name>%s</name>\n" % context)
            for key, original in contexts[context]:
                if key in seen:
                    continue
                seen.add(key)
                total += 1
                fh.write("    <message>\n        <source>%s</source>\n"
                         "        <translation>%s</translation>\n    </message>\n"
                         % (escape(key), escape(original)))
            fh.write("</context>\n")
        fh.write("</TS>\n")
    print("%d German strings moved into %s" % (total, os.path.normpath(out)))


if __name__ == "__main__":
    main(sys.argv[1:])
