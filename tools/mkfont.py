#!/usr/bin/env python3
"""Génère firmware/fonts.h : polices bitmap LISSÉES (alpha 4 bits) et proportionnelles, depuis Inter (SIL OFL 1.1).

   python3 tools/mkfont.py [dossier_des_otf]

Chaque police : hauteur de cellule commune H, et par caractère une avance (largeur) + un bitmap avance x H en
4 bits par pixel (2 pixels par octet). Le firmware mélange couleur du texte / fond selon l'alpha (16 niveaux).
"""
import sys, os
from PIL import Image, ImageDraw, ImageFont

SRC = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/opentype/inter"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware", "fonts.h")
ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
FONTS = [   # nom C, fichier, taille px, jeu de caractères, espacement supplémentaire
    ("FT_SM", "Inter-SemiBold.otf", 12, ASCII, 0),
    ("FT_MD", "Inter-Bold.otf", 19, ASCII, 0),
    ("FT_LG", "Inter-Bold.otf", 72, " #-ABCDEFG", 0),
]

def build(name, fname, size, chars, track):
    f = ImageFont.truetype(os.path.join(SRC, fname), size)
    asc, desc = f.getmetrics()
    # boîte verticale commune : de la ligne la plus haute à la plus basse sur tout le jeu de caractères
    top, bot = 10**9, -10**9
    for ch in chars:
        bb = f.getbbox(ch)
        if bb[2] > bb[0]:
            top, bot = min(top, bb[1]), max(bot, bb[3])
    if top > bot: top, bot = 0, asc + desc
    H = bot - top
    glyphs = []
    for ch in chars:
        adv = int(round(f.getlength(ch))) + track
        if adv < 1: adv = 1
        im = Image.new("L", (adv + 4, H), 0)
        ImageDraw.Draw(im).text((0, -top), ch, font=f, fill=255)
        im = im.crop((0, 0, adv, H))
        px = [min(15, (v + 8) >> 4) for v in im.getdata()]     # alpha 0..15
        if len(px) % 2: px.append(0)
        data = bytes((px[i] << 4) | px[i + 1] for i in range(0, len(px), 2))
        glyphs.append((ch, adv, data))
    return H, glyphs

lines = ["/* GÉNÉRÉ par tools/mkfont.py — ne pas éditer. Police Inter (Rasmus Andersson), SIL Open Font License 1.1,",
         " * voir firmware/fonts/LICENSE-Inter.txt. Bitmaps lissés 4 bits/pixel (2 pixels par octet, poids fort à gauche). */",
         "#pragma once", "#include <stdint.h>",
         "typedef struct { uint8_t h; const char *chars; uint8_t n; const uint8_t *adv; const uint32_t *off; const uint8_t *bits; } op_font;",
         ""]
total = 0
for name, fname, size, chars, track in FONTS:
    H, gl = build(name, fname, size, chars, track)
    bits = b"".join(g[2] for g in gl)
    offs, o = [], 0
    for g in gl: offs.append(o); o += len(g[2])
    total += len(bits)
    esc = chars.replace("\\", "\\\\").replace('"', '\\"')
    lines.append(f"/* {name} : {fname} {size}px, hauteur {H} px, {len(gl)} caractères, {len(bits)} octets */")
    lines.append(f"static const uint8_t {name}_adv[{len(gl)}] = {{{', '.join(str(g[1]) for g in gl)}}};")
    lines.append(f"static const uint32_t {name}_off[{len(gl)}] = {{{', '.join(map(str, offs))}}};")
    hexs = ", ".join(f"0x{b:02X}" for b in bits)
    lines.append(f"static const uint8_t {name}_bits[{len(bits)}] = {{{hexs}}};")
    lines.append(f'static const op_font {name} = {{ {H}, "{esc}", {len(gl)}, {name}_adv, {name}_off, {name}_bits }};')
    lines.append("")
    print(f"{name}: {size}px Inter -> hauteur {H}px, {len(gl)} glyphes, {len(bits)} octets")
open(OUT, "w").write("\n".join(lines))
print(f"-> {os.path.normpath(OUT)} ({total} octets de glyphes)")
