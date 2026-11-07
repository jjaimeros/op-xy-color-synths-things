#!/usr/bin/env python3
"""Genera tables.h de Ofender: seno, ritmo del ruido de `noise` y la muestra del bombo (boom.wav)."""
import math, pathlib, struct, wave
HERE = pathlib.Path(__file__).resolve().parent

# Boom: boom.wav a 16 bits, recortado a 0,30 s con 10 ms de salida (después solo queda un resto inaudible).
with wave.open(str(HERE / "boom.wav")) as w:
    assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (1, 3, 44100)
    raw = w.readframes(w.getnframes())
frames = [int.from_bytes(raw[i:i + 3], "little", signed=True) >> 8 for i in range(0, len(raw), 3)]
keep, tail = int(0.30 * 44100), int(0.01 * 44100)
boom = frames[:keep]
for i in range(tail):
    boom[keep - tail + i] = boom[keep - tail + i] * (tail - i) // tail

sine = [round(32767 * math.sin(2 * math.pi * i / 256)) for i in range(256)]

# Ruido de "Sound 8": cruces por cero medidos cada 10 ms, pasados a pasos de 8 bloques (11,6 ms).
# Unidad: reloj del ruido (el doble de los cruces) dividido por 440 Hz, en Q4.
crossings = [1450, 2700, 300, 300, 650, 800, 1850, 1550, 3000, 4250, 3400, 2050, 4650, 4850, 4800,
             4950, 3750, 2800, 2000, 800, 1250, 1850, 3050]
rate = []
for k in range(20):
    t = k * 8 * 64 / 44100 / 0.01
    a = crossings[min(int(t), len(crossings) - 1)]
    rate.append(max(4, round(a * 2 / 440 * 16)))


def table(name, kind, values, per=16):
    rows = [", ".join(map(str, values[i:i + per])) for i in range(0, len(values), per)]
    return f"const {kind} {name}[{len(values)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n"


text = ("// Generado por tables.py: no editar.\n#pragma once\n#include <opxy/types.hpp>\n\nnamespace ofender {\n"
        + table("kSine", "opxy::i16", sine, 12)
        + table("kNoiseRate", "opxy::u16", rate)
        + f"const opxy::u32 kBoomFrames = {len(boom)};\n"
        + table("kBoom", "opxy::i16", boom, 20) + "}\n")
(HERE / "tables.h").write_text(text)
print(f"boom: {len(boom)} muestras, {2 * len(boom) / 1024:.1f} KiB; pico {max(map(abs, boom))}")
