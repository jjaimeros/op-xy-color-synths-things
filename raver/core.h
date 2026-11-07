#pragma once
#include <opxy/audio.hpp>

namespace raver {
const opxy::u32 kSaws = 7;
// Una nota: 7 osciladores libres (el modo sub solo usa el primero), el seno de la página 2,
// el generador de ruido, la envolvente y el estado del filtro.
struct Voice {
    opxy::u32 phase[kSaws];
    opxy::u32 sine, random;
    opxy::i32 env, low, band;
};
struct Shared {};
}
