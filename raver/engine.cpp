#include "core.h"

// Raver: supersaw de rave.
//   Página 1: Wave (saw, sqr o sub: seno una octava abajo, un oscilador y sin detune),
//             Detune (curva y desplazamientos del JP-8000, análisis publicado de Adam Szabo),
//             Filter Env (el centro del pote vale 25: curva x^2), Filter.
//   Página 2: Noise, Sine (seno una octava abajo que se suma), Reso, Filter.
// Filtro paso bajo de 2 polos (Chamberlin) iterado 2 veces por muestra, para que sea estable
// hasta arriba. Sin antialiasing por ahora, y fases libres: cada nota empieza distinta.
// Coma fija de 32 bits: el Blackfin no tiene FPU ni multiplicación de 64 bits.
namespace raver {
typedef opxy::i32 i32;
typedef opxy::u32 u32;

// Desafinación relativa de cada onda a Detune máximo (Q15).
const i32 kOffset[kSaws] = {-3605, -2061, -640, 0, 652, 2037, 3521};
// Curva de Detune (Q15), 33 puntos de 0 a 1: casi plana al principio y disparada al final.
const i32 kCurve[33] = {98, 324, 340, 497, 749, 997, 1199, 1367, 1537, 1732, 1954, 2185, 2405, 2603, 2785, 2975, 3209, 3519, 3925, 4428, 5015, 5665, 6366, 7121, 7944, 8837, 9755, 10590, 11213, 11705, 12917, 17641, 32761};
// Corte del filtro: 40 Hz a 16,6 kHz, coeficiente Q13 de 2 sin(pi fc / 2 fs).
const i32 kCutoff[65] = {23, 26, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 72, 79, 87, 96, 105, 116, 127, 140, 154, 169, 186, 204, 224, 246, 270, 297, 327, 359, 394, 433, 476, 523, 575, 631, 694, 762, 837, 920, 1011, 1111, 1220, 1341, 1473, 1618, 1777, 1952, 2144, 2354, 2585, 2838, 3115, 3418, 3750, 4113, 4509, 4941, 5412, 5923, 6478, 7077, 7722, 8413, 9150};
// Mezcla fija: onda central y laterales (Q15).
const i32 kCenter = 23626, kSide = 16450;
const i32 kGain = 7282;   // 1 / 4,5: la suma de las 7 ondas cabe en el fondo de escala
const i32 kDecay = 32609;  // por bloque (1,45 ms): la envolvente cae a 1/e en unos 300 ms
const i32 kLimit = 131072;  // tope de los estados del filtro (cuatro veces el fondo de escala)

inline u32 next(u32& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
inline i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
inline i32 q15(i32 value) { return value < 0 ? 0 : value >> 16; }
inline i32 curve(i32 x) {
    const i32 at = x >> 10, frac = x & 1023;
    const i32 low = kCurve[at], high = kCurve[at < 32 ? at + 1 : 32];
    return low + (((high - low) * frac) >> 10);
}
inline i32 cutoff(i32 x) {
    const i32 at = x >> 9, frac = x & 511;
    const i32 low = kCutoff[at], high = kCutoff[at < 64 ? at + 1 : 64];
    return low + (((high - low) * frac) >> 9);
}
// Seno por parábola corregida (Q15).
inline i32 sine(u32 phase) {
    const i32 a = static_cast<i32>(phase >> 16) - 32768;
    const i32 y = (a * (32768 - (a < 0 ? -a : a))) >> 13;
    return y + ((7373 * (((y * (y < 0 ? -y : y)) >> 15) - y)) >> 15);
}

inline void initialize(Voice& v, Shared&, u32 owner) {
    u32 random = 0x6C8E9CF5u ^ (owner * 0x9E3779B9u);
    if (!random) random = 1;
    for (u32 k = 0; k < kSaws; ++k) v.phase[k] = next(random);
    v.sine = next(random);
    v.random = random;
    v.env = 0; v.low = 0; v.band = 0;
}
inline void start(Voice& v, Shared&, u32) { v.env = 32767; }  // fases libres, como el original
inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& v, Shared&, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch pitch) {
    const u32 picked = (static_cast<u32>(q15(parameters[0])) * 3u) >> 15;
    const u32 wave = picked > 2u ? 2u : picked;
    i32 mixed[opxy::block_frames];
    if (wave == 2) {
        u32 ph = v.phase[0];
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            mixed[i] = sine(ph);
            ph += pitch[i] >> 1;
        }
        v.phase[0] = ph;
    } else {
        const i32 amount = curve(q15(parameters[1]));
        for (unsigned i = 0; i < opxy::block_frames; ++i) mixed[i] = 0;
        const u32 base = pitch[0] > 0x7fffffffu ? 0x7fffffffu : pitch[0];
        for (u32 k = 0; k < kSaws; ++k) {
            const i32 relative = (kOffset[k] * amount) >> 15;
            const i32 delta = (static_cast<i32>(base >> 12) * relative) >> 3;
            const i32 level = k == 3 ? kCenter : kSide;
            u32 ph = v.phase[k];
            if (wave == 0) {
                for (unsigned i = 0; i < opxy::block_frames; ++i) {
                    mixed[i] += ((static_cast<i32>(ph >> 16) - 32768) * level) >> 15;
                    ph += pitch[i] + static_cast<u32>(delta);
                }
            } else {
                for (unsigned i = 0; i < opxy::block_frames; ++i) {
                    mixed[i] += ((ph & 0x80000000u ? 32767 : -32768) * level) >> 15;
                    ph += pitch[i] + static_cast<u32>(delta);
                }
            }
            v.phase[k] = ph;
        }
        for (unsigned i = 0; i < opxy::block_frames; ++i) mixed[i] = (mixed[i] * kGain) >> 15;
    }

    // Página 2: ruido y seno una octava abajo, antes del filtro.
    const i32 noise = q15(parameters[4]), added = q15(parameters[5]);
    if (noise) {
        u32 r = v.random;
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            mixed[i] += ((static_cast<i32>(next(r) >> 16) - 32768) * noise) >> 15;
        }
        v.random = r;
    }
    u32 ph = v.sine;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        if (added) mixed[i] += (sine(ph) * added) >> 15;
        ph += pitch[i] >> 1;
    }
    v.sine = ph;

    // Filtro: Filter marca el corte y la envolvente lo abre hacia arriba desde ahí.
    const i32 lpf = q15(parameters[3]), raw = q15(parameters[2]);
    const i32 depth = (raw * raw) >> 15;  // el centro del pote vale 25
    const i32 position = lpf + ((depth * v.env) >> 15);
    const i32 f = cutoff(position > 32767 ? 32767 : position);
    const i32 q = 9830 - ((q15(parameters[6]) * 8847) >> 15);  // Q13: de 1,2 a 0,12
    v.env = (v.env * kDecay) >> 15;
    i32 low = v.low, band = v.band;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 x = mixed[i];
        for (unsigned pass = 0; pass < 2; ++pass) {
            low = clamp(low + ((f * band) >> 13), -kLimit, kLimit);
            const i32 high = clamp(x - low - ((q * band) >> 13), -kLimit, kLimit);
            band = clamp(band + ((f * high) >> 13), -kLimit, kLimit);
        }
        // Limitador suave: y = 1,5x - 0,5x^3 con x = señal / 3 (Q15).
        const i32 s = (clamp(low, -98304, 98304) * 10923) >> 15;
        const i32 s3 = (((s * s) >> 15) * s) >> 15;
        output[i] = (s + (s >> 1) - (s3 >> 1)) * 32768;
    }
    v.low = low; v.band = band;
}
}
