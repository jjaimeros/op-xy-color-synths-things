#pragma once
#include <opxy/audio.hpp>

namespace acidgun {
struct Voice {
    opxy::u32 phase;
    opxy::i32 y[4];           // etapas del filtro de diodos (Q22)
    opxy::i32 env, envs;      // envolvente del filtro y la misma suavizada (ataque de 3 ms), Q31
    opxy::i32 glide;          // desvío de afinación que queda del slide (Q16 de octava)
    opxy::i32 fb, fb_x;       // paso alto de 150 Hz en la realimentación (Q22)
    opxy::i32 hp, hp_in;      // drive: paso alto de entrada (Q26)
    opxy::i32 lp;             // drive: paso bajo de salida (Q28)
    opxy::i32 dc_in, dc_out;  // drive: condensador de salida
    opxy::i32 here;           // log2 del incremento al final del bloque anterior (Q16): detecta legato
    opxy::u32 fresh;          // 1 hasta el primer bloque de la nota
    opxy::u32 dt16, rdt;      // último incremento y su inverso: dividir sale caro
};
// Mono, como la 303: solo suena la última nota (lead). Cada voz nueva recoge el estado de
// la anterior (fase, filtro, drive): el cambio de nota es continuo, un solo oscilador.
struct Shared {
    Voice snap;           // estado de la voz que sonaba al final de su último bloque
    opxy::i32 last_log;   // log2 del incremento que suena (Q16), con el slide
    opxy::u32 lead;       // voz de la última nota (0 = ninguna)
    opxy::u32 valid;
};
}
