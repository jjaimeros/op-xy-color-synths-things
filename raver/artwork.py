#!/usr/bin/env python3
"""Genera panel.svg.fragment y geometry.h de Raver. Uso: python3 artwork.py
Diseño del usuario: un resplandor del que nacen los rayos (su aplastado de los rayos está en
display.cpp). Su opacidad se convierte en gris sobre negro y su desenfoque en elipses
anidadas, porque el firmware no anuncia ninguna de las dos cosas. Los índices son los de display.h.
"""
import math, pathlib

P = "raver"
HERE = pathlib.Path(__file__).resolve().parent
FIXTURE_X, FIXTURE_Y = (211, 240, 274), 91  # donde nacen los rayos del usuario
# De aquí salen las ondas del modo sub: donde estaban los conos, bajo el resplandor.
WAVE_X, WAVE_Y = (211.4, 230.46, 249.53, 268.59), 113.2


def n(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


out = [f'<g id="{P}_panel">']

# Resplandor del usuario: una elipse blanca al 50 % con desenfoque de 3,2 px. Se imita con
# elipses anidadas, de fuera hacia dentro, cuyo gris sube como el borde de ese desenfoque.
GLOW_X, GLOW_Y, GLOW_RX, GLOW_RY, GLOW_SD, GLOW_RINGS, GLOW_POINTS = 240.86, 94.9, 68.28, 20.36, 3.18, 7, 40
for ring in range(GLOW_RINGS):
    offset = GLOW_SD * 2.2 * (1 - 2 * ring / (GLOW_RINGS - 1))
    edge = 0.5 * (1 + math.erf(-offset / (GLOW_SD * math.sqrt(2))))
    level = round(0x80 * edge)
    color = f"#{level:02X}{level:02X}{level:02X}"
    points = [f"{GLOW_X + (GLOW_RX + offset) * math.cos(2 * math.pi * k / GLOW_POINTS):.1f},"
              f"{GLOW_Y + (GLOW_RY + offset) * math.sin(2 * math.pi * k / GLOW_POINTS):.1f}" for k in range(GLOW_POINTS)]
    out.append(f'<path d="M{" L".join(points)} Z" fill="{color}" stroke="{color}" stroke-width="0.3"/>')

# Ondas del modo sub: arcos delanteros de elipse, como vistos sobre el suelo, que display.cpp
# agranda desde cada cono. Cada cono tiene su tramo, de fuera hacia el centro y sin pisar el del
# vecino: así las ondas no se cruzan. Son cintas cerradas: el aparato no admite líneas sin área.
WAVE_RX, WAVE_RY, WAVE_THICK, WAVE_POINTS = 200, 85, 1.0, 12
SECTORS = ((177, 137), (133, 92), (88, 47), (43, 3))  # grados: 0 = derecha, 90 = hacia el público


def arc(start, end, rx, ry):
    steps = [start + (end - start) * k / (WAVE_POINTS - 1) for k in range(WAVE_POINTS)]
    return [f"{rx * math.cos(math.radians(a)):.1f},{ry * math.sin(math.radians(a)):.1f}" for a in steps]


for wave in range(3):
    for cone, (start, end) in enumerate(SECTORS):
        outer = arc(start, end, WAVE_RX, WAVE_RY)
        inner = arc(end, start, WAVE_RX - WAVE_THICK, WAVE_RY - WAVE_THICK)
        out.append(f'<path id="{P}_node_{32 + wave * 4 + cone}" transform="matrix(1 0 0 1 240 113)" '
                   f'd="M{" L".join(outer + inner)} Z" fill="#FFFFFF" stroke="#FFFFFF" stroke-width="0.2"/>')

# Rayos: triángulos que apuntan a +x desde el foco y se abren al acercarse. display.cpp los
# escala para que acaben justo en el borde de la pantalla.
beam = "M0,-0.3 L300,-1.9 L300,1.9 L0,0.3 Z"
for i in range(21):
    out.append(f'<path id="{P}_node_{8 + i}" transform="matrix(1 0 0 1 10 10)" d="{beam}" fill="#ACFFCA" stroke="#00FF5C" stroke-width="1"/>')
for i in range(3):
    out.append(f'<circle id="{P}_node_{29 + i}" transform="matrix(1 0 0 1 10 10)" cx="0" cy="0" r="2" fill="#FFFFFF" stroke="#FFFFFF" stroke-width="0.2"/>')

# Paleta: cada color tenue, medio y entero, para el fundido de los rayos. Verde, azul y
# violeta son los del usuario. Orden: display.cpp, paint.
COLORS = [("#ACFFCA", "#00FF5C"), ("#ACD3FF", "#56A5FF"), ("#E2ABFF", "#A600FF"), ("#FFFFFF", "#FFFFFF")]


def dim(color, factor):
    return "#" + "".join(f"{round(int(color[i:i + 2], 16) * factor):02X}" for i in (1, 3, 5))


entry = 0
for fill, stroke in COLORS:
    for factor in (0.33, 0.66, 1.0):
        width = 0.2 if fill == "#FFFFFF" else 1  # las ondas son cintas finas: casi sin borde
        out.append(f'<circle id="{P}_palette_{entry}" cx="10" cy="10" r="1" fill="{dim(fill, factor)}" stroke="{dim(stroke, factor)}" stroke-width="{width}"/>')
        entry += 1

# Cabecera de dos filas del SDK, encima de los rayos para que no la tapen.
out.append('<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>')
for i, c in enumerate(["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]):
    out.append(f'<path id="{P}_node_{i}" transform="matrix(1 0 0 1 {i * 60} 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="{c}" stroke="{c}" stroke-width="0.5"/>')
TEXTS = [(5, 10, "#CBCBCB", 10), (125, 10, "#CBCBCB", 10), (245, 10, "#000000", 10), (365, 10, "#000000", 10),
         (65, 17.4, "#CBCBCB", 20), (185, 17.4, "#CBCBCB", 20), (305, 17.4, "#000000", 20), (425, 17.4, "#000000", 20),
         (5, 33, "#5A5A5A", 10), (125, 33, "#5A5A5A", 10), (245, 33, "#5A5A5A", 10), (365, 33, "#5A5A5A", 10),
         (65, 33, "#707070", 13), (185, 33, "#707070", 13), (305, 33, "#707070", 13), (425, 33, "#707070", 13)]
for i, (x, y, c, size) in enumerate(TEXTS):
    out.append(f'<text id="{P}_text_{i}" transform="matrix(1 0 0 1 {x} {y})" fill="{c}" font-family="UniversNextPro-Light" font-size="{size}px">-</text>')

out.append("</g>")
(HERE / "panel.svg.fragment").write_text("\n".join(out) + "\n")

geometry = ["#pragma once", "#include <opxy/types.hpp>", "", "// Generado por artwork.py: no editar a mano.", "namespace raver {",
            f"const opxy::i32 kFixtureX[3] = {{{', '.join(map(str, FIXTURE_X))}}};",
            f"const opxy::i32 kFixtureY = {FIXTURE_Y};",
            f"const opxy::i32 kWaveX[4] = {{{', '.join(str(round(x * 1024)) for x in WAVE_X)}}};",
            f"const opxy::i32 kWaveY = {round(WAVE_Y * 1024)};", "}", ""]
(HERE / "geometry.h").write_text("\n".join(geometry))
print("fragmento:", (HERE / "panel.svg.fragment").stat().st_size, "bytes")
