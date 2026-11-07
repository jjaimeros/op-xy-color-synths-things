#include "core.h"

// Nuvola: sinte de nubes. Cada nube es un oscilador al unísono.
//
// Controles:
//   0 Waveform  saw, sqr, pulse, sine, saw+sqr
//   1 Nuvolas   1..5 osciladores al unísono
//   2 Wind      detune: 0..50 cents entre la nube más grave y la más aguda
//   3 Sun       drift: cada nube se desafina sola, despacio y al azar (hasta ±15 cents)
//
// Volumen constante con detune: las nubes desafinadas baten entre sí, y cada armónico
// de la suma sube y baja según se alinean o se cancelan sus fases (con poco detune son
// olas lentas: picos de volumen). Como el motor conoce la fase de cada nube, calcula en
// cada bloque la energía real de los 4 primeros armónicos de la suma (|suma de e^(i h fase)|^2,
// ponderada por el peso de cada armónico en la onda) y compensa justo esa energía. El
// timbre sigue moviéndose (el chorus), el volumen no. Un limitador suave remata.
//
// Todo en coma fija de 32 bits y sin divisiones por muestra (el Blackfin no
// tiene FPU ni multiplicación de 64 bits por hardware).
namespace nuvola {
typedef opxy::i32 i32;
typedef opxy::u32 u32;

const u32 kWaves = 5;

inline u32 choice(i32 value, u32 count) {
    const u32 q = value < 0 ? 0u : static_cast<u32>(value) >> 16;
    const u32 index = (q * count) >> 15;
    return index < count ? index : count - 1;
}
inline i32 q15(i32 value) { return value < 0 ? 0 : value >> 16; }
inline i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }

// Datos de una nube para su bloque. Sierra con antialiasing PolyBLEP (Q15).
// dt16: incremento Q16; rdt = 2^30 / dt16. Fuera de la zona de un salto basta una
// comparación (t - dt16 <= span); dentro, las dos ramas de la corrección.
struct Rate {
    u32 dt16, rdt, span;  // span: anchura de la zona sin saltos de la sierra
    u32 edge, edge_span;  // lo mismo para la cuadrada, con saltos cada media vuelta
};
inline i32 saw_edge(u32 t, const Rate& r) {
    i32 v = static_cast<i32>(t) - 32768;
    if (t < r.dt16) {
        const i32 x = static_cast<i32>((t * r.rdt) >> 15);
        v -= (x << 1) - ((x * x) >> 15) - 32768;
    } else if (t > 65535u - r.dt16) {
        const i32 y = static_cast<i32>(((65536u - t) * r.rdt) >> 15), k = 32768 - y;
        v -= (k * k) >> 15;
    }
    return v;
}
inline i32 saw(u32 phase, const Rate& r) {
    const u32 t = phase >> 16;
    if (__builtin_expect(t - r.dt16 > r.span, 0)) return saw_edge(t, r);
    return static_cast<i32>(t) - 32768;
}
// (sierra - sierra media vuelta después) / 2: fuera de los saltos vale ±16384.
inline i32 square(u32 phase, const Rate& r) {
    const u32 t = phase >> 16;
    if (__builtin_expect((t & 0x7fffu) - r.edge > r.edge_span, 0))
        return (saw_edge(t, r) - saw_edge(t ^ 0x8000u, r)) >> 1;
    return t & 0x8000u ? 16384 : -16384;
}
// Seno por parábola corregida (error < 0,1 %), Q15.
inline i32 sine(u32 phase) {
    const i32 a = static_cast<i32>(phase >> 16) - 32768;           // -1..1 (Q15) = -pi..pi
    const i32 y = (a * (32768 - (a < 0 ? -a : a))) >> 13;           // 4a(1-|a|)
    return y + ((7373 * (((y * (y < 0 ? -y : y)) >> 15) - y)) >> 15);  // + 0,225 (y|y| - y)
}

// Incremento desafinado `cents_q16` (Q16: 946 = 25 cents, hasta ±4000 = 105 cents),
// limitado bajo Nyquist.
inline u32 detuned(u32 increment, i32 cents_q16) {
    if (increment > 0x7fffffffu) increment = 0x7fffffffu;
    cents_q16 = cents_q16 < -4000 ? -4000 : cents_q16 > 4000 ? 4000 : cents_q16;
    const i32 delta = (static_cast<i32>(increment >> 12) * cents_q16) >> 4;
    const u32 v = static_cast<u32>(static_cast<i32>(increment) + delta);
    return v > 0x7fffffffu ? 0x7fffffffu : v;
}

// Ganancia por número de nubes, ~1/sqrt(n) en Q15: el volumen apenas cambia al añadir nubes.
const i32 kCloudGain[kMaxClouds + 1] = {0, 32767, 23170, 18919, 16384, 14654};
// Fases de arranque repartidas (proporción áurea): sin cancelaciones al empezar.
const u32 kStartPhase[kMaxClouds] = {0x00000000u, 0x9E3779B9u, 0x3C6EF372u, 0xDAA66D2Bu, 0x78DDE6E4u};
// Desplazamiento irregular de cada nube respecto al reparto uniforme, en fracción
// de paso (Q10): rompe la simetría para que las nubes no se alineen todas a la vez.
const i32 kJitter[kMaxClouds] = {0, 297, -233, 179, 0};
// Peso (energía, Q15) de los armónicos 1..4 de cada onda y del resto (calculado con
// una DFT de las ondas ideales). Con fases al azar, la suma de n nubes tiene energía n.
const i32 kHarmonic[5][5] = {
    {19921, 4980, 2213, 1245, 4409},   // saw
    {26561, 0, 2951, 0, 3256},         // sqr
    {10745, 8526, 5643, 2938, 4916},   // pulse
    {32768, 0, 0, 0, 0},               // sine
    {24518, 1532, 2724, 383, 3611},    // saw+sqr
};

inline u32 isqrt(u32 x) {
    u32 r = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

// Una forma de onda por clase: se elige una vez por bloque, no en cada muestra.
struct SawWave { static inline i32 at(u32 ph, const Rate& r) { return saw(ph, r); } };
struct SquareWave { static inline i32 at(u32 ph, const Rate& r) { return square(ph, r); } };
struct PulseWave {  // pulso 15 %
    static inline i32 at(u32 ph, const Rate& r) { return (saw(ph, r) - saw(ph - 0x26666666u, r)) >> 1; }
};
struct SineWave { static inline i32 at(u32 ph, const Rate&) { return sine(ph); } };
struct SawSquareWave {
    static inline i32 at(u32 ph, const Rate& r) { return (saw(ph, r) + square(ph, r)) >> 1; }
};
// Suma una nube entera a la mezcla.
template <class Wave>
inline void add_cloud(i32* sum, u32& phase, u32 increment, i32 slope, const Rate& r) {
    u32 ph = phase, inc = increment;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        sum[i] += Wave::at(ph, r);
        ph += inc;
        inc += static_cast<u32>(slope);
    }
    phase = ph;
}

// x / n exacto para x < 2^23 y n de 2 a 5, sin la división de 32 pasos: estima con
// el inverso de n en Q16 y corrige con el resto (nunca se pasa, solo se queda corto).
inline u32 divide_by_clouds(u32 x, u32 n) {
    static const u32 inverse[kMaxClouds + 1] = {0, 65536, 32768, 21845, 16384, 13107};
    const u32 m = inverse[n];
    u32 q = ((x >> 8) * m) >> 8;
    q += ((x - q * n) * m) >> 16;
    u32 r = x - q * n;
    while (r >= n) { ++q; r -= n; }
    return q;
}

inline u32 xorshift(u32& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

inline void initialize(Voice& v, Shared&, u32 index) {
    v.random = 0x12345678u ^ (index * 0x9E3779B9u);
    if (!v.random) v.random = 1;
    for (u32 k = 0; k < kMaxClouds; ++k) { v.phase[k] = kStartPhase[k]; v.drift[k] = v.target[k] = 0; }
    for (u32 k = 0; k < kMaxClouds; ++k) v.dt16[k] = v.rdt[k] = 0;
    v.tick = 0; v.gain = 0;
}
inline void start(Voice& v, Shared&, u32) {
    // La deriva sigue donde estaba (como un oscilador analógico); la fase se reparte de nuevo.
    for (u32 k = 0; k < kMaxClouds; ++k) v.phase[k] = kStartPhase[k];
    v.gain = 0;
}
inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& v, Shared&, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch pitch) {
    const u32 wave = choice(parameters[0], kWaves);
    const u32 clouds = 1 + choice(parameters[1], kMaxClouds);  // 1..5
    const i32 wind = q15(parameters[2]);
    const i32 sun = q15(parameters[3]);

    // Deriva (Sun): cada nube elige un destino nuevo cada ~0,2 s (escalonadas) y se acerca
    // a él despacio (~0,2 s). Rango ±15 cents (568 en Q16 de cents).
    ++v.tick;
    for (u32 k = 0; k < kMaxClouds; ++k) {
        if (((v.tick + k * 17) & 127) == 0)
            v.target[k] = static_cast<i32>(xorshift(v.random) >> 16) % 1137 - 568;
        v.drift[k] += (v.target[k] - v.drift[k]) >> 7;
    }

    // Desafinación de cada nube en Q16 de cents (946 = 25 cents).
    i32 cents[kMaxClouds];
    const i32 half = (wind * 946) >> 15;  // 0..25 cents a cada lado
    static const i32 kStep[kMaxClouds + 1] = {0, 0, 2048, 1024, 682, 512};  // 2048 / (n - 1)
    const i32 step = kStep[clouds];
    for (u32 k = 0; k < clouds; ++k) {
        i32 pos = 0;  // -1024..1024
        if (clouds > 1) {
            pos = -1024 + static_cast<i32>(k) * step;
            if (k > 0 && k + 1 < clouds) pos += (kJitter[k] * step) >> 10;
        }
        cents[k] = ((half * pos) >> 10) + ((v.drift[k] * sun) >> 15);
    }

    // Alturas de cada nube al principio y al final del bloque, interpoladas: sigue
    // glide, bend y LFO sin recalcular la desafinación en cada muestra.
    const unsigned last = opxy::block_frames - 1;
    const i32 gain = kCloudGain[clouds];
    i32 mixed[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) mixed[i] = 0;
    for (u32 k = 0; k < clouds; ++k) {
        const u32 inc = detuned(pitch[0], cents[k]);
        const i32 slope = static_cast<i32>(detuned(pitch[last], cents[k]) - inc) >> 6;
        const u32 dt16 = inc >> 16;
        if (dt16 != v.dt16[k] && wave != 3) {
            v.dt16[k] = dt16;
            v.rdt[k] = dt16 ? (1u << 30) / dt16 : 0;
        }
        Rate r;
        r.dt16 = dt16;
        r.rdt = v.rdt[k];
        r.span = 65535u - 2 * dt16;
        // Con dt16 >= media vuelta / 2 los saltos se solapan: edge imposible, siempre corrige.
        r.edge = dt16 <= 16383u ? dt16 : 0x7fffffffu;
        r.edge_span = dt16 <= 16383u ? 32767u - 2 * dt16 : 0;
        switch (wave) {
            case 0: add_cloud<SawWave>(mixed, v.phase[k], inc, slope, r); break;
            case 1: add_cloud<SquareWave>(mixed, v.phase[k], inc, slope, r); break;
            case 2: add_cloud<PulseWave>(mixed, v.phase[k], inc, slope, r); break;
            case 3: add_cloud<SineWave>(mixed, v.phase[k], inc, slope, r); break;
            default: add_cloud<SawSquareWave>(mixed, v.phase[k], inc, slope, r); break;
        }
    }
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        // Nivel: ±32768 normal; alineadas todas, hasta ~±73300 (5 nubes).
        mixed[i] = ((clamp(mixed[i], -262144, 262144) >> 2) * gain) >> 13;  // >> 2 antes: cabe en 32 bits
    }

    // Compensación: energía de la suma según las fases al final del bloque (la que viene).
    i32 g = 4096;
    if (clouds > 1) {
        const i32* w = kHarmonic[wave];
        i32 power = w[4];  // el resto de armónicos, que baten deprisa: energía media
        for (u32 h = 1; h <= 4; ++h) {
            if (!w[h - 1]) continue;
            i32 re = 0, im = 0;
            for (u32 k = 0; k < clouds; ++k) {
                const u32 ph = v.phase[k] * h;
                re += sine(ph + 0x40000000u) >> 6;  // ±512 por nube
                im += sine(ph) >> 6;
            }
            // |suma|^2 / n, con 32768 = energía media (fases al azar)
            const i32 ratio = static_cast<i32>(divide_by_clouds(static_cast<u32>(re * re + im * im) >> 3, clouds));
            power += ((w[h - 1] >> 3) * (ratio >> 3)) >> 9;
        }
        power = clamp(power, 512, 262143);  // ganancia 8x .. 0,35x (5 nubes en fase = energía 5)
        const u32 root = isqrt(static_cast<u32>(power) << 13) << 1;  // sqrt(energía) en Q15
        g = static_cast<i32>((1u << 27) / root);                 // 1/sqrt(energía) en Q12
    }
    const i32 g0 = v.gain ? v.gain : g, dg = (g - g0) >> 6;
    v.gain = g;

    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 sum = (mixed[i] * (g0 + dg * static_cast<i32>(i))) >> 12;
        // Limitador suave: y = 1,5x - 0,5x^3 con x = sum/3 (Q15). Ganancia 1 con señal
        // normal y techo redondo en ±98304, sin escalones ni recortes.
        const i32 x = (clamp(sum, -98304, 98304) * 10923) >> 15;  // ±32768
        const i32 x3 = (((x * x) >> 15) * x) >> 15;
        const i32 y = x + (x >> 1) - (x3 >> 1);                    // ±32768
        // Q15 -> Q31 con pico 0,5 (medio fondo de escala), como Twin Osc.
        output[i] = y * 32768;
    }
}
}
