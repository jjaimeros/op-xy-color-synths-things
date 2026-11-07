#include "display.h"

// Pantalla de Acidgun: una pistola de rayos a la izquierda, apuntando a la derecha.
//   Cada nota es un disparo: rojo con saw, azul intenso con sqr.
//   Cutoff  cerrado: un rayo gordo y redondeado (cuentas de plasma). Abierto: un láser fino
//           y afilado que suelta chispitas.
//   Reso    engorda el núcleo brillante.
//   Env Mod el disparo sale con chispas, en proporción.
//   Decay   lo que tarda el rayo en adelgazar tras el disparo (como la envolvente del filtro).
//   Drive   el borde del rayo se vuelve aserrado, como una onda de sierra.
//   Slide   la pistola sube y baja un poco en cada nota, durante el tiempo del slide.
namespace acidgun {
inline void display_reset(DisplayState& s) {
    s.header.reset(); s.notes.reset();
    s.serial = 0; s.random = 0x6C8E9CF5u; s.key = 1;
    s.shot = -2; s.env = 0; s.release = 0; s.recoil = 0;
    s.bob = s.bob_from = s.bob_to = 0; s.bob_t = -1; s.bob_dir = 1;
    s.sparkle = 0; s.clock = 0; s.flash_turn = 0;
    for (opxy::u32 i = 0; i < kSparks; ++i) {
        Spark& p = s.sparks[i];
        p.x = p.y = 0; p.vx = p.vy = 0; p.life = p.total = 0; p.shade = p.size = 0; p.spin = 0;
    }
}

namespace detail {
using namespace opxy::draw;
typedef opxy::u32 u32;
typedef opxy::i32 i32;
const i32 kHomeX = 196, kHomeY = 104;   // boca del cañón en reposo (px)
const i32 kReach = 256;                  // largo máximo del rayo (px)

inline i32 q15(i32 value) { return value < 0 ? 0 : value >> 16; }
inline u32 next(u32& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
inline i32 rnd(u32& s, i32 range) { return static_cast<i32>((next(s) >> 8) % static_cast<u32>(range)); }
inline u32 isqrt(u32 x) {
    u32 r = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}
// 2^(x/16) * 1024 para x en 1/16 de octava (0..255).
inline i32 exp2_16(i32 x) {
    static const i32 frac[16] = {1024, 1069, 1116, 1166, 1217, 1271, 1327, 1386,
                                 1448, 1512, 1579, 1649, 1722, 1798, 1878, 1961};
    return frac[x & 15] << (x >> 4);
}
// Texto corto: número con sufijo.
inline void put(char* out, const char* a, const char* b) {
    char t[header_capacity]; u32 at = 0;
    for (u32 i = 0; a[i] && at + 1 < header_capacity; ++i) t[at++] = a[i];
    for (u32 i = 0; b[i] && at + 1 < header_capacity; ++i) t[at++] = b[i];
    t[at] = 0;
    copy(out, header_capacity, t);
}
inline void itoa(char* t, i32 v) {
    char r[12]; u32 n = 0;
    bool neg = v < 0; if (neg) v = -v;
    do { r[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && n < 11);
    u32 at = 0;
    if (neg) t[at++] = '-';
    while (n) t[at++] = r[--n];
    t[at] = 0;
}
// Valor con un decimal: 12 -> "1.2".
inline void tenths(char* t, i32 v) {
    itoa(t, v / 10);
    u32 at = 0; while (t[at]) ++at;
    t[at++] = '.'; t[at++] = static_cast<char>('0' + v % 10); t[at] = 0;
}
// Tiempo del slide en ms según los anchors 0, 40, 150, 600 de definition.json.
inline i32 slide_ms(i32 slide) {
    const i32 x = slide * 3;            // 0..98301 (Q15 * 3)
    const i32 j = x >> 15, f = x & 32767;
    static const i32 a[4] = {0, 40, 150, 600};
    if (j >= 3) return 600;
    return a[j] + (((a[j + 1] - a[j]) * f) >> 15);
}
// Suavizado 3t^2 - 2t^3 (Q10).
inline i32 smooth(i32 t) {
    t = clamp(t, 0, unit);
    return (((t * t) >> 10) * (3 * unit - 2 * t)) >> 10;
}
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace detail;

    const i32 cutoff = q15(input.slots[0]);
    const i32 reso = q15(input.slots[1]);
    const i32 envmod = q15(input.slots[2]);
    const i32 decay = q15(input.slots[3]);
    const bool square = q15(input.slots[4]) >= 16384;
    const i32 tune = q15(input.slots[5]);
    const i32 slide = q15(input.slots[6]);
    const i32 drive = q15(input.slots[7]);
    const bool still = input.reduced_motion != 0;

    // --- Cabecera de dos páginas, con valores en unidades reales ---
    {
        static const char* const titles[8] = {"cutoff", "reso", "env mod", "decay",
                                              "wave", "tune", "slide", "drive"};
        char values[8][header_capacity];
        char t[12];
        const i32 hz = (exp2_16((13607 + ((cutoff * 16003) >> 15) + 128) >> 8) * 10) >> 10;  // 100 Hz .. 1,5 kHz
        if (hz < 1000) { itoa(t, hz); put(values[0], t, ""); }
        else { tenths(t, hz / 100); put(values[0], t, "k"); }
        header_amount(values[1], header_capacity, input.slots[1]);
        header_amount(values[2], header_capacity, input.slots[2]);
        const i32 ms = (100 * exp2_16(decay * 74 / 32768)) >> 10;   // 100 ms .. 2,5 s
        if (ms < 1000) { itoa(t, ms); put(values[3], t, "ms"); }
        else { tenths(t, ms / 100); put(values[3], t, "s"); }
        copy(values[4], header_capacity, square ? "sqr" : "saw");
        i32 semis_q8 = ((tune - 16384) * 3) >> 4;
        const i32 nearest = (semis_q8 + (semis_q8 >= 0 ? 128 : -128)) / 256 * 256;
        if (semis_q8 - nearest < 38 && nearest - semis_q8 < 38) {
            itoa(t, nearest / 256);
            put(values[5], nearest > 0 ? "+" : "", t);
        } else {
            const i32 d = semis_q8 * 10 / 256;
            tenths(t, d < 0 ? -d : d);
            put(values[5], d < 0 ? "-" : "+", t);
        }
        const i32 sms = slide_ms(slide);
        if (slide <= 400) copy(values[6], header_capacity, "off");
        else { itoa(t, sms); put(values[6], t, "ms"); }
        header_amount(values[7], header_capacity, input.slots[7]);
        const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
        page_header(state.header, input, frame, titles, values, node::header, pages);
    }

    const i32 dt = clamp(input.elapsed, 0, 102);
    const NoteEvents events = state.notes.update(input);
    const bool held = input.held_notes > 0 && input.held_notes <= 128;
    if (state.shot == -2) { state.serial = input.note_serial; state.shot = -1; }  // tras reset: sin disparo inventado
    const bool fired = input.note_serial != state.serial;
    state.serial = input.note_serial;
    state.clock = (state.clock + dt) & 0xFFFFF;

    // --- Disparo ---
    if (fired) {
        state.shot = 0;
        state.release = -1;
        state.env = unit;
        state.recoil = still ? 0 : 3 * unit;
        state.flash_turn = rnd(state.random, 3600);
        // Chispas del disparo, en proporción a Env Mod.
        if (!still) state.sparkle += 1 * unit + ((envmod * 12) >> 5);  // 1..13
        // Slide: la pistola va a la otra altura durante el tiempo del slide.
        if (slide > 400 && !still) {
            state.bob_dir = -state.bob_dir;
            state.bob_from = state.bob;
            state.bob_to = state.bob_dir * (3 * unit + ((slide * 5) >> 5));  // 3..8 px
            state.bob_t = 0;
        }
    }
    if (events.hold_ended && state.release < 0) state.release = 0;
    if (!held && state.shot >= 0 && state.release < 0) state.release = 0;

    if (state.shot >= 0) state.shot = clamp(state.shot + dt, 0, 60 * unit);
    if (state.release >= 0 && state.shot >= 0) state.release += dt;
    // Envolvente visual: cae como la del filtro (tau 30 ms .. 3 s).
    {
        const i32 tau = clamp((100 * exp2_16(decay * 74 / 32768)) >> 10, 100, 2500);  // ms
        const i32 step = state.env * dt / tau;           // dt en ms ~ Q10 s
        state.env -= step > 0 ? step : (state.env > 0 ? 1 : 0);
        if (state.env < 0) state.env = 0;
    }
    state.recoil -= state.recoil * dt / 80 + (state.recoil > 0 ? 1 : 0);
    if (state.recoil < 0) state.recoil = 0;

    // Slide: tramo con curva suave de bob_from a bob_to en el tiempo del slide.
    if (slide <= 400 || still) {
        state.bob -= state.bob * dt / 150;
        state.bob_t = -1;
    } else if (state.bob_t >= 0) {
        const i32 ms = slide_ms(slide);
        state.bob_t += dt;
        const i32 p = state.bob_t * 1000 / (ms > 10 ? ms : 10);   // Q10
        state.bob = state.bob_from + (((state.bob_to - state.bob_from) >> 4) * smooth(p) >> 6);
        if (p >= unit) { state.bob = state.bob_to; state.bob_t = -1; }
    }

    // --- Pistola ---
    const i32 gx = kHomeX * unit - state.recoil;
    const i32 gy = kHomeY * unit + state.bob;
    place(frame, node::body, gx, gy, unit, 0, keep);
    place(frame, node::detail, gx, gy, unit, 0, keep);
    place(frame, node::tube, gx, gy, unit, 0, keep);

    // --- Rayo ---
    // Largo: sale en 60 ms. Al soltar, la cola se va hacia la derecha en 150 ms.
    i32 length = 0, tail = 0;
    if (state.shot >= 0) {
        length = still ? unit : clamp(state.shot * unit / 61, 0, unit);
        if (state.release >= 0) tail = still ? unit : clamp(state.release * unit / 154, 0, unit);
    }
    const u32 base_shade = square ? shade::blue : shade::red;
    const i32 open = cutoff >> 5;                 // Q10: 0 cerrado (redondo) .. 1 abierto (afilado)
    if (tail >= unit || length == 0) {
        hide(frame, node::beam); hide(frame, node::mid); hide(frame, node::core);
        // Primer frame: los paths necesitan geometría válida aunque estén ocultos.
        for (u32 l = 0; l < 3; ++l) {
            Frame::path_type& path = frame.paths[l];
            if (path.key) continue;
            path.key = state.key;
            for (u32 i = 0; i < 32; ++i) {
                const i32 lx = (static_cast<i32>(i) * 4 - 62) * unit;
                path.points[i].x = path.points[63 - i].x = lx;
                path.points[i].y = -unit; path.points[63 - i].y = unit;
            }
        }
        if (tail >= unit) state.shot = -1;
    } else {
        // Grosor (medio, px Q10): base + envolvente * Env Mod + redondez (corte cerrado).
        const i32 e = (state.env * (256 + (envmod >> 5))) >> 10;   // 0..1,25
        i32 half = 2 * unit + ((e * 5) >> 1) + ((1024 - open) * 9 >> 1);
        if (state.release >= 0) half = (half * (unit - tail)) >> 10;
        // Capas: la media y el núcleo, en fracción del borde; Reso engorda el núcleo.
        const i32 mid_frac = 600, core_frac = 230 + (reso >> 7);   // núcleo 0,22..0,47
        const i32 saw_amp = (drive * 6) >> 5;                       // sierra: hasta ±6 px (Q10)
        i32 x1 = length * kReach;                                   // punta (Q10 px desde la boca)
        i32 x0 = tail * kReach;                                     // inicio
        if (x1 < 8 * unit) x1 = 8 * unit;
        if (x0 > x1 - 8 * unit) x0 = x1 - 8 * unit;
        const i32 span = x1 - x0;
        const i32 beads = (1024 - open) * 45 / 100;                 // cuentas de plasma
        const i32 phase = (state.clock * 1800) >> 10;               // las cuentas avanzan

        Frame::path_type* paths[3] = {&frame.paths[node::beam], &frame.paths[node::mid], &frame.paths[node::core]};
        const i32 fracs[3] = {1024, mid_frac, core_frac};
        ++state.key; if (!state.key) state.key = 1;
        for (u32 l = 0; l < 3; ++l) paths[l]->key = state.key;
        for (u32 i = 0; i < 32; ++i) {
            // 15 dientes de sierra: pares de puntos (inicio y final de cada diente).
            i32 t; i32 rise = 0;  // t Q10 a lo largo del rayo
            // Sin puntos repetidos y sin media vuelta en la punta (la punta sigue a su diente): el
            // OP-XY dibuja rayos sueltos desde los contornos que se doblan sobre sí mismos.
            if (i == 0) t = 0;
            else if (i == 31) { t = unit; rise = unit; }
            else {
                const i32 k = static_cast<i32>(i - 1) >> 1;
                const bool end = ((i - 1) & 1) != 0;
                t = (k * unit + (end ? unit - 40 : 32)) / 15;
                rise = end ? unit : 0;
            }
            const i32 x = x0 + (span >> 10) * t;                      // Q10 px
            const i32 from_start = x - x0, to_tip = x1 - x;
            // Perfil redondo: cápsula con extremos semicirculares de radio R, y cuentas.
            const i32 R = half + 2 * unit;
            i32 round = unit;
            {
                const i32 d = from_start < to_tip ? from_start : to_tip;
                if (d < R) {
                    const i32 u = unit - d * unit / R;                // 1 - d/R
                    round = static_cast<i32>(isqrt(static_cast<u32>(unit * unit - u * u)));
                }
                const i32 wave = sine(t * 2109 / 100 - phase);       // 6 cuentas a lo largo
                round = (round * (unit + ((beads * wave) >> 10))) >> 10;
            }
            // Perfil afilado: nace fino y acaba en punta de lanza (último 30 %).
            i32 sharp = unit;
            if (from_start < 6 * unit) sharp = 400 + (from_start * 624) / (6 * unit);
            const i32 spear = (span * 3) / 10;
            if (to_tip < spear) sharp = (sharp * (to_tip / 4 * unit / (spear / 4 > 0 ? spear / 4 : 1))) >> 10;
            const i32 profile = (round * (unit - open) + sharp * open) >> 10;
            const i32 h = (half >> 4) * profile >> 6;
            // Drive: el rayo entero sigue una onda de sierra (sube despacio, cae de golpe).
            // Nace en el eje del cañón: la onda alcanza su amplitud en el primer diente (16 px).
            const i32 grow = clamp(from_start >> 4, 0, unit);
            const i32 lift = state.release < 0 ? (((saw_amp * (2 * rise - unit)) >> 10) * grow) >> 10 : 0;
            const i32 cy = -(lift >> 1);                            // centro local (escala 2)
            // Coordenadas locales del path (escala 2): x de -64 a 64, en Q10.
            const i32 lx = (x >> 1) - 64 * unit;
            for (u32 l = 0; l < 3; ++l) {
                // Puntos siempre distintos: medio grosor mínimo 0,3 px.
                const i32 hl = clamp((((h >> 4) * fracs[l]) >> 6) >> 1, 160, 60 * unit);
                paths[l]->points[i].x = lx; paths[l]->points[i].y = cy - hl;
                paths[l]->points[63 - i].x = lx; paths[l]->points[63 - i].y = cy + hl;
            }
        }
        const bool dim = state.release >= 0 && tail > 600;
        const i32 bx = gx + kReach / 2 * unit, by = gy;
        place(frame, node::beam, bx, by, 2 * unit,
              dim ? (square ? shade::blue_dim : shade::red_dim) : base_shade, stroke);
        place(frame, node::mid, bx, by, 2 * unit,
              dim ? (square ? shade::blue_dim_mid : shade::red_dim_mid) : base_shade + 1, stroke);
        place(frame, node::core, bx, by, 2 * unit, base_shade + 2, stroke);

        // Chispitas a lo largo del láser cuando el corte está abierto.
        if (!still && open > 400 && state.release < 0) state.sparkle += (open - 400) * 25 * dt / 624;  // hasta 25/s
    }

    // --- Fogonazo en la boca ---
    if (state.shot >= 0 && state.shot < 110 && !still) {
        const i32 sc = clamp(1400 - state.shot * 12, 64, 2047);
        place(frame, node::flash, gx + 3 * unit, gy, sc, base_shade + 2, fill_and_stroke);
        turn(frame, node::flash, rotation(state.flash_turn));
    } else {
        hide(frame, node::flash);
    }

    // --- Chispas ---
    while (state.sparkle >= unit) {
        state.sparkle -= unit;
        u32 slot = kSparks;
        for (u32 i = 0; i < kSparks; ++i) if (state.sparks[i].life <= 0) { slot = i; break; }
        if (slot == kSparks) { state.sparkle = 0; break; }
        Spark& p = state.sparks[slot];
        const bool burst = state.shot >= 0 && state.shot < 60;
        if (burst) {
            // Del disparo: salen de la boca, hacia delante y abiertas.
            p.x = gx + 4 * unit; p.y = gy;
            p.vx = static_cast<opxy::i16>(70 + rnd(state.random, 190));
            p.vy = static_cast<opxy::i16>(rnd(state.random, 200) - 100);
            p.total = static_cast<opxy::i16>(220 + rnd(state.random, 260));
            p.size = static_cast<opxy::u8>(12 + rnd(state.random, 10));
            const u32 c = static_cast<u32>(rnd(state.random, 4));
            p.shade = static_cast<opxy::u8>(c == 0 ? shade::yellow : c == 1 ? shade::white : c == 2 ? shade::orange
                                            : (square ? shade::spark_blue : shade::spark_red));
        } else {
            // Del láser abierto: pequeñas, en cualquier punto del rayo.
            p.x = gx + (8 + rnd(state.random, kReach - 16)) * unit;
            p.y = gy + (rnd(state.random, 5) - 2) * unit;
            p.vx = static_cast<opxy::i16>(rnd(state.random, 80) - 20);
            p.vy = static_cast<opxy::i16>(rnd(state.random, 90) - 45);
            p.total = static_cast<opxy::i16>(140 + rnd(state.random, 160));
            p.size = static_cast<opxy::u8>(6 + rnd(state.random, 5));
            p.shade = static_cast<opxy::u8>(rnd(state.random, 2) ? shade::white
                                            : (square ? shade::spark_blue : shade::yellow));
        }
        p.life = p.total;
        p.spin = static_cast<opxy::u16>(rnd(state.random, 900));
    }
    for (u32 i = 0; i < kSparks; ++i) {
        Spark& p = state.sparks[i];
        if (p.life <= 0 || still) { p.life = 0; hide(frame, node::sparks + i); continue; }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vy = static_cast<opxy::i16>(p.vy + ((260 * dt) >> 10));   // caen un poco
        p.life = static_cast<opxy::i16>(p.life - dt);
        p.spin = static_cast<opxy::u16>((p.spin + ((dt * 900) >> 10)) % 3600);
        if (p.life <= 0 || p.x > 500 * unit || p.y > 230 * unit || p.y < 40 * unit) {
            p.life = 0; hide(frame, node::sparks + i); continue;
        }
        const i32 sc = (static_cast<i32>(p.size) * 64 * (200 + p.life * 824 / p.total)) >> 10;
        place(frame, node::sparks + i, p.x, p.y, clamp(sc, 32, 2047), p.shade, fill_and_stroke);
        turn(frame, node::sparks + i, rotation(p.spin));
    }
}
}
