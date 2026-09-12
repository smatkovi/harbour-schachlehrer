#!/usr/bin/env python3
"""Generate the application icon in the five Sailfish sizes.

A board corner in two wood tones with a knight standing on it. Drawn here
rather than taken from a set so that the icon carries no licence of its own;
the cburnett pieces on the board itself are a separate matter (CREDITS/ASSETS.md).

The knight is the U+265E glyph of DejaVu Sans, which is a solid silhouette and
survives being scaled down to 86 px. Without that font the script falls back to
a drawn crown, so the package always has an icon.

Usage:  python3 tools/make_icon.py
"""

import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAILFISH_SIZES = (86, 108, 128, 172, 256)

DARK = (108, 76, 48, 255)        # the dark square
LIGHT = (232, 210, 178, 255)     # the light square
EDGE = (58, 40, 26, 255)
PIECE = (250, 248, 244, 255)     # a white piece, as on the board
PIECE_EDGE = (32, 24, 18, 255)

SUPERSAMPLE = 4

KNIGHT = "♞"                # BLACK CHESS KNIGHT: filled, so it reads small

FONT_CANDIDATES = (
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/symbola/Symbola.ttf",
)


def chess_font(size):
    """A font that actually has the chess glyph, or None."""
    for path in FONT_CANDIDATES:
        if not os.path.exists(path):
            continue
        try:
            fnt = ImageFont.truetype(path, size)
        except OSError:
            continue
        if fnt.getmask(KNIGHT).getbbox():
            return fnt
    return None


def board(px):
    """The 4x4 board that fills the icon."""
    image = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    cell = px / 4.0
    for row in range(4):
        for col in range(4):
            fill = LIGHT if (row + col) % 2 == 0 else DARK
            draw.rectangle((col * cell, row * cell,
                            (col + 1) * cell - 1, (row + 1) * cell - 1), fill=fill)

    # Round the corners by masking, the way the other Sailfish icons look.
    mask = Image.new("L", (px, px), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, px - 1, px - 1),
                                           radius=int(px * 0.22), fill=255)
    rounded = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    rounded.paste(image, (0, 0), mask)
    ImageDraw.Draw(rounded).rounded_rectangle((0, 0, px - 1, px - 1),
                                              radius=int(px * 0.22),
                                              outline=EDGE, width=max(2, px // 120))
    return rounded


def crown(draw, box):
    """Fallback piece: a plain crown, in case the chess glyph is unavailable."""
    left, top, right, bottom = box
    width = right - left
    height = bottom - top
    draw.polygon([(left, bottom - height * 0.30),
                  (left + width * 0.10, top),
                  (left + width * 0.30, top + height * 0.35),
                  (left + width * 0.50, top - height * 0.05),
                  (left + width * 0.70, top + height * 0.35),
                  (left + width * 0.90, top),
                  (right, bottom - height * 0.30)],
                 fill=PIECE, outline=PIECE_EDGE)
    draw.rounded_rectangle((left, bottom - height * 0.32, right, bottom),
                           radius=height * 0.08, fill=PIECE, outline=PIECE_EDGE)


def icon(size):
    px = size * SUPERSAMPLE
    image = board(px)
    draw = ImageDraw.Draw(image)

    glyph_box = (px * 0.14, px * 0.10, px * 0.86, px * 0.90)
    fnt = chess_font(int(px * 0.78))
    if fnt is None:
        crown(draw, glyph_box)
        return image.resize((size, size), resample=Image.LANCZOS)

    left, top, right, bottom = draw.textbbox((0, 0), KNIGHT, font=fnt)
    x = (px - (right - left)) / 2 - left
    y = (px - (bottom - top)) / 2 - top

    # A dark halo under the light piece, so it stays legible on the light
    # squares as well as on the dark ones.
    halo = max(2, px // 90)
    for dx in (-halo, 0, halo):
        for dy in (-halo, 0, halo):
            if dx or dy:
                draw.text((x + dx, y + dy), KNIGHT, font=fnt, fill=PIECE_EDGE)
    draw.text((x, y), KNIGHT, font=fnt, fill=PIECE)

    return image.resize((size, size), resample=Image.LANCZOS)


def main():
    out = os.path.join(ROOT, "sailfish", "icons")
    os.makedirs(out, exist_ok=True)
    for size in SAILFISH_SIZES:
        icon(size).save(os.path.join(out, "icon-%d.png" % size))
        print("sailfish/icons/icon-%d.png" % size)


if __name__ == "__main__":
    main()
