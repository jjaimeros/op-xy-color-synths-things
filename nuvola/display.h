#pragma once
#include <opxy/draw.hpp>

namespace nuvola {
// Coincide con resources.display de definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<0, 0, 44, 0, 16>;
const opxy::u32 kDrops = 24;

// Una nubecita de golosina que cae.
struct Drop {
    opxy::i32 x, y;     // Q10 px
    opxy::u16 speed;    // px/s
    opxy::u8 shade;     // color (0 = libre)
    opxy::u8 sway;      // fase del balanceo
};
struct DisplayState {
    opxy::u32 time;       // reloj del vaivén de las nubes al tocar (Q10 s)
    opxy::u32 serial;     // última nota vista
    opxy::u32 random;     // generador para la lluvia
    opxy::i32 rain;       // gotas pendientes de soltar (Q10)
    opxy::i32 wind;       // último Wind visto (Q15); -1 = aún sin ver
    opxy::i32 gust;       // fuerza de la racha de viento (Q10, 0..1)
    opxy::i32 gust_clock; // tiempo desde la última racha (Q10 s)
    opxy::i32 drift;      // primer suavizado del empuje (Q10 px)
    opxy::i32 push;       // empuje de las nubes hacia la izquierda (Q10 px, <= 0)
    opxy::i32 streaks;    // recorrido de las ráfagas dibujadas (Q10 px)
    opxy::i32 flight;     // posición de la bandada (Q10 px)
    opxy::i32 spin;       // giro de los destellos (Q10 décimas de grado)
    Drop drops[kDrops];
};
namespace node {
const opxy::u32 clouds = 0;   // 0..7
const opxy::u32 drops = 8;    // 8..31 nubecitas de golosina
const opxy::u32 sun = 32, sun_glints = 33;
const opxy::u32 wind = 34;    // 34..37 ráfagas
const opxy::u32 birds = 38;   // pájaro j: 38 + 2j alas arriba, 39 + 2j alas abajo
}
namespace shade {
const opxy::u32 cloud = 1, cloud_glow = 2, sun = 3;
const opxy::u32 wind = 8;     // 8..11 viento, de tenue a pleno
const opxy::u32 bird = 12;
const opxy::u32 candy = 13;   // 13 rosa, 14 blanco, 15 rosa fuerte
}
}
