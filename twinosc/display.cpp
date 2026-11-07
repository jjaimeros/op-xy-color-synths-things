#include "display.h"
#include "wave.h"

// Pantalla de Twin Osc (diseño del usuario): dos cuadrados bajo cada encoder.
//   Encoder 1: onda de osc 1 y osc 2         Encoder 2: esas ondas deformadas por Shape
//   Encoder 3: Detune, líneas verdes que se separan; Octave, flechas arriba o abajo
//   Encoder 4: Mix, barra azul (osc 1) y blanca (osc 2); Sub, submarino amarillo y agua
namespace twinosc {
inline void display_reset(DisplayState& state) { state.phase = 0; state.header.reset(); }

namespace detail {
using namespace opxy::draw;
typedef opxy::u32 u32;
typedef opxy::i32 i32;

// Onda deformada por Shape: 1,5 ciclos como cinta de trazo grueso dentro de un cuadrado.
// Los contornos mutables no admiten curvas: las aristas se redondean suavizando la línea.
inline void shaped(Frame::path_type& path, u32 wave, i32 amount) {
    const Shape s = shape_for(wave, amount);
    const Rate none = {0, 0};
    path.key = 1 + wave + (static_cast<u32>(amount) << 2);  // cambia con la onda: si no, no se redibuja
    i32 raw[32];
    for (u32 i = 0; i < 32; ++i) {
        const u32 phase = i * (0xC0000000u / 31u * 2u) + 0x40000000u;  // 1,5 ciclos (la fase da la vuelta sola)
        const i32 v = clamp(oscillator(s, phase, none), -65536, 65536);
        raw[i] = -clamp((v >> 5) * 17, -21504, 21504);  // v / 32768 * 17 px en Q10, hasta ±21 px
    }
    for (u32 i = 0; i < 32; ++i) {
        const i32 x = static_cast<i32>(i) * 58 * unit / 31 - 29 * unit;
        const i32 y = (raw[i ? i - 1 : 0] + 2 * raw[i] + raw[i < 31 ? i + 1 : 31]) / 4;
        path.points[i].x = x;      path.points[i].y = y;
        path.points[63 - i].x = x; path.points[63 - i].y = y + unit / 2;
    }
}
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace detail;
    static const char* const waves[3] = {"saw", "sqr", "tri"};
    static const char* const octaves[5] = {"-2", "-1", "0", "+1", "+2"};
    const char* const titles[8] = {"Osc 1", "Shape 1", "Detune", "Mix", "Osc 2", "Shape 2", "Octave", "Sub"};

    const u32 wave1 = choice(input.slots[0], kWaves), wave2 = choice(input.slots[4], kWaves);
    const u32 octave = choice(input.slots[6], kOctaves);
    const i32 shape1 = q15(input.slots[1]), shape2 = q15(input.slots[5]);
    const i32 detune = q15(input.slots[2]), mix = q15(input.slots[3]), sub = q15(input.slots[7]);
    const u32 cents = static_cast<u32>((detune * 50 + 16384) >> 15);

    // Cabecera de dos páginas (valores en unidades reales).
    char values[8][header_capacity];
    copy(values[0], header_capacity, waves[wave1]);
    header_amount(values[1], header_capacity, input.slots[1]);
    char c[4] = {static_cast<char>(cents >= 10 ? '0' + cents / 10 : '0' + cents),
                 static_cast<char>(cents >= 10 ? '0' + cents % 10 : 'c'), static_cast<char>(cents >= 10 ? 'c' : 0), 0};
    copy(values[2], header_capacity, c);
    header_amount(values[3], header_capacity, input.slots[3]);
    copy(values[4], header_capacity, waves[wave2]);
    header_amount(values[5], header_capacity, input.slots[5]);
    copy(values[6], header_capacity, octaves[octave]);
    header_amount(values[7], header_capacity, input.slots[7]);
    const opxy::u8 slots[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, node::header, slots);

    // Encoder 1: icono de la onda elegida.
    const u32 wave[2] = {wave1, wave2};
    for (u32 r = 0; r < 2; ++r)
        for (u32 k = 0; k < 3; ++k) {
            const u32 id = node::icons + 3 * r + k;
            if (k == wave[r]) place(frame, id, 0, 0, unit, 0, keep);
            else hide(frame, id);
        }

    // Encoder 2: la onda tal como la deja Shape, en el centro de su cuadrado.
    shaped(frame.paths[node::shape1], wave1, shape1);
    shaped(frame.paths[node::shape2], wave2, shape2);
    place(frame, node::shape1, 18027 * unit / 100, 8504 * unit / 100, unit, 0, keep);
    place(frame, node::shape2, 17965 * unit / 100, 17295 * unit / 100, unit, 0, keep);

    // Encoder 3, arriba: seis líneas que nacen juntas y se separan con el detune (hasta ±34 px).
    const i32 detune_x = 30030 * unit / 100, detune_y = 8504 * unit / 100;
    const i32 spread = (detune * 34) >> 5;  // Q10 px de la línea más alejada
    for (u32 k = 0; k < 6; ++k) {
        const i32 slot = static_cast<i32>(k < 3 ? 5 - 2 * k : 2 * k - 5);  // 5, 3, 1, 1, 3, 5
        const i32 dx = spread * slot / 5;
        place(frame, node::lines + k, detune_x + (k < 3 ? -dx : dx), detune_y, unit, 0, keep);
    }

    // Encoder 3, abajo: una flecha por octava, arriba o abajo; en 0, una raya.
    const i32 oct = static_cast<i32>(octave) - 2;
    const i32 octave_x = 29987 * unit / 100, octave_y = 17295 * unit / 100;
    const Turn way = rotation(oct < 0 ? 1800 : 0);
    const i32 count = oct < 0 ? -oct : oct;
    if (count == 0) { hide(frame, node::arrow1); hide(frame, node::arrow2); }
    else {
        place(frame, node::arrow1, octave_x - (count == 2 ? 15 * unit : 0), octave_y, unit, 0, keep);
        turn(frame, node::arrow1, way);
        if (count == 2) {
            place(frame, node::arrow2, octave_x + 15 * unit, octave_y, unit, 0, keep);
            turn(frame, node::arrow2, way);
        } else hide(frame, node::arrow2);
    }
    if (count == 0) place(frame, node::flat, octave_x, octave_y, unit, 0, keep);
    else hide(frame, node::flat);

    // Encoder 4, arriba: cada barra sube con su parte del mix; el resto queda tras la tapa negra.
    {
        const i32 full = 44 * unit;
        const i32 white = (mix >> 5) * 44;  // Q10 px de la barra blanca
        if (white >= full - unit) hide(frame, node::bar1);  // sin parte en la mezcla: ni asoma
        else place(frame, node::bar1, 0, white, unit, 0, keep);
        if (white <= unit) hide(frame, node::bar2);
        else place(frame, node::bar2, 0, full - white, unit, 0, keep);
    }

    // Encoder 4, abajo: el agua sube por detrás del submarino con Sub.
    const bool notes = input.held_notes > 0 && input.held_notes <= 128;
    if ((notes || input.playing) && !input.reduced_motion)
        state.phase += static_cast<u32>(clamp(input.elapsed, 0, 102)) << 20;  // una vuelta cada 4 s
    // Con Sub a 0 ya hay agua hasta el 45 % del cuadrado; a tope lo llena (65 px de alto).
    {
        const u32 empty = static_cast<u32>(32767 - sub);
        i32 drop = static_cast<i32>((empty * 36608u) >> 15);  // 0 .. 35,75 px (55 % de 65) en Q10
        if (notes && !input.reduced_motion)
            drop += sine(static_cast<i32>(state.phase >> 22) * 10) / 1024 * unit;  // oleaje de ±1 px
        place(frame, node::water, 0, drop, unit, 0, keep);
    }

    // El submarino cabecea con play o al tocar: ±1,5 px de alto (0,5 Hz) y ±1 px de lado.
    // Se detiene donde esté al parar. Las vueltas son enteras: no hay salto al dar la vuelta la fase.
    const i32 turn_phase = static_cast<i32>(state.phase >> 16);
    const i32 sub_dx = sine((turn_phase * 3600) >> 16);
    const i32 sub_dy = sine((turn_phase * 7200) >> 16) * 3 / 2;
    for (u32 k = 0; k < node::submarine_parts; ++k)
        place(frame, node::submarine + k, sub_dx, sub_dy, unit, 0, keep);
}
}
