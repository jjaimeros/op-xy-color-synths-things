#pragma once
#include <opxy/page_header.hpp>

namespace twinosc {
// Coincide con resources.display de definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<2, 64, 35, 16, 1>;
struct DisplayState { opxy::u32 phase; opxy::draw::PageHeader header; };

// Nodos del artwork (generado por artwork.py; mantener en sincronía).
namespace node {
const opxy::u32 shape1 = 0, shape2 = 1;   // contornos mutables
const opxy::u32 header = 2;               // 2..9 cabecera de dos páginas
const opxy::u32 icons = 10;               // 10..12 osc 1, 13..15 osc 2 (saw, sqr, tri)
const opxy::u32 lines = 16;               // 16..21 líneas verdes del detune
const opxy::u32 arrow1 = 22, arrow2 = 23, flat = 24;  // octava
const opxy::u32 bar1 = 25, bar2 = 26;     // mix: azul (osc 1) y blanca (osc 2)
const opxy::u32 water = 27;
const opxy::u32 submarine = 28, submarine_parts = 7;  // 28..34 casco, torreta, periscopio, hélice, 3 ojos de buey
}
}
