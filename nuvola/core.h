#pragma once
#include <opxy/audio.hpp>

namespace nuvola {
const opxy::u32 kMaxClouds = 5;

// Una nota: hasta 5 osciladores al unísono (las nuvolas), cada uno con su deriva.
struct Voice {
    opxy::u32 phase[kMaxClouds];
    opxy::i32 drift[kMaxClouds];   // deriva propia de cada nube (Q16 de cents: 946 = 25 cents)
    opxy::i32 target[kMaxClouds];  // hacia dónde deriva
    opxy::u32 random;              // generador (xorshift)
    opxy::u32 tick;                // bloques desde el arranque
    opxy::i32 gain;                // ganancia de la compensación del bloque anterior (Q12)
    opxy::u32 dt16[kMaxClouds];    // último incremento de cada nube y su inverso: dividir sale caro
    opxy::u32 rdt[kMaxClouds];     // en el Blackfin, y casi nunca cambian de un bloque a otro
};
// Estado compartido por todas las voces; vacío porque no comparten nada.
struct Shared {};
}
