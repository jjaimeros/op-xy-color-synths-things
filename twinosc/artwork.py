#!/usr/bin/env python3
"""Genera panel.svg.fragment de Twin Osc a partir del diseño del usuario
(~/Desktop/twinosc.svg): 4 columnas de 2 cuadrados, una bajo cada encoder.
   Osc 1 / Osc 2      onda elegida        Shape 1 / Shape 2  onda deformada
   Detune             líneas verdes        Octave             flechas
   Mix                barras azul/blanca   Sub                submarino amarillo
Los índices de nodos son los de display.h.  Uso: python3 artwork.py
"""
import pathlib

P = "twinosc"
HERE = pathlib.Path(__file__).resolve().parent
W, H, R = 85.3319, 64.9541, 6.7926
CELLS = {   # esquina de cada cuadrado, tal como está en el diseño
    "wave1": (16.0266, 52.5671), "wave2": (16.0133, 140.4772),
    "shape1": (137.607, 52.5671), "shape2": (136.9847, 140.4772),
    "detune": (257.6375, 52.5671), "octave": (257.2055, 140.4772),
    "mix": (377.0156, 52.5671), "sub": (377.2765, 140.4772),
}
NAVY, BROWN, GREY = "#31425F", "#856742", "#D1D3D4"
CREAM, GREEN, DARK = "#E8E3D3", "#1F9D55", "#191919"
BLUE, WHITE, WATER, YELLOW = "#2F6AA3", "#FFFFFF", "#2F6FB0", "#F2C230"
BAR_TOP, BAR_H = 63, 44


def n(v):
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


def center(name):
    x, y = CELLS[name]
    return x + W / 2, y + H / 2


def rounded(x, y, w, h, r):
    k = r * 0.5523
    return (f"M{n(x + r)},{n(y)} L{n(x + w - r)},{n(y)} "
            f"C{n(x + w - r + k)},{n(y)} {n(x + w)},{n(y + r - k)} {n(x + w)},{n(y + r)} "
            f"L{n(x + w)},{n(y + h - r)} "
            f"C{n(x + w)},{n(y + h - r + k)} {n(x + w - r + k)},{n(y + h)} {n(x + w - r)},{n(y + h)} "
            f"L{n(x + r)},{n(y + h)} "
            f"C{n(x + r - k)},{n(y + h)} {n(x)},{n(y + h - r + k)} {n(x)},{n(y + h - r)} "
            f"L{n(x)},{n(y + r)} "
            f"C{n(x)},{n(y + r - k)} {n(x + r - k)},{n(y)} {n(x + r)},{n(y)} Z")


def rect(x, y, w, h):
    return f"M{n(x)},{n(y)} L{n(x + w)},{n(y)} L{n(x + w)},{n(y + h)} L{n(x)},{n(y + h)} Z"


def poly(points):
    return "M" + " L".join(f"{n(x)},{n(y)}" for x, y in points) + " Z"


def square(name, fill, stroke=None):
    x, y = CELLS[name]
    s = stroke or fill
    return f'<path d="{rounded(x, y, W, H, R)}" fill="{fill}" stroke="{s}" stroke-width="1"/>'


def path(i, d, fill, stroke, width, origin=None):
    tr = f' transform="matrix(1 0 0 1 {n(origin[0])} {n(origin[1])})"' if origin else ""
    return f'<path id="{P}_node_{i}"{tr} d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{n(width)}"/>'


def solid(d, color):
    return f'<path d="{d}" fill="{color}" stroke="{color}" stroke-width="0.5"/>'


out = [f'<g id="{P}_panel">', (HERE / "header.svg.part").read_text().rstrip()]

# Cuadrados de fondo
out += [square("wave1", NAVY), square("wave2", NAVY), square("shape1", BROWN), square("shape2", BROWN),
        square("detune", GREY), square("octave", GREY)]

# 10..12 / 13..15 iconos de onda (saw, sqr, tri)
ICONS = [
    [(-24, 14), (-8, -14), (-8, 14), (8, -14), (8, 14), (24, -14)],
    [(-24, 14), (-24, -14), (-12, -14), (-12, 14), (0, 14), (0, -14), (12, -14), (12, 14), (24, 14), (24, -14)],
    [(-24, 14), (-12, -14), (0, 14), (12, -14), (24, 14)],
]
ICON_ROUND = 2.5  # radio de las aristas: el firmware no admite stroke-linejoin


def rounded_line(pts, r):
    """Línea abierta con cada arista interior sustituida por una curva pequeña."""
    d = f"M{n(pts[0][0])},{n(pts[0][1])}"
    for a, p, b in zip(pts, pts[1:], pts[2:]):
        def toward(q):
            dx, dy = q[0] - p[0], q[1] - p[1]
            length = (dx * dx + dy * dy) ** 0.5
            k = min(r, length / 2) / length
            return p[0] + dx * k, p[1] + dy * k
        s, e = toward(a), toward(b)
        c1 = (s[0] + (p[0] - s[0]) * .55, s[1] + (p[1] - s[1]) * .55)
        c2 = (e[0] + (p[0] - e[0]) * .55, e[1] + (p[1] - e[1]) * .55)
        d += (f" L{n(s[0])},{n(s[1])} C{n(c1[0])},{n(c1[1])} {n(c2[0])},{n(c2[1])} {n(e[0])},{n(e[1])}")
    return d + f" L{n(pts[-1][0])},{n(pts[-1][1])}"


for r, name in enumerate(("wave1", "wave2")):
    cx, cy = center(name)
    for k, pts in enumerate(ICONS):
        d = rounded_line([(cx + x, cy + y) for x, y in pts], ICON_ROUND)
        out.append(path(10 + 3 * r + k, d, "none", CREAM, 4))

# 0, 1 contornos mutables: la onda deformada (M + 63 L + Z, puntos distintos, sin relleno)
for i, name in enumerate(("shape1", "shape2")):
    pts = [(-32 + k, -1) for k in range(32)] + [(31 - k, 1) for k in range(32)]
    d = f"M{pts[0][0]} {pts[0][1]} " + " ".join(f"L{x} {y}" for x, y in pts[1:]) + " Z"
    out.append(path(i, d, "none", CREAM, 3.5, center(name)))  # el grosor lo da el trazo

# 16..21 líneas verdes del detune, centradas en (0,0): el código las separa
cx, cy = center("detune")
for k in range(6):
    out.append(path(16 + k, rect(-1.25, -22, 2.5, 44), GREEN, GREEN, 0.5, (cx, cy)))

# 22, 23 flechas de octava (apuntan arriba; el código gira las de bajada); 24 raya de octava 0
ARROW = [(0, -18), (14, -2), (5, -2), (5, 18), (-5, 18), (-5, -2), (-14, -2)]
cx, cy = center("octave")
for i in (22, 23):
    out.append(path(i, poly(ARROW), DARK, DARK, 0.5, (cx, cy)))
out.append(path(24, rect(-20, -2.5, 40, 5), DARK, DARK, 0.5, (cx, cy)))

# 25, 26 barras del mix: bajan y se esconden tras la tapa según su parte de la mezcla
cx, _ = center("mix")
for i, (dx, color) in zip((25, 26), ((-14, BLUE), (14, WHITE))):
    out.append(path(i, rect(cx + dx - 10, BAR_TOP, 20, BAR_H), color, color, 0.5))
x, y = CELLS["mix"]
out.append(solid(rect(x + 3, BAR_TOP + BAR_H, W - 6, BAR_H + 2), "#000000"))
out.append(square("mix", "none", "#DC4E34"))

# 27 agua del sub, a nivel máximo; el código la baja según Sub
x, y = CELLS["sub"]
wave = "".join(f"C{n(x + W * (i + .33) / 4)},{n(y)} {n(x + W * (i + .66) / 4)},{n(y + 3)} {n(x + W * (i + 1) / 4)},{n(y + 1.5)} "
               for i in range(4))
out.append(path(27, f"M{n(x)},{n(y + 1.5)} {wave}L{n(x + W)},{n(y + H)} L{n(x)},{n(y + H)} Z", WATER, WATER, 0.5))
# Tapa bajo el cuadrado y esquinas negras para que el agua siga el redondeo
out.append(solid(rect(x - 1, y + H, W + 2, 220 - (y + H)), "#000000"))
k = R * 0.5523
for cx_, cy_, sx, sy in ((x, y, 1, 1), (x + W, y, -1, 1), (x, y + H, 1, -1), (x + W, y + H, -1, -1)):
    out.append(solid(f"M{n(cx_)},{n(cy_ + sy * R)} C{n(cx_)},{n(cy_ + sy * (R - k))} {n(cx_ + sx * (R - k))},{n(cy_)} "
                     f"{n(cx_ + sx * R)},{n(cy_)} L{n(cx_ - sx)},{n(cy_ - sy)} Z", "#000000"))

# Submarino amarillo (fijo, delante del agua)
cx, cy = center("sub")
cy += 5
SUB = [
    (f'd="M{n(cx - 13)},{n(cy - 10)} L{n(cx + 13)},{n(cy - 10)} C{n(cx + 18.5)},{n(cy - 10)} {n(cx + 23)},{n(cy - 5.5)} {n(cx + 23)},{n(cy)} '
     f'C{n(cx + 23)},{n(cy + 5.5)} {n(cx + 18.5)},{n(cy + 10)} {n(cx + 13)},{n(cy + 10)} L{n(cx - 13)},{n(cy + 10)} '
     f'C{n(cx - 18.5)},{n(cy + 10)} {n(cx - 23)},{n(cy + 5.5)} {n(cx - 23)},{n(cy)} '
     f'C{n(cx - 23)},{n(cy - 5.5)} {n(cx - 18.5)},{n(cy - 10)} {n(cx - 13)},{n(cy - 10)} Z" fill="{YELLOW}" stroke="{YELLOW}" stroke-width="0.5"'),
    (f'd="M{n(cx - 8)},{n(cy - 9)} L{n(cx - 8)},{n(cy - 15)} C{n(cx - 8)},{n(cy - 18)} {n(cx - 6)},{n(cy - 20)} {n(cx - 3)},{n(cy - 20)} '
     f'L{n(cx + 2)},{n(cy - 20)} C{n(cx + 5)},{n(cy - 20)} {n(cx + 7)},{n(cy - 18)} {n(cx + 7)},{n(cy - 15)} L{n(cx + 7)},{n(cy - 9)} Z" fill="{YELLOW}" stroke="{YELLOW}" stroke-width="0.5"'),
    (f'd="M{n(cx + 2)},{n(cy - 20)} L{n(cx + 2)},{n(cy - 24)} L{n(cx + 8)},{n(cy - 24)}" fill="none" stroke="{YELLOW}" stroke-width="2"'),
]
# 28..34 submarino: el código mueve todas sus piezas a la vez
for i, attrs in enumerate(SUB):
    out.append(f'<path id="{P}_node_{28 + i}" {attrs}/>')
out.append(f'<circle id="{P}_node_31" cx="{n(cx - 26)}" cy="{n(cy)}" r="3.5" fill="{YELLOW}" stroke="{YELLOW}" stroke-width="0.5"/>')
for i, dx in enumerate((-11, 0, 11)):
    out.append(f'<circle id="{P}_node_{32 + i}" cx="{n(cx + dx)}" cy="{n(cy)}" r="3.2" fill="{NAVY}" stroke="{CREAM}" stroke-width="0.8"/>')
out.append(square("sub", "none", WHITE))

# Paleta obligatoria (el dibujo usa los colores propios de cada elemento)
out.append(f'<circle id="{P}_palette_0" cx="640" cy="0" r="1" fill="#000000" stroke="#000000" stroke-width="1"/>')
out.append("</g>")
(HERE / "panel.svg.fragment").write_text("\n".join(out) + "\n")
print("panel.svg.fragment listo")
