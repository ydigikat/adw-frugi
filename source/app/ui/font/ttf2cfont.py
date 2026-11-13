#!/usr/bin/env python3
"""
ttf2cfont.py - rasterise a monospaced TrueType/OpenType font to bitmap table.

Usage:
    ttf2cfont.py FONT.ttf --name NAME [--out DIR] [--pt 18] [--dpi 141]
                 [--px N] [--first 0x20] [--last 0x7e]
                 [--cell-w N] [--cell-h N] [--weight N] [--licence-file FILE]

The generated table is a derivative of the font, so it carries the font's
own licence: the copyright and licence strings are copied from the font's
name table into the .c file header, which also names the licence text file
given by --licence-file (to be shipped alongside it).

Frugi's display font (font_ui.c/h) is generated from one of the font
folders next to this script - each holds the source TTF, its licence and a
make_font.py that runs this script with that font's settings:

    python3 jetbrains_mono/make_font.py
    python3 noto_sans_mono/make_font.py

Whichever was run last is the font built into the firmware; the header of
font_ui.c names it.  16x24 cells give the 20x10 character grid on the
320x240 panel.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

def name_strings(path):
    tt = TTFont(path, lazy=True)
    out = {}
    for rec in tt["name"].names:
        if rec.nameID in (0, 1, 4, 13, 14) and rec.nameID not in out:
            try:
                out[rec.nameID] = rec.toUnicode().strip()
            except Exception:
                pass
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ttf")
    ap.add_argument("--name", required=True, help="C identifier suffix, e.g. ui")
    ap.add_argument("--out", default=".")
    ap.add_argument("--pt", type=float, default=18.0)
    ap.add_argument("--dpi", type=float, default=141.0)
    ap.add_argument("--px", type=float, help="em size in pixels (overrides --pt/--dpi)")
    ap.add_argument("--first", type=lambda s: int(s, 0), default=0x20)
    ap.add_argument("--last", type=lambda s: int(s, 0), default=0x7E)
    ap.add_argument("--cell-h", type=int, help="cell height (>= ink height); spare rows go on top")
    ap.add_argument("--cell-w", type=int, help="override cell width (default: rounded advance)")
    ap.add_argument("--licence-file", help="font licence text shipped alongside the output")
    ap.add_argument("--weight", type=float,
                    help="weight for a variable font (e.g. 300 = Light); "
                         "assumes weight is the font's first variation axis")
    a = ap.parse_args()

    em_px = a.px if a.px else a.pt * a.dpi / 72.0
    font = ImageFont.truetype(a.ttf, size=em_px)
    if a.weight:
        font.set_variation_by_axes([a.weight])

    adv = font.getlength("M")
    cell_w = a.cell_w or int(round(adv))
    if cell_w > 32:
        sys.exit("error: cell wider than 32 pixels is not supported")
    # sanity: monospaced?
    for probe in ("i", "W", "."):
        if abs(font.getlength(probe) - adv) > 0.01:
            print(f"warning: font does not look monospaced ({probe!r} advance "
                  f"{font.getlength(probe)} vs {adv})", file=sys.stderr)
            break

    # Render each glyph on a generous canvas, baseline at (OX, BY)
    OX = int(em_px * 2)
    BY = int(em_px * 2)
    W = OX * 2 + int(em_px * 2)
    H = BY + int(em_px * 2)

    glyphs = {}          # enc -> list of (x, y) relative to origin/baseline
    for enc in range(a.first, a.last + 1):
        img = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        d.text((OX, BY), chr(enc), font=font, fill=255, anchor="ls")
        px = img.load()
        pts = [(x - OX, y - BY) for y in range(H) for x in range(W) if px[x, y]]
        glyphs[enc] = pts

    ys = [y for pts in glyphs.values() for _, y in pts]
    if not ys:
        sys.exit("error: nothing rendered - does the font cover the range?")
    top, bottom = min(ys), max(ys) + 1      # rows relative to baseline (y<0 above)
    ink_h = bottom - top
    cell_h = a.cell_h or ink_h
    if cell_h < ink_h:
        sys.exit(f"error: --cell-h must be >= {ink_h}")
    base = -top + (cell_h - ink_h)          # baseline row inside the cell

    rowtype, ctype = ("uint8_t", 8) if cell_w <= 8 else \
                     ("uint16_t", 16) if cell_w <= 16 else ("uint32_t", 32)
    hexw = ctype // 4

    table, clipped, blank = [], [], []
    for enc in range(a.first, a.last + 1):
        rows = [0] * cell_h
        pts = glyphs[enc]
        if not pts and enc != 0x20:
            blank.append(enc)
        for x, y in pts:
            cx, cy = x, y + base
            if 0 <= cx < cell_w and 0 <= cy < cell_h:
                rows[cy] |= 1 << (cell_w - 1 - cx)
            elif enc not in clipped:
                clipped.append(enc)
        table.append(rows)

    os.makedirs(a.out, exist_ok=True)
    ident = "".join(c if c.isalnum() else "_" for c in a.name)
    guard = f"__FONT_{ident.upper()}_H"
    count = a.last - a.first + 1
    ns = name_strings(a.ttf)

    lic_lines = []
    for nid, label in ((1, "Font family"), (4, "Full name"), (0, "Copyright"),
                       (13, "Licence"), (14, "Licence URL")):
        if nid in ns:
            txt = ns[nid].replace("*/", "* /")
            body = "\n".join("      " + ln.strip() if ln.strip() else "" for ln in txt.splitlines())
            lic_lines.append(f"   {label}:" + ("\n" + body if "\n" in txt else " " + txt))
    if a.weight:
        lic_lines.append(f"   Drawn at weight {a.weight:g} (a variable font - the names above are "
                         "its default instance).")
    lic = "\n".join(lic_lines) or "   (no licence text in the font's name table)"

    with open(os.path.join(a.out, f"font_{ident}.h"), "w") as f:
        f.write(f"""/*
   Plain fixed-cell bitmap font table generated by ttf2cfont.py from
   {os.path.basename(a.ttf)} at {em_px:.2f} px em ({'%g pt @ %g dpi' % (a.pt, a.dpi) if not a.px else 'given in pixels'}){', weight %g' % a.weight if a.weight else ''},
   1-bit monochrome. See font_{ident}.c for licence notices.

   Layout: font_{ident}[ch - FONT_FIRST][row], row 0 = top of cell.
   Bit (FONT_CELL_W-1) of each row is the leftmost pixel.
*/
#ifndef {guard}
#define {guard}

#include <stdint.h>

#define FONT_FIRST  (0x{a.first:02X})
#define FONT_LAST   (0x{a.last:02X})
#define FONT_CELL_W ({cell_w})
#define FONT_CELL_H ({cell_h})

typedef {rowtype} font_row_t;

extern const font_row_t font_{ident}[FONT_LAST - FONT_FIRST + 1][FONT_CELL_H];

#endif /* {guard} */
""")

    with open(os.path.join(a.out, f"font_{ident}.c"), "w") as f:
        licence_ref = (f"\n   The full licence text is in {a.licence_file} (relative to this file).\n"
                       if a.licence_file else "")
        f.write(f"""/*
   Bitmap glyph data rasterised from the font below by ttf2cfont.py. This
   table is a derivative of that font and is distributed under the font's
   own licence, not the licence of the code around it.
{licence_ref}
   Source font (strings taken from the font file's name table):
{lic}
*/

""")
        f.write(f'#include "font_{ident}.h"\n\n')
        f.write(f"const font_row_t font_{ident}[FONT_LAST - FONT_FIRST + 1][FONT_CELL_H] = {{\n")
        names = {0x20: "space", 0x5C: "backslash", 0x27: "quote", 0x2A: "star", 0x2F: "slash"}
        for i, rows in enumerate(table):
            enc = a.first + i
            f.write(f"    /* 0x{enc:02X} {names.get(enc, chr(enc))} */ {{\n")
            for r in range(0, cell_h, 8):
                f.write("        " + ", ".join(f"0x{v:0{hexw}X}" for v in rows[r:r + 8]) + ",\n")
            f.write("    },\n")
        f.write("};\n")

    size = count * cell_h * ctype // 8
    print(f"em {em_px:.2f}px, advance {adv:.2f} -> cell {cell_w}x{cell_h} "
          f"(ink rows {ink_h}, baseline row {base}), {rowtype}, table {size} bytes")
    print(f"panel grid: {320 // cell_w} cols x {240 // cell_h} rows on 320x240")
    if blank:
        print("warning: no ink for: " + " ".join(f"0x{e:02X}" for e in blank))
    if clipped:
        print("warning: ink clipped outside cell for: "
              + " ".join(f"0x{e:02X}" for e in clipped[:20]))


if __name__ == "__main__":
    main()