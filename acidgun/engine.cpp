#include "core.h"
#include "tables.h"
#include <opxy/q31.hpp>

// Acidgun: sinte ácido de bajo al estilo de la TB-303.
//
// Página 1: Cutoff, Reso, Env Mod, Decay
// Página 2: Wave (saw / sqr), Tune (±12 semitonos), Slide (0..600 ms), Drive (estilo MXR)
//
// El filtro sigue el modelo TeeBeeFilter de Open303 (Robin Schmidt), ajustado a medidas de
// una 303 real: una escalera de diodos de 4 polos ACOPLADOS (cada etapa carga a la vecina,
// no son 4 filtros independientes), con la realimentación saturada y pasada por un paso
// alto de 150 Hz. Con resonancia aparece un pico chillón que se come los graves y, al
// saturar, retuerce la onda: el "chirrido" ácido.
// La envolvente también sigue las medidas de Open303: Cutoff va de 100 Hz (la 303: 314) a 1,5 kHz (la 303: 2,4 kHz),
// Env Mod barre hasta ~5 octavas, y la envolvente está centrada: al caer, el filtro baja
// por debajo del Cutoff (por eso el ataque "muerde").
// Después: drive (MXR Distortion+: paso alto de 720 Hz, mucha ganancia y diodos de
// germanio) y un limitador suave. Slide: cada nota empieza en la altura de la anterior.
//
// Todo en coma fija de 32 bits: el Blackfin no tiene coma flotante ni libgcc.
namespace acidgun {
typedef opxy::i32 i32;
typedef opxy::u32 u32;
using opxy::q31::multiply;

inline i32 q15(i32 value) { return value < 0 ? 0 : value >> 16; }
inline i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }

// Sierra con PolyBLEP (Q15). dt16: incremento Q16; rdt = 2^30 / dt16. Fuera de la zona de un
// salto basta una comparación (t - dt16 <= span). shifted() limita el incremento bajo Nyquist.
struct Rate { u32 dt16, rdt, span, half, half_span; };
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
inline i32 square_wave(u32 phase, const Rate& r) {
    const u32 t = phase >> 16;
    if (__builtin_expect((t & 0x7fffu) - r.half > r.half_span, 0))
        return (saw_edge(t, r) - saw_edge(t ^ 0x8000u, r)) >> 1;
    return t & 0x8000u ? 16384 : -16384;
}

// log2 de un incremento, Q16.
inline i32 log2q(u32 x) {
    if (!x) return 0;
    i32 n = 31;
    while (!(x & 0x80000000u)) { x <<= 1; --n; }
    const u32 i = (x >> 25) & 63, f = (x >> 9) & 0xFFFF;
    const i32 a = kLog2[i], b = kLog2[i + 1];
    return (n << 16) + a + static_cast<i32>((static_cast<u32>(b - a) * f) >> 16);
}
// Incremento * 2^(octaves), octaves en Q16; limitado bajo Nyquist.
inline u32 shifted(u32 increment, i32 octaves) {
    const i32 whole = octaves >> 16;  // suelo
    const u32 frac = static_cast<u32>(octaves) & 0xFFFF;
    const u32 i = frac >> 10, f = frac & 1023;
    const u32 e = kExp2[i] + ((((kExp2[i + 1] - kExp2[i]) >> 6) * f) >> 4);  // 2^frac / 2, Q31
    u32 v = static_cast<u32>(multiply(static_cast<i32>(increment >> 1), static_cast<i32>(e)));  // inc*2^frac/4
    const i32 s = whole + 2;
    if (s >= 0) v = s > 3 || v > (0x7fffffffu >> s) ? 0x7fffffffu : v << s;
    else v = s < -31 ? 0 : v >> -s;
    return v > 0x7fffffffu ? 0x7fffffffu : v;
}
// Tabla de 33 puntos indexada por un control Q15.
inline i32 lookup33(const i32* t, i32 control) {
    const i32 i = control >> 10, f = control & 1023;
    if (i >= 32) return t[32];
    return t[i] + ((t[i + 1] - t[i]) >> 10) * f;
}
// Valor de una tabla de 193 posiciones (1/16 de octava sobre 10 Hz) en una posición Q8.
inline i32 at193(const i32* t, i32 index_q8) {
    index_q8 = clamp(index_q8, 0, 192 << 8);
    const i32 i = index_q8 >> 8, f = index_q8 & 255;
    if (i >= 192) return t[192];
    return t[i] + ((t[i + 1] - t[i]) >> 8) * f;
}
// Saturación de la realimentación (Open303): x - x^3/6, recortada en ±raíz de 2 (Q22).
inline i32 shape22(i32 x) {
    x = clamp(x, -5931642, 5931642);
    const i32 s = x >> 8;                           // Q14
    const i32 c = ((((s * s) >> 14) * s) >> 14);    // x^3 en Q14
    return x - ((c * 2731) >> 6);                   // /6
}
// Saturación suave cúbica: x - 4x^3/27, plana a partir de ±1,5 (Q28). Con productos de
// 16 bits (Q14), que el Blackfin hace en un ciclo; de sobra para una curva de saturación.
inline i32 soft(i32 x) {
    const i32 limit = 3 << 27;  // 1,5
    if (x >= limit) return 1 << 28;
    if (x <= -limit) return -(1 << 28);
    const i32 s = x >> 14;                   // Q14, ±24576
    const i32 x3 = ((s * s) >> 14) * s;      // x^3 en Q28
    return x - (x3 >> 12) * 607;             // 4/27 = 607/4096
}

inline void initialize(Voice& v, Shared&, u32) {
    v.phase = 0;
    for (u32 k = 0; k < 4; ++k) v.y[k] = 0;
    v.env = v.envs = 0; v.glide = 0; v.fb = v.fb_x = 0;
    v.hp = v.hp_in = 0; v.lp = 0; v.dc_in = v.dc_out = 0; v.here = 0; v.fresh = 0;
    v.dt16 = v.rdt = 0;
}
inline void start(Voice& v, Shared& s, u32 owner) {
    // El oscilador sigue libre (como el analógico); la envolvente del filtro salta.
    v.env = 0x7fffffff;
    v.fresh = 1;
    s.lead = owner;
}
inline void destroy(Voice&, Shared& s, u32 owner) { if (s.lead == owner) s.lead = 0; }
// Nota: si la voz acaba (destroy) el estado guardado sigue valiendo para la siguiente.

// Lo que el bucle de audio necesita y no cambia dentro del bloque.
struct Block {
    u32 inc; i32 slope;
    Rate rate;
    i32 b, b_step, kr, k_step, go, g_step, rq, gq8, wet;
};
// El bucle de audio, con la onda y el drive elegidos una vez por bloque y el estado del
// filtro en variables locales mientras dura.
template <bool Square, bool Drive>
inline void run_block(Voice& v, const Block& block, opxy::Output output) {
    const i32 fb_c = 2102075903;   // paso alto 150 Hz en la realimentación, Q31
    const i32 hp_c = 1938111690;   // exp(-2 pi 720 / fs), Q31
    const i32 lp_c = 16696;        // 1 - exp(-2 pi 5000 / fs), Q15
    const i32 dc_c = 2141373067;   // exp(-2 pi 20 / fs), Q31
    const Rate r = block.rate;
    const i32 rq = block.rq, gq8 = block.gq8, wet = block.wet, slope = block.slope;
    const i32 b_step = block.b_step, k_step = block.k_step, g_step = block.g_step;
    u32 ph = v.phase, inc = block.inc;
    i32 b = block.b, kr = block.kr, go = block.go;
    i32 y0 = v.y[0], y1 = v.y[1], y2 = v.y[2], y3 = v.y[3];
    i32 fb = v.fb, fb_x = v.fb_x;
    i32 hp_state = v.hp, hp_in = v.hp_in, lp = v.lp, dc_in = v.dc_in, dc_out = v.dc_out;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 osc = Square ? square_wave(ph, r) : saw(ph, r);
        ph += inc;
        inc += static_cast<u32>(slope);

        // Filtro de diodos (Q22, margen ±512). Entra caliente (±2): la saturación de la
        // realimentación trabaja más y retuerce la onda.
        const i32 in = osc * 256;
        const i32 fbin = multiply(kr, shape22(y3) * 128);      // k * forma(y4)
        fb = multiply(fb_c, fb + fbin - fb_x);
        fb_x = fbin;
        const i32 input = in - fb;
        y0 += multiply(b * 2, input - y0 + y1);
        y1 += multiply(b, y0 - 2 * y1 + y2);
        y2 += multiply(b, y1 - 2 * y2 + y3);
        y3 += multiply(b, y2 - 2 * y3);
        y3 = clamp(y3, -(31 << 22), 31 << 22);
        const i32 out = multiply(go * 16, y3 * 16);            // g * y4 / 2 (Q24)
        b += b_step; kr += k_step; go += g_step;
        // La resonancia se come los graves (unos -10 dB con la escalera de diodos): se
        // devuelve el nivel (hasta x3) para que el pico chillón suene delante.
        i32 x = clamp(out, -(5 << 23), 5 << 23) * 16;          // Q28, ±2,5
        x += (x >> 14) * rq;                                   // x (1 + 2r), ±7,5

        if (Drive) {
            // Paso alto en Q26 (margen de sobra: la entrada llega a ±2).
            const i32 xq26 = x >> 2;
            hp_state = multiply(hp_c, hp_state + xq26 - hp_in);
            hp_in = xq26;
            const i32 hp = clamp(hp_state, -(1 << 26), 1 << 26);
            i32 boosted = (x >> 4) + (hp >> 10) * gq8;          // Q24
            boosted = clamp(boosted, -(10066329), 10066329);    // ±0,6
            // Diodos de germanio: codo suave a 0,35 (x / 0,35, saturado en ±1,5).
            const i32 z = clamp(((boosted >> 4) * 2925) >> 6, -(3 << 23), 3 << 23);  // Q24
            const i32 clipped = (soft(z * 16) >> 10) * 717;      // * 0,7 (Q28)
            lp += ((clipped - lp) >> 15) * lp_c;
            // Condensador de salida: quita la continua que deja recortar una onda asimétrica.
            dc_out = multiply(dc_c, dc_out + lp - dc_in);
            dc_in = lp;
            x += ((dc_out - x) >> 15) * wet;
        }
        // Limitador suave final: y = 1,5u - 0,5u^3 con u = x/3; techo redondo.
        const i32 xs = clamp(x >> 13, -98304, 98304);  // Q15
        const i32 uu = (xs * 10923) >> 15;
        const i32 u3 = (((uu * uu) >> 15) * uu) >> 15;
        const i32 y = uu + (uu >> 1) - (u3 >> 1);
        output[i] = y * 32768;  // pico 0,5 (medio fondo de escala)
    }
    v.phase = ph;
    v.y[0] = y0; v.y[1] = y1; v.y[2] = y2; v.y[3] = y3;
    v.fb = fb; v.fb_x = fb_x;
    v.hp = hp_state; v.hp_in = hp_in; v.lp = lp; v.dc_in = dc_in; v.dc_out = dc_out;
}

inline void render(Voice& v, Shared& s, u32 owner, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const i32 cutoff = q15(parameters[0]);
    const i32 reso = q15(parameters[1]);
    const i32 envmod = q15(parameters[2]);
    const i32 decay = q15(parameters[3]);
    const bool square = q15(parameters[4]) >= 16384;
    const i32 tune = q15(parameters[5]);   // 0..32767 = -12..+12 semitonos
    const i32 slide = q15(parameters[6]);
    const i32 drive = q15(parameters[7]);
    const unsigned last = opxy::block_frames - 1;

    // Tune: ±1 octava; se ajusta al semitono más cercano en ±0,15 semitonos.
    i32 semis_q8 = ((tune - 16384) * 3) >> 4;  // ±3072 = ±12 semitonos (Q8)
    const i32 nearest = (semis_q8 + (semis_q8 >= 0 ? 128 : -128)) / 256 * 256;
    if (semis_q8 - nearest < 38 && nearest - semis_q8 < 38) semis_q8 = nearest;
    const i32 tune_oct = (semis_q8 * 21845) >> 10;  // /12 -> Q16 de octava

    // Mono: una voz que ya no es la última nota se calla (la nueva sigue su sonido).
    if (s.lead != owner && !v.fresh) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }
    const i32 here = log2q(pitch[0]);
    if (v.fresh) {
        v.fresh = 0;
        // Recoge el estado de la nota anterior: fase, filtro, drive y la envolvente suavizada
        // (el ataque sale desde donde estaba, sin clic). La envolvente vuelve a saltar.
        if (s.valid) {
            const i32 env = v.env;
            v = s.snap;
            v.env = env;
            v.fresh = 0;
        }
        // Slide: la nota nueva empieza donde sonaba la anterior y se desliza (curva RC).
        v.glide = (s.valid && slide > 400) ? clamp(s.last_log - here, -(4 << 16), 4 << 16) : 0;
    } else if (here - v.here > 1966 || v.here - here > 1966) {
        // Salto de altura sin nota nueva: legato (pista en modo legato). Como el slide de la
        // 303: se desliza desde la nota anterior y la envolvente NO vuelve a saltar.
        v.glide = slide > 400 ? clamp(v.glide + v.here - here, -(4 << 16), 4 << 16) : 0;
    }
    const i32 glide0 = v.glide;
    v.glide = slide > 400 ? multiply(v.glide, lookup33(kSlide, slide)) : 0;
    v.here = pitch[last] == pitch[0] ? here : log2q(pitch[last]);

    const u32 inc = shifted(pitch[0], tune_oct + glide0);
    const u32 inc_end = pitch[last] == pitch[0] && v.glide == glide0 ? inc : shifted(pitch[last], tune_oct + v.glide);
    Block block;
    block.inc = inc;
    block.slope = static_cast<i32>(inc_end - inc) >> 6;
    const u32 dt16 = (inc > inc_end ? inc : inc_end) >> 16;
    if (dt16 != v.dt16) {
        v.dt16 = dt16;
        v.rdt = dt16 ? (1u << 30) / dt16 : 0;
    }
    block.rate.dt16 = dt16;
    block.rate.rdt = v.rdt;
    block.rate.span = 65535u - 2 * dt16;  // dt16 <= 32767: shifted() limita bajo Nyquist
    // Con saltos a menos de media vuelta se solapan: valor imposible, siempre corrige.
    block.rate.half = dt16 <= 16383u ? dt16 : 0x7fffffffu;
    block.rate.half_span = dt16 <= 16383u ? 32767u - 2 * dt16 : 0;

    // Envolvente del filtro: salto en la nota, ataque de 3 ms y caída exponencial.
    const i32 envs0 = v.envs;
    v.env = multiply(v.env, lookup33(kDecay, decay));
    v.envs += multiply(823628952, v.env - v.envs);
    // Mapeo medido (Open303): posición del corte en 1/16 de octava sobre 10 Hz (Q8).
    const i32 knob = 13607 + ((cutoff * 16003) >> 15);                 // 100 Hz .. 1,5 kHz
    // Las curvas medidas de Env Mod van de 314 Hz (la 303) a 2,4 kHz; por debajo, las de 314 Hz.
    const i32 c303 = clamp(((knob - 20365) * 11178) >> 12, 0, 32767);
    const i32 s_lo = ((envmod * 15459) >> 15) + 3019;                   // octavas Q12
    const i32 s_hi = ((envmod * 17181) >> 15) + 3540;
    const i32 scaler = s_lo + (((s_hi - s_lo) * c303) >> 15);
    const i32 offset = 9647 + ((c303 * 1582) >> 15);                  // Q15
    const i32 idx0 = knob + (((((envs0 >> 16) - offset) * scaler) >> 11) >> 4);
    const i32 idx1 = knob + (((((v.envs >> 16) - offset) * scaler) >> 11) >> 4);

    // Resonancia con la curva de Open303.
    const i32 rq = clamp(lookup33(kReso, reso), 0, 32767);
    const i32 r31 = rq << 16;
    // Coeficientes al principio y al final del bloque, interpolados.
    const i32 b_0 = at193(kB0, idx0), b_1 = at193(kB0, idx1);
    const i32 k_table0 = at193(kK, idx0), k_table1 = at193(kK, idx1);
    // Más chirrido que la 303 original: arriba, la realimentación sube hasta un 15 % más (al borde
    // de la autooscilación: chilla en los picos sin dispararse).
    const i32 extra = (((rq * rq) >> 15) * 4915) >> 15;
    i32 k_0 = multiply(k_table0, r31), k_1 = multiply(k_table1, r31);
    k_0 += (k_0 >> 15) * extra;
    k_1 += (k_1 >> 15) * extra;
    // Ganancia de salida (compensa la resonancia): ((k/17 - 1) r + 1)(1 + r), Q24.
    i32 g_0 = multiply(multiply(k_table0, 126322568) - (1 << 24), r31) + (1 << 24);
    i32 g_1 = multiply(multiply(k_table1, 126322568) - (1 << 24), r31) + (1 << 24);
    g_0 += multiply(g_0, r31);
    g_1 += multiply(g_1, r31);
    block.b = b_0; block.b_step = (b_1 - b_0) >> 6;
    block.kr = k_0; block.k_step = (k_1 - k_0) >> 6;
    block.go = g_0; block.g_step = (g_1 - g_0) >> 6;
    block.rq = rq;

    // Drive (MXR Distortion+): ganancia 1..61 sobre el paso alto de 720 Hz (Q8).
    block.gq8 = (((drive * drive) >> 15) * 60) >> 7;   // 0..15360
    block.wet = drive >= 4096 ? 32768 : drive * 8;      // entra rápido desde el limpio

    if (square) {
        if (drive) run_block<true, true>(v, block, output);
        else run_block<true, false>(v, block, output);
    } else {
        if (drive) run_block<false, true>(v, block, output);
        else run_block<false, false>(v, block, output);
    }
    // La nota que suena deja su estado para la siguiente.
    s.snap = v;
    s.last_log = v.here + v.glide;
    s.valid = 1;
}
}
