#pragma once
#include <opxy/page_header.hpp>

namespace acidgun {
// Coincide con resources.display de definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<3, 64, 39, 16, 16>;
const opxy::u32 kSparks = 24;

struct Spark {
    opxy::i32 x, y;         // Q10 px
    opxy::i16 vx, vy;       // px/s
    opxy::i16 life, total;  // vida restante y total (ms); 0 = libre
    opxy::u8 shade, size;   // color y tamaño (Q4: 16 = escala 1)
    opxy::u16 spin;         // giro (décimas de grado)
};
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::u32 serial;     // última nota vista
    opxy::u32 random;
    opxy::u32 key;        // clave de geometría de los paths del láser
    opxy::i32 shot;       // tiempo desde el último disparo (Q10 s); -1 = sin disparo
    opxy::i32 env;        // envolvente visual (Q10, 0..1)
    opxy::i32 release;    // tiempo desde que se soltó (Q10 s); -1 = sonando
    opxy::i32 recoil;     // retroceso (Q10 px)
    opxy::i32 bob;        // desplazamiento vertical de la pistola (Q10 px)
    opxy::i32 bob_from, bob_to, bob_t;  // tramo del slide: origen, destino, tiempo (Q10)
    opxy::i32 bob_dir;    // alterna arriba / abajo
    opxy::i32 sparkle;    // chispas pendientes (Q10)
    opxy::i32 clock;      // reloj de animación (Q10 s, envuelve)
    opxy::i32 flash_turn; // giro del fogonazo
    Spark sparks[kSparks];
};
namespace node {
const opxy::u32 beam = 0, mid = 1, core = 2;  // tres contornos del láser
const opxy::u32 header = 3;   // 3..10
const opxy::u32 body = 11, detail = 12, tube = 13, flash = 14;
const opxy::u32 sparks = 15;  // 15..38
}
namespace shade {
const opxy::u32 red = 1, blue = 4;            // + 0 borde, + 1 capa media, + 2 núcleo
const opxy::u32 yellow = 7, white = 8, orange = 9, spark_red = 10, spark_blue = 11;
const opxy::u32 red_dim = 12, blue_dim = 13, red_dim_mid = 14, blue_dim_mid = 15;
}
}
