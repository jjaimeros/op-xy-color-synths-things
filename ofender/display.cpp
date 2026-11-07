#include "display.h"

// Pantalla de Ofender: la cabina del usuario con un matamarcianos que juega solo.
//   Cada nota pulsa un botón, alternando: MISSILE dispara una ráfaga de dos tiros;
//   BOMB suelta una bomba que estalla contra las montañas. Con un acorde se hunden los dos.
//   Cada enemigo abatido suma 10 puntos al marcador, arriba a la izquierda.
//   Boom sacude el paisaje; Blast agranda las explosiones.
//   El juego solo avanza con el transporte en marcha o con teclas pulsadas.
// Todas las posiciones se ajustan a píxeles enteros, como en una máquina de la época.
namespace ofender {
inline void display_reset(DisplayState& s) {
    s.started = 0; s.serial = 0; s.random = 0x6C8E9CF5u; s.last_bomb = 1; s.score = 0;
    s.coast = 0; s.clock = 0; s.press_m = s.press_b = 0; s.pending = s.gap = 0;
    s.ship_y = ((kTop + kBottom) / 2 - 12) * opxy::draw::unit;
    s.tilt = 0; s.scroll = 0; s.shake = 0; s.spawn = 200;
    for (opxy::u32 i = 0; i < kShots; ++i) s.shots[i].x = s.shots[i].y = 0;
    for (opxy::u32 i = 0; i < kBombs; ++i) s.bombs[i].x = s.bombs[i].y = s.bombs[i].vy = 0;
    for (opxy::u32 i = 0; i < kEnemies; ++i) {
        Enemy& e = s.enemies[i];
        e.x = e.y = 0; e.vx = 0; e.wobble = 0;
    }
    for (opxy::u32 i = 0; i < kParticles; ++i) {
        Particle& p = s.particles[i];
        p.x = p.y = 0; p.vx = p.vy = 0; p.life = 0; p.shade = p.size = 0;
    }
}

namespace detail {
using namespace opxy::draw;
typedef opxy::u32 u32;
typedef opxy::i32 i32;
const i32 kShipX = kLeft + 21, kShipW = 10, kShipH = 7;
const i32 kEnemyW = 7, kEnemyH = 5;
const i32 kScoreX = kLeft + 2, kScoreY = kTop + 2, kDigitStep = 4;
const i32 kBurst = 2;   // tiros por ráfaga
const i32 kSkyBottom = kBottom - kMountainPeak - kEnemyH - 2;   // las naves vuelan por encima de los picos

inline u32 next(u32& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
inline i32 rnd(u32& s, i32 range) { return static_cast<i32>((next(s) >> 8) % static_cast<u32>(range)); }
inline i32 snap(i32 q10) { return (q10 >> 10) << 10; }

// Altura de las montañas (px) en la columna x de la pantalla.
inline i32 ground(const DisplayState& s, i32 x) {
    const i32 column = (x - kLeft) + (s.scroll >> 10);
    const i32 index = (column / kMountainStep) % static_cast<i32>(sizeof(kHeights));
    return kHeights[index < 0 ? 0 : index];
}

inline void burst(DisplayState& s, i32 x, i32 y, i32 count, i32 blast, i32 speed) {
    static const opxy::u8 colors[4] = {shade::white, shade::yellow, shade::orange, shade::red};
    for (u32 i = 0; i < kParticles && count > 0; ++i) {
        Particle& p = s.particles[i];
        if (p.life > 0) continue;
        --count;
        p.x = x; p.y = y;
        const i32 reach = speed + ((blast * speed) >> 15);
        p.vx = static_cast<opxy::i16>(rnd(s.random, 2 * reach + 1) - reach);
        p.vy = static_cast<opxy::i16>(rnd(s.random, 2 * reach + 1) - reach);
        p.life = static_cast<opxy::i16>(180 + rnd(s.random, 220) + ((blast * 200) >> 15));
        p.shade = colors[rnd(s.random, 4)];
        p.size = static_cast<opxy::u8>(16 + (blast >> 11) * (rnd(s.random, 2)));  // hasta 2 px
    }
}

inline void update(DisplayState& s, i32 dt, i32 blast) {
    s.clock = (s.clock + dt) & 0xFFFFF;
    s.scroll += 12 * dt;
    if (s.scroll >= kMountainWidth * unit) s.scroll -= kMountainWidth * unit;
    s.shake -= (s.shake * dt) >> 6;
    if (s.shake < 8) s.shake = 0;

    // Sale una nave enemiga por la derecha de vez en cuando.
    s.spawn -= dt;
    if (s.spawn <= 0) {
        s.spawn = 500 + rnd(s.random, 1100);
        for (u32 i = 0; i < kEnemies; ++i) {
            Enemy& e = s.enemies[i];
            if (e.x) continue;
            e.x = (kRight + 2) * unit;
            e.y = (kTop + 3 + rnd(s.random, kSkyBottom - kTop - 3)) * unit;
            e.vx = static_cast<opxy::i16>(14 + rnd(s.random, 16));
            e.wobble = static_cast<opxy::u16>(rnd(s.random, 3600));
            break;
        }
    }
    i32 target = -1, nearest = 0x7fffffff;
    for (u32 i = 0; i < kEnemies; ++i) {
        Enemy& e = s.enemies[i];
        if (!e.x) continue;
        e.x -= e.vx * dt;
        e.wobble = static_cast<opxy::u16>((e.wobble + ((dt * 1400) >> 10)) % 3600);
        if (e.x < (kLeft - kEnemyW - 2) * unit) { e.x = 0; continue; }
        if (e.x > (kShipX + kShipW) * unit && e.x < nearest) { nearest = e.x; target = e.y; }
    }

    // Piloto automático: la nave se alinea con el enemigo más cercano.
    const i32 aim = (target >= 0 ? target + (kEnemyH - kShipH) * unit / 2 : ((kTop + kSkyBottom) / 2) * unit);
    const i32 step = 34 * dt;
    i32 move = clamp(aim - s.ship_y, -step, step);
    s.ship_y = clamp(s.ship_y + move, (kTop + 1) * unit, kSkyBottom * unit);
    const i32 lean = dt ? clamp(move / dt * 64, -2 * unit, 2 * unit) : 0;  // sigue a la nave
    s.tilt += ((lean - s.tilt) * dt) >> 6;

    // Ráfaga de tiros.
    s.gap -= dt;
    if (s.pending > 0 && s.gap <= 0) {
        for (u32 i = 0; i < kShots; ++i) {
            if (s.shots[i].x) continue;
            s.shots[i].x = (kShipX + kShipW) * unit;
            s.shots[i].y = s.ship_y + (kShipH / 2) * unit;
            break;
        }
        --s.pending;
        s.gap = 56;  // unos 55 ms
    }
    for (u32 i = 0; i < kShots; ++i) {
        Shot& shot = s.shots[i];
        if (!shot.x) continue;
        shot.x += 150 * dt;
        if (shot.x > (kRight + 1) * unit) { shot.x = 0; continue; }
        for (u32 k = 0; k < kEnemies; ++k) {
            Enemy& e = s.enemies[k];
            if (!e.x) continue;
            const i32 dx = shot.x - e.x, dy = shot.y - e.y;
            if (dx > -3 * unit && dx < (kEnemyW + 1) * unit && dy > -2 * unit && dy < (kEnemyH + 2) * unit) {
                burst(s, e.x + (kEnemyW / 2) * unit, e.y + (kEnemyH / 2) * unit, 5 + (blast >> 12), blast, 26);
                e.x = 0;
                shot.x = 0;
                s.score = s.score >= 99990u ? 0u : s.score + 10u;
                break;
            }
        }
    }

    // Bombas: caen sobre las montañas.
    for (u32 i = 0; i < kBombs; ++i) {
        Bomb& b = s.bombs[i];
        if (!b.x) continue;
        b.x += 24 * dt;
        b.vy += 120 * dt;
        b.y += (b.vy >> 10) * dt;
        if (b.y >= (kBottom - ground(s, b.x >> 10) - 2) * unit || b.x > (kRight - 2) * unit) {
            burst(s, b.x, b.y, 7 + (blast >> 12), blast, 34);
            b.x = 0;
        }
    }

    for (u32 i = 0; i < kParticles; ++i) {
        Particle& p = s.particles[i];
        if (p.life <= 0) continue;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vy = static_cast<opxy::i16>(p.vy + ((90 * dt) >> 10));
        p.life = static_cast<opxy::i16>(p.life - dt);
        if (p.x < kLeft * unit || p.x > kRight * unit || p.y < kTop * unit || p.y > (kBottom - 1) * unit) p.life = 0;
    }
}

// Marcador: segmentos a, b, c, d, e, f, g de cada cifra, con origen común en su esquina.
inline void draw_score(Frame& f, u32 score) {
    static const opxy::u8 digits[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    for (u32 i = kScoreDigits; i-- > 0;) {
        const u32 mask = digits[score % 10u];
        score /= 10u;
        const i32 x = (kScoreX + static_cast<i32>(i) * kDigitStep) * unit;
        for (u32 k = 0; k < kDigitSegments; ++k) {
            const u32 id = node::score + i * kDigitSegments + k;
            if ((mask >> k) & 1u) place(f, id, x, kScoreY * unit, unit, 0, keep);
            else hide(f, id);
        }
    }
}

inline void press(Frame& f, u32 face, u32 glint, i32 x, i32 y, bool down) {
    const i32 scale = down ? unit * 7 / 8 : unit;
    place(f, face, x, y, scale, down ? shade::button_down : shade::button, fill_and_stroke);
    place(f, glint, x, y, scale, 0, keep);
}
}

inline void display(DisplayState& s, const opxy::draw::Input& input, Frame& f) {
    using namespace opxy::draw;
    using namespace detail;
    static const char* const armors[kArmorNames] = {"arp", "arp2", "arps", "vibe", "sine", "zap", "coin", "noise"};
    const u32 q_armor = static_cast<u32>(input.slots[0] < 0 ? 0 : input.slots[0]) >> 16;
    const u32 armor = (q_armor * kArmorNames) >> 15;
    const i32 boom = input.slots[2] < 0 ? 0 : input.slots[2] >> 16;
    const i32 blast = input.slots[3] < 0 ? 0 : input.slots[3] >> 16;
    header(f, 0, armors[armor < kArmorNames ? armor : kArmorNames - 1]);
    // Battery: entero y con %, en lugar del "100.0" de serie.
    char battery[header_capacity];
    header_amount(battery, header_capacity, input.slots[1]);
    u32 end = 0;
    while (battery[end]) ++end;
    battery[end] = '%'; battery[end + 1] = 0;  // caben: como mucho "100"
    header(f, 1, battery);
    // Boom y Blast: enteros, en lugar del "100.0" de serie.
    char amount[header_capacity];
    header_amount(amount, header_capacity, input.slots[2]);
    header(f, 2, amount);
    header_amount(amount, header_capacity, input.slots[3]);
    header(f, 3, amount);

    const i32 dt = clamp(input.elapsed, 0, 102);
    const bool held = input.held_notes > 0 && input.held_notes <= 128;
    bool note = false;
    if (!s.started) s.started = 1;
    else note = input.note_serial != s.serial;
    s.serial = input.note_serial;

    if (held || input.playing || note) s.coast = 256;  // un cuarto de segundo de inercia al soltar
    const bool running = s.coast > 0 && !input.reduced_motion;
    if (note) {
        s.last_bomb = !s.last_bomb;
        if (s.last_bomb) {
            s.press_b = 123;
            for (u32 i = 0; i < kBombs; ++i)
                if (!s.bombs[i].x) {
                    s.bombs[i].x = (kShipX + 3) * unit;
                    s.bombs[i].y = s.ship_y + kShipH * unit;
                    s.bombs[i].vy = 0;
                    break;
                }
        } else {
            s.press_m = 123;
            s.pending = kBurst;
            s.gap = 0;
        }
        if (boom > 0) s.shake = (boom >> 4) + 256;
    }
    if (running) update(s, dt, blast);
    if (!held && !input.playing) s.coast = s.coast > dt ? s.coast - dt : 0;
    s.press_m = s.press_m > dt ? s.press_m - dt : 0;
    s.press_b = s.press_b > dt ? s.press_b - dt : 0;

    // Dibujo: el marco conserva lo anterior, pero se coloca todo en cada imagen.
    const i32 shake = s.shake >> 10;
    const i32 mountain_x = snap((kLeft * unit) - s.scroll);
    for (u32 i = 0; i < 2; ++i)
        place(f, node::mountain + i, mountain_x + static_cast<i32>(i) * kMountainWidth * unit,
              (kBottom + shake) * unit, unit, 0, keep);
    const i32 ship_y = snap(s.ship_y);
    place(f, node::ship, kShipX * unit, ship_y, unit, 0, keep);
    if (running) {
        place(f, node::flame, (kShipX - 3) * unit, ship_y + 3 * unit, unit,
              (s.clock >> 6) & 1 ? shade::orange : shade::yellow, fill_and_stroke);
    } else {
        hide(f, node::flame);
    }
    for (u32 i = 0; i < kShots; ++i) {
        if (s.shots[i].x) place(f, node::shots + i, snap(s.shots[i].x), snap(s.shots[i].y), unit, 0, keep);
        else hide(f, node::shots + i);
    }
    for (u32 i = 0; i < kBombs; ++i) {
        if (s.bombs[i].x) place(f, node::bombs + i, snap(s.bombs[i].x), snap(s.bombs[i].y), unit, 0, keep);
        else hide(f, node::bombs + i);
    }
    const u32 flash = (static_cast<u32>(s.clock) >> 8) & 1u;
    for (u32 i = 0; i < kEnemies; ++i) {
        const Enemy& e = s.enemies[i];
        if (!e.x) { hide(f, node::enemies + i); continue; }
        const i32 wobble = sine(e.wobble) * 2;  // ±2 px
        place(f, node::enemies + i, snap(e.x), snap(e.y + wobble), unit,
              flash ^ (i & 1u) ? shade::dark_red : shade::red, fill_and_stroke);
    }
    for (u32 i = 0; i < kParticles; ++i) {
        const Particle& p = s.particles[i];
        if (p.life <= 0) { hide(f, node::particles + i); continue; }
        place(f, node::particles + i, snap(p.x), snap(p.y), static_cast<i32>(p.size) * 64, p.shade, fill_and_stroke);
    }
    draw_score(f, s.score);
    place(f, node::stick, kStickX, kStickY + s.tilt, unit, 0, keep);
    const bool chord = input.held_notes >= 2 && input.held_notes <= 128;
    press(f, node::missile, node::missile_glint, kMissileX, kMissileY,
          s.press_m > 0 || chord || (held && !s.last_bomb));
    press(f, node::bomb, node::bomb_glint, kBombX, kBombY,
          s.press_b > 0 || chord || (held && s.last_bomb));
}
}
