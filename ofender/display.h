#pragma once
#include <opxy/draw.hpp>
#include <opxy/page_header.hpp>
#include "geometry.h"

namespace ofender {
// Coincide con resources.display de definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<0, 0, 76, 0, 7>;

const opxy::u32 kArmorNames = 8;
const opxy::u32 kShots = 8, kBombs = 2, kEnemies = 6, kParticles = 16;
const opxy::u32 kScoreDigits = 5, kDigitSegments = 7;
struct Shot { opxy::i32 x, y; };                 // x == 0: libre
struct Bomb { opxy::i32 x, y, vy; };             // x == 0: libre
struct Enemy { opxy::i32 x, y; opxy::i16 vx; opxy::u16 wobble; };  // x == 0: libre
struct Particle {
    opxy::i32 x, y;
    opxy::i16 vx, vy;      // px/s
    opxy::i16 life;        // ms; 0 = libre
    opxy::u8 shade, size;  // color y tamaño (Q4: 16 = 1 px)
};
struct DisplayState {
    opxy::u32 started, serial, random;
    opxy::u32 last_bomb;    // la última nota soltó una bomba (se alterna con los misiles)
    opxy::u32 score;        // 10 puntos por enemigo; vuelve a 0 al pasar de 99990
    opxy::i32 coast;        // inercia al soltar (Q10 s)
    opxy::i32 clock;        // reloj del juego (Q10 s, envuelve)
    opxy::i32 press_m, press_b;  // tiempo que quedan hundidos los botones (Q10 s)
    opxy::i32 pending, gap; // disparos pendientes de la ráfaga y tiempo hasta el siguiente
    opxy::i32 ship_y;       // Q10 px
    opxy::i32 tilt;         // inclinación de la palanca (Q10 px)
    opxy::i32 scroll;       // desplazamiento de las montañas (Q10 px)
    opxy::i32 shake;        // sacudida del bombo (Q10 px)
    opxy::i32 spawn;        // tiempo hasta la siguiente nave enemiga (Q10 s)
    Shot shots[kShots];
    Bomb bombs[kBombs];
    Enemy enemies[kEnemies];
    Particle particles[kParticles];
};
namespace node {
const opxy::u32 mountain = 0, ship = 2, flame = 3, shots = 4, bombs = 12, enemies = 14, particles = 20;
const opxy::u32 stick = 36, missile = 37, missile_glint = 38, bomb = 39, bomb_glint = 40;
const opxy::u32 score = 41;  // 7 segmentos por cifra
}
namespace shade {
const opxy::u32 white = 0, red = 1, dark_red = 2, orange = 3, yellow = 4, button = 5, button_down = 6;
}
}
