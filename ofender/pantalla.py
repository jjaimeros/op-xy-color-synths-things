#!/usr/bin/env python3
"""Genera pantalla.svg, una imagen de muestra de la pantalla para ver y editar en Inkscape.
Sale de panel.svg.fragment y de geometry.h, con el juego en un fotograma fijo. Uso: python3 pantalla.py
"""
import pathlib, re

HERE = pathlib.Path(__file__).resolve().parent
P = "ofender"
geo = (HERE / "geometry.h").read_text()
L, T, R, B = (int(re.search(rf"k{k} = (\d+)", geo).group(1)) for k in ("Left", "Top", "Right", "Bottom"))
STICK = tuple(int(re.search(rf"k{k} = (\d+)", geo).group(1)) / 1024 for k in ("StickX", "StickY"))
MISSILE = tuple(int(re.search(rf"k{k} = (\d+)", geo).group(1)) / 1024 for k in ("MissileX", "MissileY"))
BOMB = tuple(int(re.search(rf"k{k} = (\d+)", geo).group(1)) / 1024 for k in ("BombX", "BombY"))

# Mismas segmentaciones que display.cpp.
DIGITS = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F]
HEADER = [("armor", "arp"), ("battery", "100%"), ("boom", "100"), ("blast", "100")]
SHIP = (L + 21, T + 46)
SAMPLE = {0: (L - 59, B), 1: (L + 69, B), 2: SHIP, 3: (SHIP[0] - 3, SHIP[1] + 3), 4: (SHIP[0] + 18, SHIP[1] + 3),
          14: (L + 99, T + 11), 15: (L + 88, T + 28), 16: (L + 83, T + 47),
          36: STICK, 37: MISSILE, 38: MISSILE, 39: BOMB, 40: BOMB}
for i, digit in enumerate(f"{1250:05d}"):
    for k in range(7):
        if (DIGITS[int(digit)] >> k) & 1:
            SAMPLE[41 + i * 7 + k] = (L + 2 + i * 4, T + 2)

panel = (HERE / "panel.svg.fragment").read_text()
for index, (x, y) in SAMPLE.items():
    panel, count = re.subn(rf'(id="{P}_node_{index}" transform=")matrix\(1 0 0 1 [^)]*\)', rf"\g<1>matrix(1 0 0 1 {x:g} {y:g})", panel)
    assert count == 1, index

out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 220" width="960" height="440">',
       '<rect width="480" height="220" fill="#000000"/>']
for i, shade in enumerate(["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]):
    out.append(f'<rect x="{i * 60}" width="60" height="20" fill="{shade}"/>')
for i, (label, value) in enumerate(HEADER):
    color = "#CBCBCB" if i < 2 else "#000000"
    out.append(f'<text x="{i * 120 + 5}" y="10" font-family="Helvetica" font-size="10" fill="{color}">{label}</text>')
    out.append(f'<text x="{i * 120 + 65}" y="17.4" font-family="Helvetica" font-size="20" fill="{color}">{value}</text>')
out += [panel, "</svg>"]
(HERE / "pantalla.svg").write_text("\n".join(out) + "\n")
