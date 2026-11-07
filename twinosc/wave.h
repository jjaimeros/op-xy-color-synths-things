#pragma once
#include "core.h"

// Formas de onda compartidas por el motor (engine.cpp) y la pantalla (display.cpp).
namespace twinosc {
typedef opxy::i32 i32;
typedef opxy::u32 u32;

const u32 kWaves = 3, kOctaves = 5;

// Valor Q31 (0..1) a índice de enum, igual que la cuantización nativa Q31->Q15.
inline u32 choice(i32 value, u32 count) {
    const u32 q15 = value < 0 ? 0u : static_cast<u32>(value) >> 16;
    const u32 index = (q15 * count) >> 15;
    return index < count ? index : count - 1;
}
inline i32 q15(i32 value) { return value < 0 ? 0 : value >> 16; }  // 0..32767

// Altura de un oscilador para todo el bloque: incremento en Q16 y su recíproco.
struct Rate { u32 dt16, rdt; };
inline Rate rate(u32 increment) {
    Rate r;
    r.dt16 = increment >> 16;
    r.rdt = r.dt16 ? (1u << 30) / r.dt16 : 0;  // 2^30 / dt
    return r;
}

// Corrección PolyBLEP en Q15 para el salto de una sierra, en la posición t (Q16).
inline i32 blep(u32 t, const Rate& r) {
    if (!r.rdt) return 0;
    if (t < r.dt16) {
        const i32 x = static_cast<i32>((t * r.rdt) >> 15);  // t/dt en Q15
        return (x << 1) - ((x * x) >> 15) - 32768;
    }
    if (t > 65535u - r.dt16) {
        const i32 y = static_cast<i32>(((65536u - t) * r.rdt) >> 15);  // (1-t)/dt en Q15
        const i32 k = 32768 - y;
        return (k * k) >> 15;
    }
    return 0;
}

// Sierra antialiasing en Q15 (-32768..32767).
inline i32 saw(u32 phase, const Rate& r) {
    const u32 t = phase >> 16;
    return static_cast<i32>(t) - 32768 - blep(t, r);
}

// Ajustes de forma de un oscilador, calculados una vez por bloque.
struct Shape {
    u32 wave;
    u32 offset;          // saw: desfase de la segunda sierra; sqr: ancho de pulso (fase)
    u32 peak;            // tri: posición del pico en Q16
    u32 inv_up, inv_dn;  // tri: 2^30 / subida, 2^30 / bajada
};
inline Shape shape_for(u32 wave, i32 amount) {
    Shape s;
    s.wave = wave;
    s.offset = 0; s.peak = 32768; s.inv_up = s.inv_dn = 0;
    const u32 a = static_cast<u32>(amount);  // 0..32767
    if (wave == 0) s.offset = a << 15;                         // 0 .. 1/2 ciclo
    else if (wave == 1) s.offset = 0x80000000u - a * 54384u;   // 50% .. ~8.5%
    else {
        s.peak = 32768u + ((a * 27525u) >> 15);                // 0.5 .. 0.92
        s.inv_up = (1u << 30) / s.peak;
        s.inv_dn = (1u << 30) / (65536u - s.peak);
    }
    return s;
}

// Una muestra en Q15. saw ±32768, sqr hasta ±60000 (sin componente continua), tri ±32768.
inline i32 oscillator(const Shape& s, u32 phase, const Rate& r) {
    if (s.wave == 0) {
        if (s.offset == 0) return saw(phase, r);
        return (saw(phase, r) + saw(phase + s.offset, r)) >> 1;
    }
    if (s.wave == 1) return saw(phase, r) - saw(phase - s.offset, r);
    const u32 t = phase >> 16;  // triángulo sin BLEP: sus esquinas casi no generan aliasing
    if (t < s.peak) return static_cast<i32>((t * s.inv_up) >> 14) - 32768;
    return 32767 - static_cast<i32>(((t - s.peak) * s.inv_dn) >> 14);
}

}
