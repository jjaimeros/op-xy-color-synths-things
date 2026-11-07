#pragma once
#include <opxy/page_header.hpp>
#include "geometry.h"

namespace raver {
// Coincide con resources.display de definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<0, 0, 44, 16, 12>;

const opxy::u32 kFixtures = 3, kBeams = 7, kSources = 4, kWaves = 3;
// Un rayo: sale al azar, entra y sale con fundido.
struct Beam {
    opxy::i16 level;  // 0 = apagado, 1024 = entero (Q10)
    opxy::i16 hold;   // ms que le quedan encendido; al llegar a 0 se funde
    opxy::i16 angle;  // desvío de este disparo, en décimas de grado
};
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::u32 started, serial, random;
    opxy::i32 linger;    // tiempo que sigue encendido tras una nota corta (Q10 s)
    opxy::u32 tick;      // reloj propio con el transporte parado (Q10 de 96 por negra)
    opxy::u32 step;      // último paso de semicorchea
    opxy::i32 opening;   // fundido de las ondas del modo sub (Q10)
    Beam beams[kFixtures * kBeams];
};
namespace node {
const opxy::u32 bars = 0, beams = 8, heads = 29, waves = 32;
}
}
