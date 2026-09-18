#!/usr/bin/env python3
"""Generate the 5x7 glyph table used by the Matrix Rain clock (style 12).

The katakana are drawn here in their normal orientation and mirrored on the way
out, because the film's code is mirrored half-width katakana. Digits, the
letter Z and the symbols are emitted as drawn.

Z is the only letter of the Latin alphabet in the film's set. Adding the rest
of the alphabet would be wrong: a column of Latin letters reads as text rather
than as code.

    python scripts/gen_matrix_glyphs.py                    # write the header
    python scripts/gen_matrix_glyphs.py --sheet out.png    # contact sheet (Pillow)
"""

import argparse
import os
import sys

GLYPH_W = 5
GLYPH_H = 7

KATAKANA = [
    ("a", """
#####
...##
..#.#
.#..#
#...#
....#
...#.
"""),
    ("i", """
....#
...#.
..##.
.#.#.
#..#.
...#.
...#.
"""),
    ("u", """
..#..
#####
#...#
#...#
....#
...#.
..#..
"""),
    ("e", """
#####
..#..
..#..
..#..
..#..
#####
.....
"""),
    ("o", """
...#.
#####
...#.
..###
.#.#.
#..#.
...##
"""),
    ("ka", """
.####
.#..#
####.
.#..#
.#.#.
.##..
##...
"""),
    ("ki", """
...#.
#####
..#..
#####
..#..
..#..
..#..
"""),
    ("ku", """
.####
.#..#
#...#
....#
...#.
..#..
.#...
"""),
    ("ke", """
..#..
.####
.#..#
##..#
....#
...#.
..#..
"""),
    ("ko", """
#####
....#
....#
....#
....#
#####
.....
"""),
    ("sa", """
.#.#.
#####
.#.#.
..#..
..#..
..#..
.##..
"""),
    ("shi", """
##...
..##.
.....
##...
...#.
#..#.
.##..
"""),
    ("su", """
#####
....#
...#.
..#..
.###.
#..##
.....
"""),
    ("se", """
.#...
#####
.#...
.#...
.#...
.#..#
.####
"""),
    ("so", """
#....
..#.#
.#..#
....#
...#.
..#..
.#...
"""),
    ("ta", """
.####
.#..#
#...#
..###
.#.#.
#..#.
...#.
"""),
    ("chi", """
...##
.##..
#####
..#..
..#..
..#..
.##..
"""),
    ("tsu", """
#.#.#
#.#.#
....#
...#.
..#..
.#...
.....
"""),
    ("te", """
#####
.....
#####
...#.
..#..
..#..
..#..
"""),
    ("to", """
.#...
.#...
.##..
.#.#.
.#..#
.#...
.#...
"""),
    ("na", """
...#.
#####
...#.
..#..
..#..
.#...
#....
"""),
    ("ni", """
.....
.####
.....
.....
#####
.....
.....
"""),
    ("nu", """
#####
....#
...#.
.####
.#.#.
#...#
....#
"""),
    ("ne", """
..#..
#####
.#...
.###.
##.#.
...#.
..##.
"""),
    ("no", """
....#
....#
...#.
..#..
..#..
.#...
#....
"""),
    ("ha", """
.....
.#.#.
.#.#.
#...#
#...#
#...#
.....
"""),
    ("hi", """
#..#.
#.#..
##...
#....
#....
#...#
.####
"""),
    ("fu", """
#####
....#
....#
...#.
..#..
.#...
#....
"""),
    ("he", """
.....
.....
..#..
.#.#.
#...#
.....
.....
"""),
    ("ho", """
..#..
#####
..#..
.###.
##.##
..#..
..#..
"""),
    ("ma", """
#####
....#
...#.
..#..
.###.
..#..
..#..
"""),
    ("mi", """
.####
#....
.....
.####
#....
.....
#####
"""),
    ("mu", """
..#..
..#..
.#...
.#...
#...#
#...#
.####
"""),
    ("me", """
....#
#...#
.#.#.
..#..
.#.#.
#...#
#....
"""),
    ("mo", """
.####
..#..
#####
..#..
..#..
..#.#
..###
"""),
    ("ya", """
..#..
#.#..
#####
..#.#
..#..
..#..
..#..
"""),
    ("yu", """
.....
.####
....#
....#
....#
#####
.....
"""),
    ("yo", """
#####
....#
.####
....#
....#
#####
.....
"""),
    ("ra", """
.####
.....
#####
....#
...#.
..#..
.##..
"""),
    ("ri", """
#...#
#...#
#...#
#...#
....#
...#.
.##..
"""),
    ("ru", """
.#..#
.#..#
.#..#
.#..#
#...#
#..##
....#
"""),
    ("re", """
#....
#....
#....
#...#
#...#
.#.#.
..#..
"""),
    ("ro", """
.....
#####
#...#
#...#
#...#
#####
.....
"""),
    ("wa", """
#####
#...#
#...#
....#
...#.
..#..
.#...
"""),
    ("wo", """
#####
....#
#####
....#
...#.
..#..
.#...
"""),
    ("n", """
##...
..#..
.....
#....
....#
#..#.
.##..
"""),
]

ASCII_GLYPHS = [
    ("0", """
.###.
#...#
#..##
#.#.#
##..#
#...#
.###.
"""),
    ("1", """
..#..
.##..
..#..
..#..
..#..
..#..
.###.
"""),
    ("2", """
.###.
#...#
....#
...#.
..#..
.#...
#####
"""),
    ("3", """
#####
...#.
..#..
...#.
....#
#...#
.###.
"""),
    ("4", """
...#.
..##.
.#.#.
#..#.
#####
...#.
...#.
"""),
    ("5", """
#####
#....
####.
....#
....#
#...#
.###.
"""),
    ("6", """
..##.
.#...
#....
####.
#...#
#...#
.###.
"""),
    ("7", """
#####
#...#
....#
...#.
..#..
..#..
..#..
"""),
    ("8", """
.###.
#...#
#...#
.###.
#...#
#...#
.###.
"""),
    ("9", """
.###.
#...#
#...#
.####
....#
...#.
.##..
"""),
    ("Z", """
#####
....#
...#.
..#..
.#...
#....
#####
"""),
    ("colon", """
.....
..#..
..#..
.....
..#..
..#..
.....
"""),
    ("equal", """
.....
.....
#####
.....
#####
.....
.....
"""),
    ("star", """
.....
..#..
#.#.#
.###.
#.#.#
..#..
.....
"""),
    ("plus", """
.....
..#..
..#..
#####
..#..
..#..
.....
"""),
    ("less", """
...#.
..#..
.#...
#....
.#...
..#..
...#.
"""),
    ("greater", """
.#...
..#..
...#.
....#
...#.
..#..
.#...
"""),
    ("bar", """
..#..
..#..
..#..
.....
..#..
..#..
..#..
"""),
]


def parse(name, art, mirror):
    rows = art.strip("\n").split("\n")
    if len(rows) != GLYPH_H:
        sys.exit("glyph %s has %d rows, expected %d" % (name, len(rows), GLYPH_H))
    for row in rows:
        if len(row) != GLYPH_W:
            sys.exit("glyph %s row %r is %d wide, expected %d"
                     % (name, row, len(row), GLYPH_W))
    if mirror:
        rows = [row[::-1] for row in rows]
    cols = []
    for x in range(GLYPH_W):
        bits = 0
        for y in range(GLYPH_H):
            if rows[y][x] == "#":
                bits |= 1 << y
        cols.append(bits)
    return rows, cols


def build():
    glyphs = []
    for name, art in KATAKANA:
        rows, cols = parse(name, art, True)
        glyphs.append((name, rows, cols))
    for name, art in ASCII_GLYPHS:
        rows, cols = parse(name, art, False)
        glyphs.append((name, rows, cols))
    return glyphs


def write_header(glyphs, path):
    lines = [
        "// Generated by scripts/gen_matrix_glyphs.py - do not edit by hand.",
        "//",
        "// Mirrored half-width katakana plus digits, the letter Z and symbols at",
        "// 5x7, one byte per column with bit 0 as the top row.",
        "#ifndef MATRIX_GLYPHS_H",
        "#define MATRIX_GLYPHS_H",
        "",
        "#include <stdint.h>",
        "",
        "#define MX_GLYPH_W %d" % GLYPH_W,
        "#define MX_GLYPH_H %d" % GLYPH_H,
        "#define MX_GLYPH_COUNT %d" % len(glyphs),
        "",
        "static const uint8_t MX_GLYPHS[MX_GLYPH_COUNT][MX_GLYPH_W] = {",
    ]
    for name, _rows, cols in glyphs:
        body = ", ".join("0x%02X" % c for c in cols)
        lines.append("    {%s},  // %s" % (body, name))
    lines.append("};")
    lines.append("")
    lines.append("#endif  // MATRIX_GLYPHS_H")
    with open(path, "w", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    return len(glyphs) * GLYPH_W


def write_sheet(glyphs, path, scale=6):
    from PIL import Image, ImageDraw

    per_row = 12
    rows = (len(glyphs) + per_row - 1) // per_row
    cell_w, cell_h = (GLYPH_W + 3) * scale, (GLYPH_H + 5) * scale
    img = Image.new("RGB", (per_row * cell_w, rows * cell_h), (0, 0, 0))
    draw = ImageDraw.Draw(img)
    for i, (name, art, _cols) in enumerate(glyphs):
        ox = (i % per_row) * cell_w + scale
        oy = (i // per_row) * cell_h + scale
        for y in range(GLYPH_H):
            for x in range(GLYPH_W):
                if art[y][x] == "#":
                    draw.rectangle([ox + x * scale, oy + y * scale,
                                    ox + (x + 1) * scale - 1,
                                    oy + (y + 1) * scale - 1], fill=(0, 255, 70))
        draw.text((ox, oy + (GLYPH_H + 1) * scale), name, fill=(140, 140, 140))
    img.save(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sheet", help="also render a contact sheet PNG here")
    parser.add_argument("--out", help="header path")
    args = parser.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = args.out or os.path.join(root, "src", "clocks", "matrix_glyphs.h")

    glyphs = build()
    size = write_header(glyphs, out)
    print("%d glyphs, %d bytes -> %s" % (len(glyphs), size, out))
    if args.sheet:
        write_sheet(glyphs, args.sheet)
        print("sheet -> %s" % args.sheet)


if __name__ == "__main__":
    main()
