#!/usr/bin/env python3
"""Genera panel.svg.fragment y geometry.h de Ofender a partir de cabina.svg (diseño del usuario).
La cabina se escala con la misma proporción en los dos ejes y se centra en la pantalla.
Los índices de nodos son los de display.h. Uso: python3 artwork.py
"""
import math, pathlib, re, xml.etree.ElementTree as ET

P = "ofender"
HERE = pathlib.Path(__file__).resolve().parent
TOP = 33.0   # 12 px de aire bajo la cabecera (edición del usuario)
SCALE = 190 / 554   # escala fija: la base del joystick ya no marca la altura de la cabina
SVG = "{http://www.w3.org/2000/svg}"


def n(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def arc(x0, y0, rx, ry, phi, large, sweep, x1, y1):
    """Arco SVG a puntos."""
    if rx == 0 or ry == 0: return [(x1, y1)]
    c, s = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x0 - x1) / 2, (y0 - y1) / 2
    xp, yp = c * dx + s * dy, -s * dx + c * dy
    k = xp ** 2 / rx ** 2 + yp ** 2 / ry ** 2
    if k > 1: rx, ry = rx * math.sqrt(k), ry * math.sqrt(k)
    a = rx ** 2 * ry ** 2 - rx ** 2 * yp ** 2 - ry ** 2 * xp ** 2
    b = rx ** 2 * yp ** 2 + ry ** 2 * xp ** 2
    f = math.sqrt(max(0, a / b)) * (-1 if large == sweep else 1)
    cxp, cyp = f * rx * yp / ry, -f * ry * xp / rx
    cx, cy = c * cxp - s * cyp + (x0 + x1) / 2, s * cxp + c * cyp + (y0 + y1) / 2
    t0 = math.atan2((yp - cyp) / ry, (xp - cxp) / rx)
    t1 = math.atan2((-yp - cyp) / ry, (-xp - cxp) / rx)
    if sweep and t1 < t0: t1 += 2 * math.pi
    if not sweep and t1 > t0: t1 -= 2 * math.pi
    steps = max(4, int(abs(t1 - t0) / (math.pi / 12)))
    return [(cx + rx * math.cos(t0 + (t1 - t0) * i / steps) * c - ry * math.sin(t0 + (t1 - t0) * i / steps) * s,
             cy + rx * math.cos(t0 + (t1 - t0) * i / steps) * s + ry * math.sin(t0 + (t1 - t0) * i / steps) * c)
            for i in range(1, steps + 1)]


def read_path(d):
    tokens = re.findall(r"[MmLlHhVvQqCcAaZz]|-?\d*\.?\d+(?:[eE][-+]?\d+)?", d)
    subs, x, y, cmd, i = [], 0.0, 0.0, None, 0
    while i < len(tokens):
        if tokens[i].isalpha():
            cmd = tokens[i]; i += 1
            if cmd in "Zz":
                subs[-1]["closed"] = True
                x, y = subs[-1]["points"][0]
            continue
        rel = cmd.islower(); c = cmd.upper()
        def num():
            nonlocal i
            i += 1
            return float(tokens[i - 1])
        if c == "H":
            x = num() + (x if rel else 0); subs[-1]["points"].append((x, y))
        elif c == "V":
            y = num() + (y if rel else 0); subs[-1]["points"].append((x, y))
        elif c in "ML":
            nx, ny = num() + (x if rel else 0), num() + (y if rel else 0)
            x, y = nx, ny
            if c == "M":
                subs.append({"points": [(x, y)], "closed": False})
                cmd = "l" if rel else "L"
            else:
                subs[-1]["points"].append((x, y))
        elif c == "Q":
            cx, cy = num() + (x if rel else 0), num() + (y if rel else 0)
            ex, ey = num() + (x if rel else 0), num() + (y if rel else 0)
            for k in range(1, 7):
                t = k / 6
                subs[-1]["points"].append(((1 - t) ** 2 * x + 2 * t * (1 - t) * cx + t * t * ex,
                                           (1 - t) ** 2 * y + 2 * t * (1 - t) * cy + t * t * ey))
            x, y = ex, ey
        elif c == "C":
            c1x, c1y = num() + (x if rel else 0), num() + (y if rel else 0)
            c2x, c2y = num() + (x if rel else 0), num() + (y if rel else 0)
            ex, ey = num() + (x if rel else 0), num() + (y if rel else 0)
            for k in range(1, 9):
                t = k / 8
                u = 1 - t
                subs[-1]["points"].append((u ** 3 * x + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t ** 3 * ex,
                                           u ** 3 * y + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t ** 3 * ey))
            x, y = ex, ey
        elif c == "A":
            rx, ry, phi, large, sweep = num(), num(), num(), num(), num()
            ex, ey = num() + (x if rel else 0), num() + (y if rel else 0)
            subs[-1]["points"] += arc(x, y, rx, ry, phi, large != 0, sweep != 0, ex, ey)
            x, y = ex, ey
    return subs


def ellipse(cx, cy, rx, ry):
    return [{"points": [(cx + rx * math.cos(2 * math.pi * i / 32), cy + ry * math.sin(2 * math.pi * i / 32))
                        for i in range(32)], "closed": True}]


def round_rect(x, y, w, h, r):
    pts = []
    for cx, cy, a in [(x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)]:
        pts += [(cx + r * math.cos(math.radians(a + 15 * k)), cy + r * math.sin(math.radians(a + 15 * k))) for k in range(7)]
    return [{"points": pts, "closed": True}]


def style_of(el):
    return dict(p.split(":", 1) for p in el.get("style", "").split(";") if ":" in p)


def shape_of(el):
    tag = el.tag.replace(SVG, "")
    f = lambda k, d=0.0: float(el.get(k, d))
    if tag == "path": return read_path(el.get("d"))
    if tag == "circle": return ellipse(f("cx"), f("cy"), f("r"), f("r"))
    if tag == "ellipse": return ellipse(f("cx"), f("cy"), f("rx"), f("ry"))
    if tag == "rect":
        r = min(f("rx"), f("height") / 2, f("width") / 2)
        if r: return round_rect(f("x"), f("y"), f("width"), f("height"), r)
        x, y, w, h = f("x"), f("y"), f("width"), f("height")
        return [{"points": [(x, y), (x + w, y), (x + w, y + h), (x, y + h)], "closed": True}]
    return None


root = ET.parse(HERE / "cabina.svg").getroot()
main = next(g for g in root if g.get("id") == "g68")
items = {}
for group in main:
    items[group.get("id")] = []
    for el in group:
        shape = shape_of(el)
        if shape:
            items[group.get("id")].append({"id": el.get("id"), "subs": shape, "style": style_of(el),
                                           "width": float(el.get("stroke-width", group.get("stroke-width", 1)))})

all_points = [p for g in items.values() for it in g for sub in it["subs"] for p in sub["points"]]
xs, ys = [p[0] for p in all_points], [p[1] for p in all_points]
S = SCALE
OX, OY = 240 - S * (max(xs) + min(xs)) / 2, TOP - S * min(ys)
mx = lambda x: OX + S * x
my = lambda y: OY + S * y


def d_of(subs, ox=0.0, oy=0.0, scale=S):
    out = []
    for sub in subs:
        pts, last = [], None
        for x, y in sub["points"]:
            p = (n(OX + scale * x - ox), n(OY + scale * y - oy))
            if p != last: pts.append(p)
            last = p
        if len(pts) < 2: continue
        out.append("M" + " L".join(f"{a},{b}" for a, b in pts) + (" Z" if sub["closed"] else ""))
    return " ".join(out)


def static(it, stroke=None, width=None):
    w = width if width is not None else max(0.4, it["width"] * S)
    fill = it["style"].get("fill", "none")
    c = stroke or it["style"].get("stroke", "#FFFFFF")
    return f'<path d="{d_of(it["subs"])}" fill="{fill.upper() if fill != "none" else "none"}" stroke="{c.upper()}" stroke-width="{n(w)}"/>'


def node(i, x, y, d, fill, stroke, w=0.2):
    return (f'<path id="{P}_node_{i}" transform="matrix(1 0 0 1 {n(x)} {n(y)})" d="{d}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="{n(w)}"/>')


def sprite(rows, px=1, ox=0, oy=0):
    """Pixel art: un rectángulo por racha de cada fila. Origen en la esquina superior izquierda."""
    out = []
    for r, row in enumerate(rows):
        c = 0
        while c < len(row):
            if row[c] == "#":
                e = c
                while e < len(row) and row[e] == "#": e += 1
                x0, x1, y0, y1 = ox + c * px, ox + e * px, oy + r * px, oy + (r + 1) * px
                out.append(f"M{x0},{y0} L{x1},{y0} L{x1},{y1} L{x0},{y1} Z")
                c = e
            else:
                c += 1
    return " ".join(out)


SHIP = ["...#......", "##.##.....", ".########.", "##########", ".########.", "##.##.....", "...#......"]
ENEMIES = [
    ["..###..", ".#####.", "#######", ".#.#.#."],
    ["....###", "..####.", "#####..", "..####.", "....###"],
    ["#.....#", ".#####.", "##.#.##", ".#####.", "#.....#"],
]
FLAME = sprite(["###", ".##"])
LIFE = [".#...", "####.", "#####", "####.", ".#..."]   # mininave de las vidas
# Marcador: cifras de 3 x 5 px hechas con 7 segmentos, en el orden a, b, c, d, e, f, g.
SEGMENTS = [(0, 0, 3, 1), (2, 0, 1, 3), (2, 2, 1, 3), (0, 4, 3, 1), (0, 2, 1, 3), (0, 0, 1, 3), (0, 2, 3, 1)]

# Montañas: perfil escalonado que empalma consigo mismo cada kMountainWidth píxeles.
PEAKS = [3, 5, 8, 11, 14, 12, 9, 6, 4, 6, 9, 13, 10, 7, 5, 3, 5, 7, 9, 12, 14, 11, 8, 5, 3, 4, 6, 10, 13, 9, 6, 4]
STEP = 4
HEIGHTS = [PEAKS[i // 2] if i % 2 == 0 else (PEAKS[i // 2] + PEAKS[(i // 2 + 1) % len(PEAKS)]) // 2 for i in range(2 * len(PEAKS))]
HEIGHTS = HEIGHTS[:31]
HEIGHTS[-1] = HEIGHTS[0] + 1
HEIGHTS.append(HEIGHTS[0] + 1)
HEIGHTS = [max(0, h - 4) for h in HEIGHTS]   # montañas 4 px más bajas y separadas (edición del usuario)
MOUNTAIN_W = STEP * len(HEIGHTS)

screen = next(it for it in items["graficos-pantalla"] if it["id"] == "path20")
L, T = round(mx(229)), round(my(119))
R, B = round(mx(571)), round(my(345))
assert MOUNTAIN_W >= R - L + 2, (MOUNTAIN_W, R - L)

mountain = "M0,0 L" + " L".join(f"{x},{-h}" for k, h in enumerate(HEIGHTS) for x in (k * STEP, (k + 1) * STEP)) + f" L{MOUNTAIN_W},0 Z"

NODES = dict(mountain=0, ship=2, flame=3, shots=4, bombs=12, enemies=14, particles=20, stick=36,
             missile=37, missile_glint=38, bomb=39, bomb_glint=40, score=41)
SHOTS, BOMBS, ENEMY_SLOTS, PARTICLES = 8, 2, 6, 16
SCORE_DIGITS = 5
TOTAL = 76
PALETTE = [("#FFFFFF", "#FFFFFF", 0.2), ("#FF2A2A", "#FF2A2A", 0.2), ("#B80F1E", "#B80F1E", 0.2),
           ("#FF8A1F", "#FF8A1F", 0.2), ("#FFD34D", "#FFD34D", 0.2),
           ("#FFC300", "#FFFFFF", 0.5), ("#A87F00", "#FFFFFF", 0.5)]

out = [f'<g id="{P}_panel">']
# Rejilla apagada, debajo del juego.
for it in items["rejilla"]:
    green = "28" in it["style"].get("stroke", "")
    out.append(static(it, "#10500A" if green else "#2A2A2A", 0.5))
# Juego. Nacen ocultos; el primer fotograma los coloca.
HX, HY = 640, 320
for i in (0, 1):
    out.append(node(NODES["mountain"] + i, HX, HY, mountain, "#FFFFFF", "#FFFFFF"))
for i in range(SHOTS):
    out.append(node(NODES["shots"] + i, HX, HY, "M0,0 L4,0 L4,1 L0,1 Z", "#FFFFFF", "#FFFFFF"))
for i in range(BOMBS):
    out.append(node(NODES["bombs"] + i, HX, HY, "M0,0 L3,0 L3,3 L0,3 Z", "#FFFFFF", "#FFFFFF"))
for i in range(ENEMY_SLOTS):
    out.append(node(NODES["enemies"] + i, HX, HY, sprite(ENEMIES[i % 3]), "#FF2A2A", "#FF2A2A"))
out.append(node(NODES["flame"], HX, HY, FLAME, "#FF8A1F", "#FF8A1F"))
out.append(node(NODES["ship"], HX, HY, sprite(SHIP), "#FFFFFF", "#FFFFFF"))
for i in range(PARTICLES):
    out.append(node(NODES["particles"] + i, HX, HY, "M0,0 L1,0 L1,1 L0,1 Z", "#FFD34D", "#FFD34D"))
# Vidas: dos mininaves arriba a la derecha, a la altura del marcador.
for x in (R - 14, R - 7):
    out.append(f'<path d="{sprite(LIFE, 1, x, T + 2)}" fill="#FFFFFF" stroke="#FFFFFF" stroke-width="0.2"/>')
# Marcador, encima del juego.
for i in range(SCORE_DIGITS * len(SEGMENTS)):
    x, y, w, h = SEGMENTS[i % len(SEGMENTS)]
    out.append(node(NODES["score"] + i, HX, HY, f"M{x},{y} L{x + w},{y} L{x + w},{y + h} L{x},{y + h} Z", "#FFFFFF", "#FFFFFF"))
# Máscaras negras: lo que entra y sale del juego queda tapado por los lados y por abajo.
for x0, y0, x1, y1 in [(0, T - 8, L, B + 8), (R, T - 8, 480, B + 8), (L, B, R, B + 8)]:
    out.append(f'<path d="M{x0},{y0} L{x1},{y0} L{x1},{y1} L{x0},{y1} Z" fill="#000000" stroke="#000000" stroke-width="0.2"/>')
# Cabina del usuario, encima.
for group in ["carcasa", "pantalla", "graficos-pantalla", "mandos", "rotulos", "joystick"]:
    for it in items[group]:
        if it["id"] in {"path66", "path67", "ellipse67", "ellipse68", "circle47", "circle48", "circle49", "path49", "path50"}:
            continue
        out.append(static(it))
by_id = {it["id"]: it for g in items.values() for it in g}
# Palanca: un solo nodo con origen en su base.
PX, PY = mx(346), my(506)


def clockwise(subs):
    """Mismo sentido en todas las piezas: en un solo path, sentidos opuestos dejarían huecos."""
    out = []
    for sub in subs:
        p = sub["points"]
        area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(p, p[1:] + p[:1]))
        out.append({"points": p if area > 0 else p[::-1], "closed": True})
    return out


stick = " ".join(d_of(clockwise(by_id[k]["subs"]), PX, PY) for k in ("path66", "path67", "ellipse67", "ellipse68"))
out.append(node(NODES["stick"], PX, PY, stick, "#FFC300", "#FFFFFF", 0.6))
# Botones: cara y brillo con origen en el centro, para hundirlos al pulsar.
MX, MY = mx(486.6), my(474)
BX, BY = mx(584.8), my(474)
out.append(node(NODES["missile"], MX, MY, d_of(by_id["circle47"]["subs"], MX, MY), "#FFC300", "#FFFFFF", 0.5))
out.append(node(NODES["missile_glint"], MX, MY, d_of(by_id["path49"]["subs"], MX, MY), "none", "#FFFFFF", 0.6))
out.append(node(NODES["bomb"], BX, BY, d_of(by_id["circle48"]["subs"], BX, BY), "#FFC300", "#FFFFFF", 0.5))
out.append(node(NODES["bomb_glint"], BX, BY, d_of(by_id["circle49"]["subs"], BX, BY) + " " + d_of(by_id["path50"]["subs"], BX, BY), "none", "#FFFFFF", 0.6))
for i, (fill, stroke, w) in enumerate(PALETTE):
    out.append(f'<circle id="{P}_palette_{i}" cx="640" cy="0" r="1" fill="{fill}" stroke="{stroke}" stroke-width="{n(w)}"/>')
out.append("</g>")
(HERE / "panel.svg.fragment").write_text("\n".join(out) + "\n")

q = lambda v: round(v * 1024)
geometry = f"""// Generado por artwork.py: no editar.
#pragma once
#include <opxy/types.hpp>

namespace ofender {{
const opxy::i32 kLeft = {L}, kTop = {T}, kRight = {R}, kBottom = {B};   // ventana del juego (px)
const opxy::i32 kMountainWidth = {MOUNTAIN_W}, kMountainStep = {STEP}, kMountainPeak = {max(HEIGHTS)};
const opxy::i32 kStickX = {q(PX)}, kStickY = {q(PY)};                  // Q10 px
const opxy::i32 kMissileX = {q(MX)}, kMissileY = {q(MY)}, kBombX = {q(BX)}, kBombY = {q(BY)};
const opxy::u8 kHeights[{len(HEIGHTS)}] = {{{", ".join(map(str, HEIGHTS))}}};
}}
"""
(HERE / "geometry.h").write_text(geometry)
print(f"escala {S:.3f}; ventana del juego {R - L} x {B - T} px en ({L},{T}); {len(out)} elementos; {TOTAL} nodos")
