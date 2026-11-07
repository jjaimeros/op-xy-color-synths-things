#!/usr/bin/env python3
"""Genera panel.svg.fragment de Acidgun: la pistola, el láser y las chispas.

Nodos (ver display.h):
  0..2    láser: tres contornos anidados (borde, capa media, núcleo), paths mutables de
          64 puntos. El OP-XY no rellena paths mutables: el trazo grueso hace de relleno.
  3..10   cabecera de dos páginas del SDK (header.svg.part)
  11      pistola: cuerpo        12  pistola: detalles (estrías, paneles)
  13      tubo                   14  fogonazo en la boca del cañón
  15..38  chispas
La pistola sale de pistola.svg (editable en Inkscape: piezas tubo, cuerpo y detalles); su origen
es la boca del cañón, para que el código la mueva con el láser. Solo admite líneas rectas.
Ejecutar con python3.12.
"""
import math
import pathlib
import re
import xml.etree.ElementTree as ET

P = "acidgun"
HERE = pathlib.Path(__file__).resolve().parent
HOME = (196, 104)        # boca del cañón en pantalla (reposo)


def n(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def read_path(d):
    """Convierte un path de Inkscape (M L H V Z, absolutos o relativos) a subpaths absolutos."""
    tokens = re.findall(r"[MmLlHhVvZz]|-?\d*\.?\d+(?:[eE]-?\d+)?", d)
    subs, x, y, start, cmd, i = [], 0.0, 0.0, (0.0, 0.0), None, 0
    while i < len(tokens):
        if tokens[i].isalpha():
            cmd = tokens[i]
            i += 1
            if cmd in "Zz":
                subs[-1]["closed"] = True
                x, y = start
            continue
        rel = cmd.islower()
        if cmd in "Hh":
            x = float(tokens[i]) + (x if rel else 0)
            i += 1
        elif cmd in "Vv":
            y = float(tokens[i]) + (y if rel else 0)
            i += 1
        elif cmd in "MmLl":
            dx, dy = float(tokens[i]), float(tokens[i + 1])
            x, y = dx + (x if rel else 0), dy + (y if rel else 0)
            i += 2
        else:
            raise ValueError(f"orden de path no admitida: {cmd}")
        if cmd in "Mm":
            start = (x, y)
            subs.append({"points": [], "closed": False})
            cmd = "l" if rel else "L"
        subs[-1]["points"].append((x, y))
    return subs


def write_path(subs):
    out = []
    for sub in subs:
        if len(sub["points"]) < 3 and sub["closed"]:
            continue  # pieza aplastada en Inkscape: sin área, no se dibuja
        d = "M" + " L".join(f"{n(x)},{n(y)}" for x, y in sub["points"])
        out.append(d + (" Z" if sub["closed"] else ""))
    return " ".join(out)


def read_piece(root, name):
    """Geometría y colores de una pieza; el style de Inkscape manda sobre los atributos."""
    el = root.find(f".//*[@id='{name}']")
    style = dict(item.split(":", 1) for item in el.get("style", "").split(";") if ":" in item)
    color = lambda key: (style.get(key) or el.get(key)).upper()
    return write_path(read_path(el.get("d"))), color("fill"), color("stroke")


SOURCE = ET.parse(HERE / "pistola.svg").getroot()
tube, TUBE_FILL, TUBE_LINE = read_piece(SOURCE, "tubo")
body, BODY_FILL, BODY_LINE = read_piece(SOURCE, "cuerpo")
detail, _, DETAIL = read_piece(SOURCE, "detalles")

# Fogonazo: estrella de 8 puntas, centrada.
flash = []
for k in range(16):
    a = math.pi * 2 * k / 16
    r = 10 if k % 2 == 0 else 3.5
    flash.append(f"{n(r * math.cos(a))},{n(r * math.sin(a))}")
FLASH = "M" + " L".join(flash) + " Z"
SPARK = "M0,-4 L1,-1 L4,0 L1,1 L0,4 L-1,1 L-4,0 L-1,-1 Z"

# Paleta: índice = "shade" del código. Los anchos de trazo se doblan: el láser va a escala 2.
PALETTE = [
    ("#000000", 1),
    ("#E8142E", 1.5),   # 1 rojo láser: borde
    ("#FF5468", 1.5),   # 2 rojo: capa media
    ("#FFD9D9", 1.5),   # 3 rojo: núcleo
    ("#1A3DFF", 1.5),   # 4 azul intenso: borde
    ("#5A80FF", 1.5),   # 5 azul: capa media
    ("#DCE6FF", 1.5),   # 6 azul: núcleo
    ("#FFD34D", 1),     # 7 chispa amarilla
    ("#FFFFFF", 1),     # 8 chispa blanca
    ("#FF8A1F", 1),     # 9 chispa naranja
    ("#FF5A6A", 1),     # 10 chispa roja
    ("#7FA6FF", 1),     # 11 chispa azul
    ("#70101C", 1.5),   # 12 rojo apagándose
    ("#0E1C78", 1.5),   # 13 azul apagándose
    ("#A8303E", 1.5),   # 14 rojo medio apagándose
    ("#3048A8", 1.5),   # 15 azul medio apagándose
]
assert len(PALETTE) == 16

# Paths mutables: 64 puntos distintos (una elipse fina), sin relleno.
beam_d = "M" + " L".join(f"{n(40 * math.cos(math.pi * 2 * i / 64))},{n(2 * math.sin(math.pi * 2 * i / 64))}"
                         for i in range(64)) + " Z"

out = [f'<g id="{P}_panel">']
out += (HERE / "header.svg.part").read_text().rstrip("\n").split("\n")
hx, hy = HOME
for i, c in enumerate(["#E8142E", "#FF5468", "#FFD9D9"]):
    out.append(f'<path id="{P}_node_{i}" transform="matrix(1 0 0 1 640 320)" d="{beam_d}" '
               f'fill="none" stroke="{c}" stroke-width="1.5"/>')
out.append(f'<path id="{P}_node_13" transform="matrix(1 0 0 1 {hx} {hy})" d="{tube}" '
           f'fill="{TUBE_FILL}" stroke="{TUBE_LINE}" stroke-width="1"/>')
out.append(f'<path id="{P}_node_11" transform="matrix(1 0 0 1 {hx} {hy})" d="{body}" '
           f'fill="{BODY_FILL}" stroke="{BODY_LINE}" stroke-width="1"/>')
out.append(f'<path id="{P}_node_12" transform="matrix(1 0 0 1 {hx} {hy})" d="{detail}" '
           f'fill="none" stroke="{DETAIL}" stroke-width="1"/>')
out.append(f'<path id="{P}_node_14" transform="matrix(1 0 0 1 640 320)" d="{FLASH}" '
           f'fill="#FFD9D9" stroke="#E8142E" stroke-width="1"/>')
for i in range(15, 39):
    out.append(f'<path id="{P}_node_{i}" transform="matrix(1 0 0 1 640 320)" d="{SPARK}" '
               f'fill="#FFD34D" stroke="#FFD34D" stroke-width="0.5"/>')
out += [f'<circle id="{P}_palette_{i}" cx="640" cy="0" r="1" fill="{c}" stroke="{c}" stroke-width="{n(w)}"/>'
        for i, (c, w) in enumerate(PALETTE)]
out.append("</g>")
(HERE / "panel.svg.fragment").write_text("\n".join(out) + "\n")
print("panel.svg.fragment: 39 nodos, 16 colores")
