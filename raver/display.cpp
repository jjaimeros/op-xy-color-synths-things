#include "display.h"

// Pantalla de Raver: un resplandor que dispara láseres hacia delante.
//   Mientras suenan notas, en cada semicorchea del tempo salen unos pocos rayos al azar,
//   cada uno con su duración y su desvío, y entran y salen con un fundido de 0,2 s.
//   Wave        saw y sqr: verde, azul y violeta;
//               sub: sin láseres; bajo el resplandor salen 4 tramos de onda hacia el público,
//               sin cruzarse con los otros, cada 2 negras (la mitad del tempo).
//   Detune      abre el abanico: cada foco tiene 7 rayos, uno por sierra, con el mismo
//               reparto irregular que sus desafinaciones.
// Nada se dibuja fuera de los 480 x 220 px: cada rayo se corta en el borde y lo oculto queda
// diminuto bajo la cabecera negra. Más línea fuera de la pantalla agota los bloques del dibujo.
namespace raver {
namespace detail {
using namespace opxy::draw;
typedef opxy::u32 u32;
typedef opxy::i32 i32;
typedef opxy::i16 i16;

// Verde, azul y violeta en la paleta, cada uno tenue, medio y entero.
const u32 kColors[3] = {0, 3, 6};
const u32 kFlat = 9;  // color de las ondas: tenue, medio y entero
// Posición de cada rayo en el abanico (Q10), proporcional a la desafinación de cada sierra.
const i32 kFan[kBeams] = {-1024, -585, -182, 0, 185, 579, 1000};
// Hacia dónde apunta cada foco, en décimas de grado (0 = derecha, 900 = abajo).
const i32 kAim[kFixtures] = {1300, 900, 500};
const i32 kSpread = 800;   // apertura máxima a cada lado, en décimas de grado
const u32 kStepTicks = 24;       // semicorcheas
const u32 kWaveTicks = 192;      // una onda nueva cada 2 negras: mitad del tempo
const u32 kCycleTicks = kWaveTicks * kWaves;  // lo que tarda cada onda en salir de la pantalla
const i32 kFade = 5;        // 1 / 0,2 s
// Los rayos van aplastados en vertical desde su vértice (edición del usuario: 29 %). Los nodos
// solo giran y escalan, así que el aplastado se traduce en un ángulo más plano.
const i32 kFlatten = 300;  // Q10
const i32 kBeamLength = 300;  // largo del rayo en el dibujo, en px
const i32 kBeamMargin = 5;    // media anchura del rayo y su borde, para no pasarse de la pantalla
const i32 kWaveScale = 1075;  // tamaño final de las ondas: justo dentro de la pantalla
const i32 kScreenRight = 480, kScreenBottom = 220, kHeaderBottom = 38;
const i32 kParkX = 10 * unit, kParkY = 10 * unit, kParkScale = 16;  // 1/64, bajo la cabecera

inline u32 root(u32 x) {
    u32 r = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}
inline Turn flattened(i32 tenths) {
    const i32 c = sine(tenths + 900), v = (sine(tenths) * kFlatten) >> 10;
    const i32 length = static_cast<i32>(root(static_cast<u32>(c * c + v * v)));
    const i32 safe = length > 0 ? length : 1;
    const Turn out = {c * unit / safe, v * unit / safe};
    return out;
}
// Escala con la que un rayo que sale de (x, y) con ese giro acaba justo en el borde.
inline i32 reach_edge(i32 x, i32 y, const Turn& turn) {
    const i32 ac = turn.cos < 0 ? -turn.cos : turn.cos, as = turn.sin < 0 ? -turn.sin : turn.sin;
    i32 best = 2 * kBeamLength;
    if (ac > 16) {
        const i32 room = (turn.cos > 0 ? kScreenRight - x : x) - 1 - ((kBeamMargin * as) >> 10);
        const i32 d = room * unit / ac;
        if (d < best) best = d;
    }
    if (as > 16) {
        const i32 room = (turn.sin > 0 ? kScreenBottom - y : y - kHeaderBottom) - 1 - ((kBeamMargin * ac) >> 10);
        const i32 d = room * unit / as;
        if (d < best) best = d;
    }
    return clamp(best * unit / kBeamLength, kParkScale, 2 * unit);
}
template <class F>
inline void park(F& frame, u32 id) { place(frame, id, kParkX, kParkY, kParkScale, 0, keep); }

inline u32 next(u32& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
inline i32 rnd(u32& s, i32 range) { return static_cast<i32>((next(s) >> 8) % static_cast<u32>(range)); }
inline i32 q15(i32 value) { return value < 0 ? 0 : value >> 16; }
}

inline void display_reset(DisplayState& s) {
    using namespace detail;
    s.header.reset();
    s.started = 0; s.serial = 0; s.random = 0x6C8E9CF5u; s.linger = 0; s.tick = 0;
    s.step = 0xffffffffu; s.opening = 0;
    for (u32 i = 0; i < kFixtures * kBeams; ++i) {
        Beam& b = s.beams[i];
        b.level = 0; b.hold = 0; b.angle = 0;
    }
}

inline void display(DisplayState& s, const opxy::draw::Input& input, Frame& f) {
    using namespace opxy::draw;
    using namespace detail;
    const u32 picked = (static_cast<u32>(q15(input.slots[0])) * 3u) >> 15;
    const u32 wave = picked > 2u ? 2u : picked;
    const bool sub = wave == 2;
    const i32 detune = q15(input.slots[1]), raw = q15(input.slots[2]);
    const i32 depth = (raw * raw) >> 15;  // el centro del pote vale 25, como en el motor

    static const char* const waves[3] = {"saw", "sqr", "sub"};
    const char* const titles[8] = {"wave", "detune", "filter env", "filter", "noise", "sine", "reso", ""};
    char values[8][header_capacity] = {};
    copy(values[0], header_capacity, waves[wave]);
    if (sub) copy(values[1], header_capacity, "off");
    else header_amount(values[1], header_capacity, input.slots[1]);
    header_amount(values[2], header_capacity, depth << 16);
    for (u32 i = 3; i < 7; ++i) header_amount(values[i], header_capacity, input.slots[i]);
    const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 3}};
    page_header(s.header, input, f, titles, values, node::bars, pages);

    const i32 dt = clamp(input.elapsed, 0, 102);
    bool note = false;
    if (!s.started) s.started = 1;
    else note = input.note_serial != s.serial;
    s.serial = input.note_serial;
    const bool held = input.held_notes > 0 && input.held_notes <= 128;
    if (note) s.linger = 154;
    else s.linger = s.linger > dt ? s.linger - dt : 0;
    const bool sounding = held || s.linger > 0;

    // Reloj: la posición de la canción si está en marcha; si no, uno propio al tempo del proyecto.
    const u32 bpm = input.bpm_x10 ? input.bpm_x10 : 1200u;
    u32 ticks;
    if (input.playing) {
        ticks = input.ticks96 % kCycleTicks;
    } else {
        if (note && input.held_notes <= 1) s.tick = 0;
        else s.tick = (s.tick + static_cast<u32>(dt) * bpm * 4u / 25u) % (kCycleTicks << 10);
        ticks = s.tick >> 10;
    }
    const u32 step = ticks / kStepTicks;
    const bool new_step = step != s.step || note;
    s.step = step;

    // En cada paso salen unos pocos rayos que estén apagados, elegidos al azar.
    if (!sub && sounding && new_step) {
        i32 count = input.reduced_motion ? static_cast<i32>(kFixtures * kBeams) : 2 + rnd(s.random, 4);
        for (i32 tries = 0; tries < 40 && count > 0; ++tries) {
            Beam& b = s.beams[input.reduced_motion ? static_cast<u32>(tries) % (kFixtures * kBeams)
                                                   : static_cast<u32>(rnd(s.random, kFixtures * kBeams))];
            if (b.level > 0 || b.hold > 0) continue;
            b.hold = static_cast<i16>(60 + rnd(s.random, 340));
            b.angle = static_cast<i16>(input.reduced_motion ? 0 : rnd(s.random, 241) - 120);
            --count;
        }
    }
    const i32 fade = dt * kFade;
    for (u32 fx = 0; fx < kFixtures; ++fx) {
        i32 brightest = 0;
        const i32 x = kFixtureX[fx] * unit, y = kFixtureY * unit;
        for (u32 k = 0; k < kBeams; ++k) {
            Beam& b = s.beams[fx * kBeams + k];
            const u32 id = node::beams + fx * kBeams + k;
            // Encendido mientras le quede tiempo y suene la nota; con cada cambio, fundido.
            const bool wanted = !sub && sounding && b.hold > 0;
            if (b.hold > 0) b.hold = static_cast<i16>(b.hold > dt ? b.hold - dt : 0);
            if (input.reduced_motion && wanted) b.hold = 1000;
            const i32 level = wanted ? b.level + fade : b.level - fade;
            b.level = static_cast<i16>(clamp(level, 0, unit));
            if (sub) b.level = 0;
            if (!b.level) { park(f, id); continue; }
            if (b.level > brightest) brightest = b.level;
            const i32 angle = kAim[fx] + b.angle + (((kFan[k] * kSpread) >> 10) * (detune >> 5)) / 1024;
            const u32 shade = kColors[(k + fx) % 3] + (b.level < 342 ? 0u : b.level < 683 ? 1u : 2u);
            const Turn flat = flattened(angle);
            place(f, id, x, y, reach_edge(kFixtureX[fx], kFixtureY, flat), shade, fill_and_stroke);
            turn(f, id, flat);
        }
        if (brightest) place(f, node::heads + fx, x, y, unit, kColors[fx] + (brightest < 342 ? 0u : brightest < 683 ? 1u : 2u), fill_and_stroke);
        else park(f, node::heads + fx);
    }

    // Modo sub: de cada origen sale un tramo de onda que se abre hacia el público, algo más tenue
    // al alejarse. Sale una por origen cada 2 negras y tarda 6 en cruzar la pantalla.
    s.opening = clamp(s.opening + (sub && sounding ? fade : -fade), 0, unit);
    for (u32 wave_index = 0; wave_index < kWaves; ++wave_index) {
        const u32 age = input.reduced_motion ? wave_index * kWaveTicks
                                             : (ticks + wave_index * kWaveTicks) % kCycleTicks;
        const i32 scale = 16 + static_cast<i32>((age * static_cast<u32>(kWaveScale - 16)) / kCycleTicks);
        const i32 level = (s.opening * static_cast<i32>(kCycleTicks - age)) / static_cast<i32>(kCycleTicks);
        for (u32 c = 0; c < kSources; ++c) {
            const u32 id = node::waves + wave_index * kSources + c;
            if (s.opening > 0) place(f, id, kWaveX[c], kWaveY, scale, kFlat + (level < 342 ? 0u : level < 683 ? 1u : 2u), fill_and_stroke);
            else park(f, id);
        }
    }
}
}
