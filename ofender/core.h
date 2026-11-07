#pragma once
#include <opxy/audio.hpp>

namespace ofender {
typedef opxy::i32 i32;
typedef opxy::u32 u32;

const u32 kArmors = 8;

// Valor Q31 (0..1) a índice de enum, igual que la cuantización nativa Q31->Q15.
inline u32 choice(i32 value, u32 count) {
    const u32 q15 = value < 0 ? 0u : static_cast<u32>(value) >> 16;
    const u32 index = (q15 * count) >> 15;
    return index < count ? index : count - 1;
}
inline i32 q15(i32 value) { return value < 0 ? 0 : value >> 16; }  // 0..32767

struct Voice {
    u32 phase;
    u32 age;             // bloques desde el inicio de esta repetición
    u32 finished;        // 1 cuando el sonido ya terminó (nota de un solo golpe)
    i32 soft;            // suavizado de los flancos del pulso
    i32 droop;           // integrador del paso alto: la caída de la onda, como en el sample
    u32 noise_phase, noise_lfsr;  // ruido del sonido `noise`
    u32 boom_pos;        // posición en la muestra del bombo
    u32 blast_phase, blast_inc, blast_lfsr;
    i32 blast_vol;       // volumen de la explosión (Q8), baja por bloques
    u32 crush_phase;     // Battery: reloj de muestreo (Q16)
    i32 crush_hold;      // Battery: última muestra tomada
};
struct Shared {};
}
