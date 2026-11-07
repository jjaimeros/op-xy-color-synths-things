#include "display.h"

// Pantalla de Nuvola: un cielo.
//   Nuvolas  una nube por oscilador (1..5). Al tocar, de las nubes cae una lluvia de
//            nubecitas de golosina rosas y blancas.
//   Wind     (detune) las nubes se separan sin tapar el sol; al subirlo llega una racha
//            que las empuja a la izquierda y vuelven despacio. Con viento, más rachas.
//   Sun      (drift) el sol crece y sus destellos giran.
//   Los pájaros vuelan siempre; el viento no se los lleva.
namespace nuvola {
inline void display_reset(DisplayState& s) {
    s.time = 0; s.serial = 0; s.random = 0x2545F491u; s.rain = 0; s.wind = -1; s.gust = 0;
    s.gust_clock = 0; s.drift = 0; s.push = 0; s.streaks = 0; s.flight = 190 * opxy::draw::unit; s.spin = 0;
    for (opxy::u32 i = 0; i < kDrops; ++i) { s.drops[i].x = s.drops[i].y = 0; s.drops[i].speed = 0; s.drops[i].shade = 0; s.drops[i].sway = 0; }
}

namespace detail {
using namespace opxy::draw;
typedef opxy::u32 u32;
typedef opxy::i32 i32;
inline u32 choice(i32 value, u32 count) {
    const u32 q = value < 0 ? 0u : static_cast<u32>(value) >> 16;
    const u32 index = (q * count) >> 15;
    return index < count ? index : count - 1;
}
inline i32 q15(i32 value) { return value < 0 ? 0 : value >> 16; }
inline void number(char* out, i32 v, const char* suffix) {
    char t[header_capacity]; u32 at = 0;
    if (v >= 100) t[at++] = static_cast<char>('0' + v / 100);
    if (v >= 10) t[at++] = static_cast<char>('0' + v / 10 % 10);
    t[at++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; suffix[i] && at + 1 < header_capacity; ++i) t[at++] = suffix[i];
    t[at] = 0;
    copy(out, header_capacity, t);
}
// Acerca `value` a `target` con constante de tiempo `tau` (Q10 s): movimiento suave.
inline i32 ease(i32 value, i32 target, i32 dt, i32 tau) {
    if (dt <= 0) return value;
    const i32 step = (target - value) * dt / tau;
    return step == 0 && target != value ? (target > value ? value + 1 : value - 1) : value + step;
}
inline u32 next(u32& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace detail;
    static const char* const waves[5] = {"saw", "sqr", "pulse", "sine", "sw/sq"};

    const u32 wave = choice(input.slots[0], 5);
    const u32 clouds = 1 + choice(input.slots[1], 5);
    const i32 wind = q15(input.slots[2]);
    const i32 sun = q15(input.slots[3]);
    const bool still = input.reduced_motion != 0;

    // Cabecera: valores en unidades reales.
    copy(frame.header[0], header_capacity, waves[wave]);
    number(frame.header[1], static_cast<i32>(clouds), "");
    number(frame.header[2], (wind * 50 + 16384) >> 15, "c");
    number(frame.header[3], (sun * 100 + 16384) >> 15, "");

    const i32 dt = clamp(input.elapsed, 0, 102);
    const bool held = input.held_notes > 0 && input.held_notes <= 128;
    const bool playing = held && !still;
    const bool fresh = input.note_serial != state.serial;
    state.serial = input.note_serial;
    if (playing) state.time += static_cast<u32>(dt);

    // --- Viento (Wind) ---
    const bool first = state.wind < 0;
    if (!first && wind > state.wind + 160) {
        // Al subir Wind llega una racha, más fuerte cuanto más viento.
        const i32 strength = 560 + (wind >> 6);  // 0,55 .. 1
        if (state.gust < strength) state.gust = strength;
        state.gust_clock = 0;
    }
    if (wind > 1600) {  // con viento, rachas de vez en cuando (cada 5 s con poco, 2,5 s con todo)
        state.gust_clock += dt;
        if (state.gust_clock > 5 * unit - ((wind * 5) >> 6)) {
            state.gust_clock = 0;
            const i32 strength = 300 + (wind >> 6);
            if (state.gust < strength) state.gust = strength;
        }
    } else state.gust_clock = 0;
    state.gust = state.gust > 0 ? state.gust - (state.gust * dt / 1500 + 1) : 0;  // amaina en ~1,5 s
    if (state.gust < 0) state.gust = 0;
    state.wind = wind;
    // Empuje: dos suavizados seguidos, sale y vuelve en curva (lento y fluido).
    if (still) { state.drift = 0; state.push = 0; }
    else {
        state.drift = ease(state.drift, -state.gust * 18, dt, 420);  // hasta 18 px a la izquierda
        state.push = ease(state.push, state.drift, dt, 420);
    }

    // --- Sol (Sun): crece de 1,2 a 1,9 ---
    const i32 sun_scale = unit * 120 / 100 + ((sun * (unit * 70 / 100)) >> 15);
    const i32 sun_left = 424 * unit - 25 * sun_scale;  // borde de los destellos

    // --- Nubes ---
    // Tamaño según cuántas haya; el grupo va entre el borde izquierdo y el sol, y el
    // viento abre la separación hasta llenar ese hueco (sin tapar el sol, aunque crezca).
    static const i32 sizes[6] = {0, 2048, 2048, 1990, 1880, 1769};  // ~8 % más grandes; 2,0 es el máximo del SDK
    const i32 size = sizes[clouds];
    const i32 lo = 10 * unit + 21 * size, hi = sun_left - 6 * unit - 24 * size;  // centros posibles
    const i32 center = (lo + hi) / 2;
    i32 spacing = 0;
    if (clouds > 1) {
        const i32 room = (hi - lo) / static_cast<i32>(clouds - 1);
        const i32 closed = 22 * size < room ? 22 * size : room, open_max = 52 * size;
        const i32 open = open_max < room ? open_max : room;
        spacing = closed + (((open > closed ? open - closed : 0) >> 5) * wind >> 10);
    }
    const i32 first_x = center - spacing * static_cast<i32>(clouds - 1) / 2;
    i32 cloud_x[5], cloud_y[5];
    for (u32 k = 0; k < 8; ++k) {
        const u32 id = node::clouds + k;
        if (k >= clouds) { hide(frame, id); continue; }
        const i32 bob = playing ? sine(static_cast<i32>((state.time * 3 + k * 977) % 3600)) * 2 : 0;
        // Cada nube cede al viento un poco distinto (0,85 .. 1,15): se mueven como grupo vivo.
        const i32 give = 870 + static_cast<i32>((k * 5) % 8) * 40;
        cloud_x[k] = first_x + static_cast<i32>(k) * spacing + state.push * give / 1000;
        // Alturas desiguales, cerca unas de otras (±12 px): un grupo natural, no una fila.
        static const i32 heights[5] = {117, 104, 121, 109, 126};
        cloud_y[k] = (clouds == 1 ? 112 : heights[k]) * unit + bob;
        place(frame, id, cloud_x[k], cloud_y[k], size, playing ? shade::cloud_glow : shade::cloud, stroke);
    }

    // --- Lluvia de golosina: cae de las nubes mientras suenan notas ---
    if (playing) {
        state.rain += dt * static_cast<i32>(5 + clouds * 2);  // 7..21 por segundo
        if (fresh) state.rain += 3 * unit;                    // cada nota nueva, un puñado
    } else state.rain = 0;
    while (state.rain >= unit) {
        state.rain -= unit;
        u32 slot = kDrops;
        for (u32 i = 0; i < kDrops; ++i) if (!state.drops[i].shade) { slot = i; break; }
        if (slot == kDrops) { state.rain = 0; break; }
        const u32 r = next(state.random);
        const u32 k = (r & 0xffff) % clouds;
        Drop& d = state.drops[slot];
        d.x = cloud_x[k] + (static_cast<i32>((r >> 16) & 31) - 16) * size;
        d.y = cloud_y[k] + 5 * size;
        d.speed = static_cast<opxy::u16>(38 + ((r >> 21) & 31));               // 38..69 px/s
        const u32 c = (r >> 26) & 7;
        d.shade = static_cast<opxy::u8>(c < 3 ? shade::candy : c < 6 ? shade::candy + 1 : shade::candy + 2);
        d.sway = static_cast<opxy::u8>(r >> 8);
    }
    for (u32 i = 0; i < kDrops; ++i) {
        Drop& d = state.drops[i];
        const u32 id = node::drops + i;
        if (d.shade && !still) {
            d.y += dt * d.speed;
            d.sway = static_cast<opxy::u8>(d.sway + ((dt * 3) >> 4));
            if (d.y > 232 * unit) d.shade = 0;
        }
        if (!d.shade || still) { d.shade = still ? 0 : d.shade; hide(frame, id); continue; }
        const i32 swing = sine(static_cast<i32>(d.sway) * 3600 / 256);         // -1..1 (Q10)
        place(frame, id, d.x + swing * 3 + state.push / 2, d.y, unit, d.shade, fill_and_stroke);
        turn(frame, id, rotation(swing * 150 / unit));                          // se mecen ±15°
    }

    // --- Sol ---
    place(frame, node::sun, 424 * unit, 92 * unit, sun_scale, shade::sun, stroke);
    place(frame, node::sun_glints, 424 * unit, 92 * unit, sun_scale, shade::sun, stroke);
    // Los destellos giran al tocar (25°/s) y, con Sun, también en reposo (hasta 12°/s).
    if (!still) state.spin = (state.spin + dt * ((playing ? 256 : 0) + (sun >> 8))) % (3600 * unit);
    turn(frame, node::sun_glints, rotation(state.spin / unit));

    // --- Ráfagas: líneas que cruzan de derecha a izquierda mientras sopla ---
    if (!still) state.streaks = (state.streaks + dt * (260 + state.gust / 4)) % (600 * unit);
    static const i32 wind_y[4] = {60, 128, 158, 190}, wind_lag[4] = {0, 230, 410, 120};
    for (u32 j = 0; j < 4; ++j) {
        if (still || state.gust < 80) { hide(frame, node::wind + j); continue; }
        const i32 x = 540 * unit - (state.streaks + wind_lag[j] * unit) % (600 * unit);
        const i32 y = wind_y[j] * unit + sine(static_cast<i32>((state.streaks / 4 + j * 900) % 3600)) * 3;
        const u32 level = static_cast<u32>(state.gust * 4 / (unit + 1));  // 0..3
        place(frame, node::wind + j, x, y, unit, shade::wind + level, stroke);
    }

    // --- Pájaros: vuelan siempre, cruzando el cielo ---
    if (!still) state.flight = (state.flight + dt * 16) % (580 * unit);  // 16 px/s
    static const i32 bird_dx[3] = {0, -17, -31}, bird_dy[3] = {0, -7, 3};
    for (u32 j = 0; j < 3; ++j) {
        const u32 up = node::birds + 2 * j, down = up + 1;
        const i32 x = (still ? 150 * unit : state.flight - 50 * unit) + bird_dx[j] * unit;
        const i32 bob = still ? 0 : sine(static_cast<i32>((state.flight / 6 + j * 1300) % 3600)) * 3;
        const i32 y = (64 + bird_dy[j]) * unit + bob;
        if (x < -60 * unit || x > 600 * unit) { hide(frame, up); hide(frame, down); continue; }
        // Aletean cada ~0,25 s; a veces planean.
        const i32 beat = state.flight / (4 * unit) + static_cast<i32>(j);
        const bool glide = !still && (state.flight / (40 * unit) + j) % 3 == 0;
        const bool wings_up = still || glide || beat % 2 == 0;
        place(frame, wings_up ? up : down, x, y, unit, shade::bird, stroke);
        hide(frame, wings_up ? down : up);
    }
}
}
