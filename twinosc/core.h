#pragma once
#include <opxy/audio.hpp>

namespace twinosc {
// Fases de 32 bits: 0..2^32 es un ciclo completo.
struct Voice {
    opxy::u32 phase1, phase2, phase_sub;
    // Divisiones guardadas: en el Blackfin salen caras y casi nunca cambian de un bloque a otro.
    opxy::u32 dt16[3], rdt[3];                    // inversos del incremento: osc 1, osc 2, sub
    opxy::i32 tri_amount[2];                      // Shape con el que se calculó el triángulo (-1: ninguno)
    opxy::u32 tri_peak[2], tri_up[2], tri_dn[2];
};
// Estado compartido por todas las voces; vacío porque no comparten nada.
struct Shared {};
}
