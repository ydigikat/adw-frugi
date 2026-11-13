#!/usr/bin/env python3
"""
Regenerate ../font_ui.c/h (Frugi's display font) from Noto Sans Mono Regular (SIL OFL 1.1).

Source: https://github.com/notofonts/latin-greek-cyrillic - a lighter
weight than JetBrains Mono.

Run from anywhere:  python3 noto_sans_mono/make_font.py
Requires Pillow and fontTools (pip install pillow fonttools).
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.dirname(HERE)

subprocess.run([
    sys.executable, os.path.join(FONT_DIR, "ttf2cfont.py"),
    os.path.join(HERE, "NotoSansMono-Regular.ttf"),
    "--name", "ui",
    "--out", FONT_DIR,
    "--px", "24.42",        # largest size whose glyphs all fit a 16x24 cell
    "--cell-w", "16",
    "--cell-h", "24",
    "--licence-file", "noto_sans_mono/OFL.txt",
], check=True)
