#!/usr/bin/env python3
"""
Regenerate ../font_ui.c/h (Frugi's display font) from JetBrains Mono
(SIL OFL 1.1), drawn at Light weight.

Source: https://github.com/JetBrains/JetBrainsMono - the variable-weight
font, so the weight can be chosen (WEIGHT below).  On the 1-bit LCD:
    400 Regular    - mostly 2-pixel strokes, looks heavy
    300 Light      - mix of 1 and 2-pixel strokes (current choice)
    200 ExtraLight - almost all 1-pixel strokes, crisp but faint
All three fit the 16x24 cell at the same size.

Run from anywhere:  python3 jetbrains_mono/make_font.py
Requires Pillow and fontTools (pip install pillow fonttools).
"""
import os
import subprocess
import sys

WEIGHT = "400"

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.dirname(HERE)

subprocess.run([
    sys.executable, os.path.join(FONT_DIR, "ttf2cfont.py"),
    os.path.join(HERE, "JetBrainsMono-VariableFont_wght.ttf"),
    "--name", "ui",
    "--out", FONT_DIR,
    "--px", "22.96",        # largest size whose glyphs all fit a 16x24 cell
    "--cell-w", "16",
    "--cell-h", "24",
    "--weight", WEIGHT,
    "--licence-file", "jetbrains_mono/OFL.txt",
], check=True)
