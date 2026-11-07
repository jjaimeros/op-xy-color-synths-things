#!/usr/bin/env python3
"""Genera tables.h de Acidgun: tablas en coma fija (el Blackfin no tiene coma flotante)."""
import math
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
FS = 44100.0
BLOCK = 64

lines = ["#pragma once", "// Generado por tables.py: no editar a mano.", "#include <opxy/audio.hpp>",
         "namespace acidgun {"]


def table(name, ctype, values, per_line=8):
    lines.append(f"const {ctype} {name}[{len(values)}] = {{")
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(str(v) + ("u" if ctype.endswith("u32") else "")
                                         for v in values[i:i + per_line]) + ",")
    lines.append("};")


# Filtro TB-303 (modelo TeeBeeFilter de Open303, Robin Schmidt: escalera de diodos de 4 polos
# acoplados, ajustada a medidas de una 303). Por posición f = 10 Hz * 2^(i/16), hasta 9 kHz:
#   b0 (Q31) coeficiente de las etapas, k (Q24) realimentación a resonancia plena.
b0s, ks = [], []
for i in range(193):
    f = min(10.0 * 2 ** (i / 16), 9000.0)  # tope: más arriba los coeficientes salen del ajuste
    fx = f / FS * math.sqrt(0.5)
    b0 = (0.00045522346 + 6.1922189 * fx) / (1.0 + 12.358354 * fx + 4.4156345 * fx * fx)
    k = fx * (fx * (fx * (fx * (fx * (fx + 7198.6997) - 5837.7917) - 476.47308) + 614.95611) + 213.87126) + 16.998792
    b0s.append(round(b0 * 2147483647))
    ks.append(round(k * 2 ** 24))
table("kB0", "opxy::i32", b0s)
table("kK", "opxy::i32", ks)
# Resonancia con la curva de Open303: (1 - e^(-3 r)) / (1 - e^(-3)), Q15.
table("kReso", "opxy::i32", [round((1 - math.exp(-3 * i / 32)) / (1 - math.exp(-3)) * 32767) for i in range(33)])

# 2^(i/64) / 2 en Q31 (u32: el último es 2^31).
table("kExp2", "opxy::u32", [round(2 ** (i / 64) * 2 ** 30) for i in range(65)])
# log2(1 + i/64) en Q16.
table("kLog2", "opxy::i32", [round(math.log2(1 + i / 64) * 65536) for i in range(65)])

# Decay de la envolvente del filtro por bloque: tau de 100 ms a 2,5 s (la 303: 200 ms a 2 s), Q31.
dec = []
for i in range(33):
    tau = 0.1 * (25 ** (i / 32))
    dec.append(round(math.exp(-BLOCK / (tau * FS)) * 2147483647))
table("kDecay", "opxy::i32", dec)

# Slide por bloque: tiempo (3 tau) por tramos 0, 40, 150, 600 ms (los anchors de definition.json), Q31.
ANCH = [0, 0.040, 0.150, 0.600]
sl = []
for i in range(33):
    x = i / 32 * 3
    j = min(int(x), 2)
    tau = (ANCH[j] + (ANCH[j + 1] - ANCH[j]) * (x - j)) / 3  # el valor mostrado = llegar al ~95 %
    sl.append(round(math.exp(-BLOCK / (tau * FS)) * 2147483647) if tau > 0 else 0)
table("kSlide", "opxy::i32", sl)

lines.append("}")
(HERE / "tables.h").write_text("\n".join(lines) + "\n")
print("tables.h escrito")
