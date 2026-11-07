#include "core.h"
#include "tables.h"

// Ofender: sonidos de chip emulados a partir de los samples de la carpeta del usuario. Cada uno es
// de un solo golpe: suena su tiempo y se calla. Los tiempos y arpegios son los medidos.
//   Armor  arp    pulso 28 %, arpegio descendente de 4 notas a 31 ms ("PO-20 Sound 9 13")
//          arp2   pulso 50 %, el mismo arpegio ("PO-20 Sound 9 6")
//          arps   seno, arpegio de 5 notas a 21 ms ("Sound 10 B 0%")
//          vibe   pulso 28 % con vibrato de 6 Hz ("Sound 14 B 50%")
//          sine   seno que cae en 240 ms ("Sound 15 B 0%")
//          zap    pulso 50 % cuya frecuencia baja en línea recta hasta 0 ("Sound 5")
//          coin   tres notas subiendo a 15 ms ("Sound 6 B 20%")
//          noise  ruido con el ritmo medido, siguiendo la nota ("Sound 8")
//   Battery al 100 % no hace nada. Al bajarla cae la frecuencia de muestreo de toda la voz (de 44,1 kHz
//          a unos 6,6 kHz) y el sonido de chip se desafina oscilando a 5 Hz, hasta ±1 semitono a 0 %.
//   Boom   volumen del bombo: la muestra boom.wav, en cada nota y a su altura.
//   Blast  volumen de la explosión: ruido LFSR que baja de tono, en cada nota.
//
// CPU: altura, nivel y onda cambian por bloque; lo que no suena no cuesta nada. Sin divisiones,
// sin 64 bits y sin coma flotante.
namespace ofender {
namespace detail {
enum Kind { kPulse, kSineWave, kNoise };

// Niveles en Q15: 65536 sería el pico declarado. Todo a un 70 % del sample, para dejar sitio al
// bombo y a la explosión. `level` es la mitad de la distancia entre los dos niveles del pulso.
struct Sound {
    opxy::u8 kind;
    opxy::u8 high;        // pulso: pasos altos de 32 (9 = 28 %)
    opxy::u16 level;
    opxy::u16 hold;       // bloques a volumen pleno
    opxy::u16 length;     // bloques en total
    opxy::u16 fade;       // Q15 / bloques de caída
    opxy::u8 step;        // medios bloques por nota del arpegio (0: sin arpegio)
    opxy::u8 notes;       // notas del arpegio
    opxy::u8 first;       // primera nota en kRatios
    opxy::u8 pad;
};
// Bloques de 1,451 ms: 441 = 640 ms, 165 = 240 ms, 155 = 225 ms, 262 = 380 ms, 31 = 45 ms.
const Sound kSounds[kArmors] = {
    {kPulse, 9, 16970, 0, 441, 74, 43, 4, 0, 0},       // arp
    {kPulse, 16, 17430, 0, 441, 74, 43, 4, 0, 0},      // arp2
    {kSineWave, 0, 33030, 0, 441, 74, 29, 5, 0, 0},    // arps
    {kPulse, 9, 21560, 138, 262, 264, 0, 1, 3, 0},     // vibe
    {kSineWave, 0, 30740, 0, 165, 198, 0, 1, 3, 0},    // sine
    {kPulse, 16, 18350, 0, 155, 211, 0, 1, 3, 0},      // zap
    {kPulse, 16, 27520, 31, 31, 0, 21, 3, 5, 0},       // coin
    {kNoise, 0, 17890, 0, 155, 211, 0, 1, 3, 0},       // noise
};
// Multiplicadores de altura en Q12: +12, +8, +5, 0, -4 semitonos, y después 0, +12, +19.
const opxy::u16 kRatios[8] = {8192, 6502, 5468, 4096, 3251, 4096, 8192, 12274};

// Battery: paso del reloj de muestreo en Q16 (65536 = cada muestra), 2,75 octavas en 8 tramos.
const u32 kCrushRate[9] = {9742, 12363, 15689, 19911, 25268, 32066, 40693, 51642, 65536};

inline u32 scaled(u32 increment, u32 ratio) {
    return (increment >> 12) * ratio;
}
}

inline void initialize(Voice& v, Shared&, u32) {
    v.phase = 0; v.age = 0; v.finished = 1; v.droop = 0; v.soft = 0;
    v.noise_phase = 0; v.noise_lfsr = 0x2B5Du;
    v.boom_pos = kBoomFrames;
    v.blast_phase = v.blast_inc = 0; v.blast_lfsr = 0x4A31u; v.blast_vol = 0;
    v.crush_phase = 65535u; v.crush_hold = 0;
}
inline void start(Voice& v, Shared&, u32) {
    v.phase = 0; v.age = 0; v.finished = 0;
    v.boom_pos = 0;
    v.blast_phase = 0; v.blast_inc = 800000000u; v.blast_vol = 15 << 8;  // reloj de unos 8 kHz
    v.crush_phase = 65535u;  // la primera muestra de la nota se toma enseguida
}
inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& v, Shared&, u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    using namespace detail;
    const unsigned frames = opxy::block_frames;
    const u32 armor = choice(parameters[0], kArmors);
    const i32 battery = q15(parameters[1]), boom = q15(parameters[2]), blast = q15(parameters[3]);
    const Sound& sound = kSounds[armor];

    // Fin del sonido.
    if (!v.finished && v.age >= sound.length) v.finished = 1;

    i32 sum[opxy::block_frames];
    if (v.finished) {
        for (unsigned i = 0; i < frames; ++i) sum[i] = 0;
    } else {
        const u32 age = v.age;
        // Volumen: pleno hasta `hold`; luego baja en línea recta.
        const i32 envelope = age < sound.hold ? 32767 : 32767 - static_cast<i32>((age - sound.hold) * sound.fade);
        i32 amount = (static_cast<i32>(sound.level) * (envelope < 0 ? 0 : envelope)) >> 15;

        // Altura.
        u32 key = pitch[frames / 2];
        if (key > 0x3fffffffu) key = 0x3fffffffu;
        // Battery: desafinación oscilante de 5 Hz (476 / 256 pasos del seno por bloque), en fase con la
        // nota. A 0 %, 3897 / 65536 = 1 semitono hacia arriba y 3679 / 65536 = 1 hacia abajo. A tope, nada.
        const i32 wobble = kSine[((age * 476u) >> 8) & 255u];
        const u32 depth = wobble < 0 ? 3679u : 3897u;
        const u32 drift = (((static_cast<u32>(32767 - battery) * depth) >> 15)
                           * static_cast<u32>(wobble < 0 ? -wobble : wobble)) >> 15;
        const u32 shift = ((key >> 10) * drift) >> 6;
        key = wobble < 0 ? key - shift : key + shift;
        u32 note = sound.first;
        if (sound.step) note += ((age * 2) / sound.step) % sound.notes;  // una división por bloque
        u32 increment = scaled(key, kRatios[note]);
        if (armor == 3) {        // vibe: triángulo de ±0,75 semitonos, una vuelta cada 117 bloques
            const i32 t = static_cast<i32>(age & 127u);
            const i32 wave = (t < 64 ? t : 127 - t) - 32;
            increment = static_cast<u32>(static_cast<i32>(increment) + static_cast<i32>(increment >> 12) * (wave * 5));
        } else if (armor == 5) { // zap: la frecuencia cae en línea recta hasta 0
            increment = (increment >> 8) * (((sound.length - age) * 423u) >> 8);
        } else if (armor == 6 && age < 10) {  // coin: la primera nota sale floja
            amount >>= 2;
        }

        u32 ph = v.phase;
        if (sound.kind == kPulse) {
            const u32 mask = (1u << sound.high) - 1u;
            const i32 hi = (amount * static_cast<i32>(32 - sound.high)) >> 4;
            const i32 lo = -((amount * static_cast<i32>(sound.high)) >> 4);
            i32 soft = v.soft;
            for (unsigned i = 0; i < frames; ++i) {
                const i32 x = (mask >> (ph >> 27)) & 1u ? hi : lo;
                soft += ((x - soft) * 3) >> 2;  // los flancos de los samples no son del todo rectos
                sum[i] = soft;
                ph += increment;
            }
            v.soft = soft;
        } else if (sound.kind == kSineWave) {
            for (unsigned i = 0; i < frames; ++i) {
                sum[i] = (kSine[ph >> 24] * amount) >> 15;
                ph += increment;
            }
        } else {
            // Ruido: un bit nuevo en cada vuelta de su reloj, que sigue la nota y el ritmo medido.
            if (key > 0x7ffffffu) key = 0x7ffffffu;
            u32 index = age >> 3;
            if (index > 19) index = 19;
            const u32 inc = (key >> 4) * kNoiseRate[index];
            u32 np = v.noise_phase, lfsr = v.noise_lfsr;
            for (unsigned i = 0; i < frames; ++i) {
                const u32 before = np;
                np += inc;
                if (np < before) lfsr = (lfsr >> 1) | (((lfsr ^ (lfsr >> 1)) & 1u) << 14);
                sum[i] = lfsr & 1u ? amount : -amount;
            }
            v.noise_phase = np;
            v.noise_lfsr = lfsr;
        }
        v.phase = ph;
        ++v.age;
    }

    // Caída de la onda, como en los samples: paso alto de unos 11 Hz.
    i32 droop = v.droop;
    for (unsigned i = 0; i < frames; ++i) {
        const i32 y = sum[i] - (droop >> 9);
        droop += y;
        sum[i] = y;
    }
    v.droop = droop;

    // Bombo: la muestra, a su altura, en cada nota.
    if (v.boom_pos < kBoomFrames) {
        const i32 gain = (boom * 22938) >> 15;  // Q14: a tope, 1,4 = 70 % del sample en Q15
        const u32 left = kBoomFrames - v.boom_pos;
        const unsigned count = left < frames ? left : frames;
        if (gain > 0) {
            const opxy::i16* const sample = kBoom + v.boom_pos;
            for (unsigned i = 0; i < count; ++i) sum[i] += (sample[i] * gain) >> 14;
        }
        v.boom_pos += count;
    }

    // Explosión: ruido de 15 bits, un bit nuevo en cada vuelta de su reloj; cae en unos 500 ms.
    if (v.blast_vol > 0) {
        const i32 level = v.blast_vol >> 8;
        const i32 gain = (((blast * (level * level)) >> 8) * 3) >> 2;
        if (gain > 0) {
            u32 np = v.blast_phase, lfsr = v.blast_lfsr;
            const u32 ni = v.blast_inc;
            for (unsigned i = 0; i < frames; ++i) {
                const u32 before = np;
                np += ni;
                if (np < before) lfsr = (lfsr >> 1) | (((lfsr ^ (lfsr >> 1)) & 1u) << 14);
                sum[i] += lfsr & 1u ? gain : -gain;
            }
            v.blast_phase = np;
            v.blast_lfsr = lfsr;
        }
        v.blast_vol -= 11;
        v.blast_inc -= v.blast_inc >> 9;
    }

    // Battery: retiene cada muestra hasta la siguiente vuelta del reloj. A tope no se toca nada.
    if (battery < 32767) {
        const u32 index = static_cast<u32>(battery) >> 12, part = static_cast<u32>(battery) & 4095u;
        const u32 rate = kCrushRate[index] + (((kCrushRate[index + 1] - kCrushRate[index]) * part) >> 12);
        u32 clock = v.crush_phase;
        i32 hold = v.crush_hold;
        for (unsigned i = 0; i < frames; ++i) {
            clock += rate;
            if (clock >= 65536u) { clock -= 65536u; hold = sum[i]; }
            sum[i] = hold;
        }
        v.crush_phase = clock;
        v.crush_hold = hold;
    }

    for (unsigned i = 0; i < frames; ++i) {
        const i32 s = sum[i];
        output[i] = (s > 65536 ? 65536 : s < -65536 ? -65536 : s) * 16384;
    }
}
}
