// Comprobaciones de sonido de Ofender, ejecutadas por: python sdk.py test --machine <carpeta>
#include "../engine.cpp"
#include <cstdio>
#include <cmath>

namespace {
const opxy::i32 kFull = 0x7fffffff;
opxy::i32 at(double v) { return static_cast<opxy::i32>(v * kFull); }
opxy::i32 slot(unsigned i, unsigned count) { return at((i + 0.5) / count); }

struct Result { bool bounded; double dc, rms; unsigned active; };

Result run(unsigned armor, double battery, double kick, double blast, opxy::u32 increment, unsigned blocks) {
    static ofender::Voice voice;
    static ofender::Shared shared;
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = increment;
    opxy::i32 output[opxy::block_frames];
    const opxy::i32 p[4] = {slot(armor, 8), at(battery), at(kick), at(blast)};
    ofender::initialize(voice, shared, 1);
    ofender::start(voice, shared, 1);
    Result r = {true, 0, 0, 0};
    double sum = 0, sq = 0; unsigned n = 0; 
    for (unsigned b = 0; b < blocks; ++b) {
        ofender::render(voice, shared, 1, output, p, pitch);
        double e = 0;
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            r.bounded = r.bounded && output[i] >= -1073741824 && output[i] <= 1073741824;
            sum += output[i]; sq += static_cast<double>(output[i]) * output[i]; ++n;
            e += std::fabs(static_cast<double>(output[i]));
        }
        if (e / opxy::block_frames >= 100000.0) ++r.active;
    }
    r.dc = sum / n / 1073741824.0;
    r.rms = std::sqrt(sq / n) / 1073741824.0;
    return r;
}

// Mide un seno grave: cruces por cero en los últimos bloques y muestras iguales a la anterior.
struct Measure { unsigned crossings, repeated, total; };
Measure measure(double battery) {
    static ofender::Voice voice;
    static ofender::Shared shared;
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = 21426140u;  // 220 Hz
    opxy::i32 output[opxy::block_frames];
    const opxy::i32 p[4] = {slot(4, 8), at(battery), 0, 0};
    ofender::initialize(voice, shared, 1);
    ofender::start(voice, shared, 1);
    Measure m = {0, 0, 0};
    opxy::i32 last = 0;
    for (unsigned b = 0; b < 100; ++b) {
        ofender::render(voice, shared, 1, output, p, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            if ((last < 0) != (output[i] < 0)) ++m.crossings;
            if (output[i] == last) ++m.repeated;
            last = output[i];
            ++m.total;
        }
    }
    return m;
}

// Mide la altura del seno bloque a bloque por lo que avanza su fase: extremos y media, en semitonos.
struct Drift { double high, low, mean; };
Drift drift(double battery) {
    static ofender::Voice voice;
    static ofender::Shared shared;
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = 21426140u;  // 220 Hz
    opxy::i32 output[opxy::block_frames];
    const opxy::i32 p[4] = {slot(4, 8), at(battery), 0, 0};
    ofender::initialize(voice, shared, 1);
    ofender::start(voice, shared, 1);
    const double base = 21426140.0 * opxy::block_frames;
    Drift d = {-99, 99, 0};
    const unsigned cycle = 138;  // una vuelta de la oscilación
    for (unsigned b = 0; b < cycle; ++b) {
        const opxy::u32 before = voice.phase;
        ofender::render(voice, shared, 1, output, p, pitch);
        const double semitones = 12 * std::log2((voice.phase - before) / base);
        if (semitones > d.high) d.high = semitones;
        if (semitones < d.low) d.low = semitones;
        d.mean += semitones / cycle;
    }
    return d;
}
}

int main() {
    bool ok = true;
    const opxy::u32 notes[3] = {1947749u, 42852281u, 389566000u};  // ~20 Hz, 440 Hz, 4 kHz
    for (unsigned a = 0; a < 8; ++a)
    for (unsigned n = 0; n < 3; ++n)
    for (unsigned b = 0; b < 2; ++b)
    for (unsigned c = 0; c < 2; ++c) {
        const Result r = run(a, b, c, c, notes[n], 400);  // Battery a 0 % y al 100 %
        if (r.rms < 0.02) { std::printf("FAIL silencio armor=%u nota=%u battery=%u\n", a, n, b); ok = false; }
        if (!r.bounded) { std::printf("FAIL supera el pico armor=%u nota=%u battery=%u\n", a, n, b); ok = false; }
        if (n == 1 && !c && std::fabs(r.dc) > 0.08) { std::printf("FAIL continua %.3f armor=%u\n", r.dc, a); ok = false; }
    }
    // Battery: un seno (`sine`) debe oscilar ±1 semitono a 0 % sin bajar de tono, y retener muestras (1 de
    // cada 6,7); al 100 % no oscila ni retiene ninguna.
    const Measure full = measure(1), empty = measure(0);
    const Drift none = drift(1), half = drift(0.5), most = drift(0);
    if (most.high < 0.97 || most.high > 1.02 || most.low > -0.97 || most.low < -1.02 || std::fabs(most.mean) > 0.03) {
        std::printf("FAIL Battery a 0 %% oscila %+.3f..%+.3f, media %+.3f semitonos\n", most.low, most.high, most.mean); ok = false;
    }
    if (half.high < 0.4 || half.high > 0.6 || half.low > -0.4 || half.low < -0.6) {
        std::printf("FAIL Battery al 50 %% oscila %+.3f..%+.3f semitonos\n", half.low, half.high); ok = false;
    }
    if (none.high - none.low > 0.001) { std::printf("FAIL Battery al 100 %% oscila %+.3f..%+.3f\n", none.low, none.high); ok = false; }
    if (full.repeated > full.total / 100) { std::printf("FAIL Battery al 100 %% retiene %u de %u\n", full.repeated, full.total); ok = false; }
    if (empty.repeated < empty.total * 8 / 10 || empty.repeated > empty.total * 9 / 10) { std::printf("FAIL Battery a 0 %% retiene solo %u de %u\n", empty.repeated, empty.total); ok = false; }
    // Boom y Blast deben añadir energía al principio de la nota.
    const double dry = run(0, 1, 0, 0, notes[1], 60).rms;
    if (run(0, 1, 1, 0, notes[1], 60).rms <= dry * 1.1) { std::puts("FAIL Boom no se oye"); ok = false; }
    if (run(0, 1, 0, 1, notes[1], 60).rms <= dry * 1.1) { std::puts("FAIL Blast no se oye"); ok = false; }
    if (ok) std::puts("OK los ocho sonidos suenan sin pasar del pico, con Battery, bombo y explosión");
    return ok ? 0 : 1;
}
