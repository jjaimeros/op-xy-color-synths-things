#!/usr/bin/env python3
"""Genera panel.svg.fragment de Nuvola: nubes, lluvia de golosina, sol con luz, viento y pájaros.

Nodos (ver display.h):
  0..7    nubes (una por oscilador), centradas en (0,0): el código las coloca
  8..31   nubecitas de golosina que caen al tocar
  32      sol: círculo        33  sol: destellos (12 rayitas cortas)
  34..37  ráfagas de viento (línea con remolino en la punta izquierda)
  38..43  pájaros: 38 + 2j alas arriba, 39 + 2j alas abajo
Ejecutar con python3.12 (f-strings anidadas).
"""
import math
import pathlib

P = "nuvola"
HERE = pathlib.Path(__file__).resolve().parent
BLUE = "#5B86B8"
CLOUD_FILL = "#000000"   # negro opaco: las nubes se tapan entre sí, tapan la luz y la lluvia que nace
SUN = "#F2C230"
LIGHT = "#FFE08A"
WIND = "#C9D8E8"
BIRD = "#E6E0D2"
PINK, WHITE, HOT = "#F5A3C7", "#FFF4F8", "#EE6FA8"


def n(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def mix(a, b, t):
    ca = [int(a[i:i + 2], 16) for i in (1, 3, 5)]
    cb = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y - x) * t):02X}" for x, y in zip(ca, cb))


# Paleta: índice = "shade" del código.
#  0 negro, 1-2 nubes (normal, al tocar), 3 sol, 4-7 sin uso (reserva), 8-11 viento de tenue a pleno,
#  12 pájaros, 13-15 golosina (rosa, blanco, rosa fuerte).
PALETTE = [("#000000", 1), (BLUE, 2), ("#8FB4DE", 2), (SUN, 1.25)]
PALETTE += [(mix("#000000", LIGHT, (k + 1) / 4), 1) for k in range(4)]
PALETTE += [(mix("#000000", WIND, (k + 1) / 4), 1.25) for k in range(4)]
PALETTE += [(BIRD, 1.5), (PINK, 1), (WHITE, 1), (HOT, 1)]
assert len(PALETTE) == 16

# Nube redondeada (44 x 26), centrada en (0,0)
CLOUD = ("M-14,11 C-21,11 -24,4 -19,0 C-21,-8 -12,-12 -6,-8 C-3,-15 9,-15 11,-7 "
         "C17,-10 24,-4 20,2 C25,5 22,11 16,11 Z")
# Nubecita de golosina (~13 x 9): tres bolitas esponjosas
PUFF = "M-4,3 C-7,3 -7,-1 -4,-1 C-4,-4 0,-5 1,-2 C2,-5 6,-4 5,-1 C8,-1 8,3 5,3 Z"

nodes = {}
for k in range(8):
    nodes[k] = (f'<path id="{P}_node_{k}" transform="matrix(1 0 0 1 {60 + 40 * k} 80)" d="{CLOUD}" '
                f'fill="{CLOUD_FILL}" stroke="{BLUE}" stroke-width="2"/>')
for i in range(8, 32):
    nodes[i] = (f'<path id="{P}_node_{i}" transform="matrix(1 0 0 1 640 320)" d="{PUFF}" '
                f'fill="{PINK}" stroke="{PINK}" stroke-width="1"/>')
# Sol: círculo de trazo fino y 12 destellos cortos; el código lo escala con Sun.
nodes[32] = (f'<circle id="{P}_node_32" transform="matrix(1 0 0 1 424 92)" cx="0" cy="0" r="14" '
             f'fill="none" stroke="{SUN}" stroke-width="1.25"/>')
glints = []
for j in range(12):
    a = math.pi * 2 * j / 12
    glints.append(f"M{n(19 * math.sin(a))},{n(-19 * math.cos(a))} L{n(25 * math.sin(a))},{n(-25 * math.cos(a))}")
nodes[33] = (f'<path id="{P}_node_33" transform="matrix(1 0 0 1 424 92)" d="{" ".join(glints)}" '
             f'fill="none" stroke="{SUN}" stroke-width="1.25"/>')
# Ráfagas: línea larga y ondulada con un remolino en la punta (el viento va a la izquierda).
GUSTS = [
    "M64,0 C50,-3 38,3 24,0 C14,-2 6,-1 2,-5 C-1,-9 4,-13 8,-10 C10,-8 8,-6 6,-7",
    "M48,0 C36,3 26,-3 14,0 C6,2 0,-1 -1,-5 C-2,-9 3,-11 6,-8",
    "M60,0 C46,2 34,-2 20,0 C10,1 3,-2 1,-6 C0,-10 5,-12 8,-9 C9,-7 7,-6 5,-7",
    "M40,0 C30,-2 20,2 10,0 C4,-1 0,-3 0,-6 C0,-9 4,-10 6,-8",
]
for j, d in enumerate(GUSTS):
    nodes[34 + j] = (f'<path id="{P}_node_{34 + j}" transform="matrix(1 0 0 1 640 320)" d="{d}" '
                     f'fill="none" stroke="{WIND}" stroke-width="1.25"/>')
# Pájaros: una "m" abierta, alas arriba y alas abajo.
WINGS = ["M-7,-3 C-5,-6 -2,-5 0,0 C2,-5 5,-6 7,-3", "M-7,3 C-5,1 -2,-1 0,0 C2,-1 5,1 7,3"]
for j in range(3):
    for f, d in enumerate(WINGS):
        i = 38 + 2 * j + f
        nodes[i] = (f'<path id="{P}_node_{i}" transform="matrix(1 0 0 1 640 320)" d="{d}" '
                    f'fill="none" stroke="{BIRD}" stroke-width="1.5"/>')
NODE_COUNT = 44

out = [f'<g id="{P}_panel">']
out += [nodes[i] for i in (32, 33)]               # sol, al fondo
out += [nodes[i] for i in range(8, 32)]           # golosina, bajo las nubes (nace de ellas)
out += [nodes[k] for k in range(8)]               # nubes (relleno negro: se tapan entre sí)
out += [nodes[i] for i in range(34, 44)]          # viento y pájaros, delante de todo
out += [f'<circle id="{P}_palette_{i}" cx="640" cy="0" r="1" fill="{c}" stroke="{c}" stroke-width="{n(w)}"/>'
        for i, (c, w) in enumerate(PALETTE)]
out.append("</g>")
assert sorted(nodes) == list(range(NODE_COUNT))
(HERE / "panel.svg.fragment").write_text("\n".join(out) + "\n")
print(f"panel.svg.fragment: {NODE_COUNT} nodos, {len(PALETTE)} colores")
