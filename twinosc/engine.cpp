#include "wave.h"

// Twin Osc: dos osciladores analógicos virtuales con antialiasing PolyBLEP,
// suboscilador cuadrado y mezcla. Todo en coma fija (el Blackfin no tiene FPU)
// y sin divisiones por muestra: los recíprocos se calculan una vez por bloque.
//
// Parámetros (orden = ranura guardada):
//   0 Wave 1  saw/sqr/tri      4 Wave 2  saw/sqr/tri
//   1 Shape 1                   5 Shape 2
//   2 Detune  0..50 cents       6 Octave  -2..+2 (osc 2)
//   3 Mix     osc1 <-> osc2     7 Sub     nivel del sub (1 octava bajo osc 1)
//
// Shape: saw -> suma una segunda sierra desfasada hasta 1/2 ciclo (sierra doble)
//        sqr -> ancho de pulso 50% .. ~8%
//        tri -> inclinación 50% .. 92% (de triángulo a casi rampa)

namespace twinosc {
// Incremento desafinado `cents_q16` (Q16, ±946 = ±25 cents) y movido `octave`
// octavas (-2..+2), limitado bajo Nyquist. Solo aritmética de 32 bits: el
// Blackfin no tiene multiplicación de 64 bits por hardware.
inline u32 scaled(u32 increment, i32 cents_q16, i32 octave) {
    if (increment > 0x7fffffffu) increment = 0x7fffffffu;
    // (inc >> 10) * 946 < 2^31: cabe en i32; la pérdida de precisión es < 0,01 cent.
    const i32 delta = (static_cast<i32>(increment >> 10) * cents_q16) >> 6;
    u32 v = static_cast<u32>(static_cast<i32>(increment) + delta);
    if (octave > 0) {
        const u32 limit = 0x7fffffffu >> octave;
        v = v > limit ? 0x7fffffffu : v << octave;
    } else if (octave < 0) {
        v >>= -octave;
    }
    return v;
}

inline void initialize(Voice& voice, Shared&, u32) {
    voice.phase1 = voice.phase2 = voice.phase_sub = 0;
    for (u32 k = 0; k < 3; ++k) voice.dt16[k] = voice.rdt[k] = 0;
    for (u32 k = 0; k < 2; ++k) {
        voice.tri_amount[k] = -1;
        voice.tri_peak[k] = voice.tri_up[k] = voice.tri_dn[k] = 0;
    }
}
inline void start(Voice& voice, Shared&, u32) {
    // Osc 2 empieza desfasado para que el detune no arranque con cancelación de fase.
    voice.phase1 = 0; voice.phase2 = 0x40000000u; voice.phase_sub = 0;
}
inline void destroy(Voice&, Shared&, u32) {}

// Altura de una parte para el bloque. Fuera de la zona de un salto basta una comparación
// (t - dt <= span); dentro, la corrección completa. Con saltos que se solapan (notas altísimas)
// dt es imposible y siempre corrige.
struct Edge {
    Rate rate;
    u32 dt, span;      // zona sin saltos de una sierra
    u32 half, half_span;  // zona sin saltos de una cuadrada simétrica (salto cada media vuelta)
};
inline Edge edge_for(Voice& voice, u32 slot, u32 increment) {
    Edge e;
    const u32 dt16 = increment >> 16;
    if (dt16 != voice.dt16[slot]) {
        voice.dt16[slot] = dt16;
        voice.rdt[slot] = dt16 ? (1u << 30) / dt16 : 0;
    }
    e.rate.dt16 = dt16;
    e.rate.rdt = voice.rdt[slot];
    const bool apart = dt16 <= 32767u, half_apart = dt16 <= 16383u;
    e.dt = apart ? dt16 : 0x7fffffffu;
    e.span = apart ? 65535u - 2 * dt16 : 0;
    e.half = half_apart ? dt16 : 0x7fffffffu;
    e.half_span = half_apart ? 32767u - 2 * dt16 : 0;
    return e;
}
inline i32 fast_saw(u32 phase, const Edge& e) {
    const u32 t = phase >> 16;
    if (__builtin_expect(t - e.dt > e.span, 0)) return saw(phase, e.rate);
    return static_cast<i32>(t) - 32768;
}

// Forma de un oscilador; el triángulo guarda sus dos divisiones mientras no cambie Shape.
inline Shape cached_shape(Voice& voice, u32 slot, u32 wave, i32 amount) {
    if (wave != 2) return shape_for(wave, amount);
    if (voice.tri_amount[slot] != amount) {
        const Shape fresh = shape_for(wave, amount);
        voice.tri_amount[slot] = amount;
        voice.tri_peak[slot] = fresh.peak;
        voice.tri_up[slot] = fresh.inv_up;
        voice.tri_dn[slot] = fresh.inv_dn;
    }
    Shape s;
    s.wave = wave;
    s.offset = 0;
    s.peak = voice.tri_peak[slot];
    s.inv_up = voice.tri_up[slot];
    s.inv_dn = voice.tri_dn[slot];
    return s;
}

// Una clase por forma de onda: se elige una vez por bloque, no en cada muestra.
struct SingleSaw {
    const Edge& e;
    inline i32 operator()(u32 ph) const { return fast_saw(ph, e); }
};
struct DoubleSaw {
    const Edge& e; u32 offset;
    inline i32 operator()(u32 ph) const { return (fast_saw(ph, e) + fast_saw(ph + offset, e)) >> 1; }
};
struct Pulse {
    const Edge& e; u32 offset;
    inline i32 operator()(u32 ph) const { return fast_saw(ph, e) - fast_saw(ph - offset, e); }
};
struct Triangle {
    u32 peak, up, down;
    inline i32 operator()(u32 ph) const {
        const u32 t = ph >> 16;
        if (t < peak) return static_cast<i32>((t * up) >> 14) - 32768;
        return 32767 - static_cast<i32>(((t - peak) * down) >> 14);
    }
};
template <class Oscillator>
inline void add_part(i32* sum, u32& phase, u32 increment, i32 step, i32 gain, const Oscillator& oscillate) {
    u32 ph = phase, inc = increment;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        sum[i] += (oscillate(ph) * gain) >> 15;
        ph += inc;
        inc += static_cast<u32>(step);
    }
    phase = ph;
}
// Mezcla un oscilador; si no suena solo avanza la fase, sin desafinar al volver:
// la suma de begin + i * step en todo el bloque es 64 begin + 2016 step.
inline void add_oscillator(i32* sum, Voice& voice, u32 slot, u32& phase, const Shape& s,
                           u32 begin, i32 step, i32 gain, bool sounding) {
    if (!sounding) {
        phase += begin * opxy::block_frames + static_cast<u32>(step) * 2016u;
        return;
    }
    if (s.wave == 2) {
        const Triangle triangle = {s.peak, s.inv_up, s.inv_dn};
        add_part(sum, phase, begin, step, gain, triangle);
        return;
    }
    const Edge e = edge_for(voice, slot, begin);
    if (s.wave == 1) {
        const Pulse pulse = {e, s.offset};
        add_part(sum, phase, begin, step, gain, pulse);
    } else if (s.offset) {
        const DoubleSaw twin = {e, s.offset};
        add_part(sum, phase, begin, step, gain, twin);
    } else {
        const SingleSaw single = {e};
        add_part(sum, phase, begin, step, gain, single);
    }
}

inline void render(Voice& voice, Shared&, u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const Shape shape1 = cached_shape(voice, 0, choice(parameters[0], kWaves), q15(parameters[1]));
    const Shape shape2 = cached_shape(voice, 1, choice(parameters[4], kWaves), q15(parameters[5]));
    const i32 mix = q15(parameters[3]), sub = q15(parameters[7]);
    const u32 octave = choice(parameters[6], kOctaves);  // 0..4 => -2..+2

    // Detune 0..50 cents repartido: osc 1 baja la mitad y osc 2 sube la mitad.
    // 2^(c/1200) ~= 1 + c*ln2/1200; 25 cents => 0.01444 * 65536 = 946 (error < 0.1 cent).
    const i32 spread = (q15(parameters[2]) * 946) >> 15;
    const i32 shift2 = static_cast<i32>(octave) - 2;

    // Incrementos al principio y al final del bloque, interpolados linealmente:
    // sigue glide, bend y LFO de altura sin recalcular en cada muestra.
    const unsigned last = opxy::block_frames - 1;
    const u32 begin1 = scaled(pitch[0], -spread, 0), end1 = scaled(pitch[last], -spread, 0);
    const u32 begin2 = scaled(pitch[0], spread, shift2), end2 = scaled(pitch[last], spread, shift2);
    const i32 step1 = (static_cast<i32>(end1 - begin1)) / static_cast<i32>(opxy::block_frames);
    const i32 step2 = (static_cast<i32>(end2 - begin2)) / static_cast<i32>(opxy::block_frames);

    // Mezcla lineal osc1/osc2 (cada término cabe en i32); se salta lo que no suena, pero
    // las fases avanzan siempre.
    i32 sum[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) sum[i] = 0;
    add_oscillator(sum, voice, 0, voice.phase1, shape1, begin1, step1, 32767 - mix, mix < 32767);
    add_oscillator(sum, voice, 1, voice.phase2, shape2, begin2, step2, mix, mix > 0);

    // Sub cuadrado una octava por debajo de osc 1 (±32768).
    u32 ph = voice.phase_sub, inc = begin1;
    if (sub > 0) {
        const Edge e = edge_for(voice, 2, begin1 >> 1);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            const u32 t = ph >> 16;
            i32 s;
            if (__builtin_expect((t & 0x7fffu) - e.half > e.half_span, 0))
                s = saw(ph, e.rate) - saw(ph - 0x80000000u, e.rate);
            else
                s = t & 0x8000u ? 32768 : -32768;
            sum[i] += (s * sub) >> 15;
            ph += inc >> 1;
            inc += static_cast<u32>(step1);
        }
    } else if (step1 == 0) {
        ph += (begin1 >> 1) * opxy::block_frames;
    } else {
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            ph += inc >> 1;
            inc += static_cast<u32>(step1);
        }
    }
    voice.phase_sub = ph;

    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        // Q15 con pico 2.0 -> Q31 con pico 0.5 (medio fondo de escala).
        const i32 s = sum[i];
        output[i] = (s > 65536 ? 65536 : s < -65536 ? -65536 : s) * 16384;
    }
}
}
