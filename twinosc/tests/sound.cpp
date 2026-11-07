// Comprobaciones de sonido de Twin Osc, compiladas para el ordenador y
// ejecutadas por: python sdk.py test --machine <carpeta>
#include "../engine.cpp"
#include <cstdio>
#include <cmath>

namespace {
const opxy::i32 kFull = 0x7fffffff;
opxy::i32 at(double v) { return static_cast<opxy::i32>(v * kFull); }

struct Result { bool sounding, bounded; double dc, rms; };

Result run(const opxy::i32 (&params)[8], opxy::u32 increment, unsigned blocks) {
    static twinosc::Voice voice;
    static twinosc::Shared shared;
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = increment;
    opxy::i32 output[opxy::block_frames];
    opxy::i32 p[8];
    for (unsigned i = 0; i < 8; ++i) p[i] = params[i];
    twinosc::initialize(voice, shared, 1);
    twinosc::start(voice, shared, 1);
    Result r = {false, true, 0, 0};
    double sum = 0, sq = 0; unsigned n = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        twinosc::render(voice, shared, 1, output, p, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            r.sounding = r.sounding || output[i] != 0;
            r.bounded = r.bounded && output[i] >= -1073741824 && output[i] <= 1073741824;
            sum += output[i]; sq += static_cast<double>(output[i]) * output[i]; ++n;
        }
    }
    r.dc = sum / n / 1073741824.0;
    r.rms = std::sqrt(sq / n) / 1073741824.0;
    return r;
}
}

int main() {
    bool ok = true;
    const double waves[3] = {0.1, 0.5, 0.9};          // saw, sqr, tri
    const double octaves[5] = {0.1, 0.3, 0.5, 0.7, 0.9};  // -2..+2
    const opxy::u32 notes[3] = {1947749u, 42852281u, 389566000u};  // ~20 Hz, 440 Hz, 4 kHz
    for (unsigned w1 = 0; w1 < 3; ++w1)
    for (unsigned w2 = 0; w2 < 3; ++w2)
    for (unsigned o = 0; o < 5; ++o)
    for (unsigned n = 0; n < 3; ++n)
    for (unsigned corner = 0; corner < 4; ++corner) {
        // Esquinas: todo a 0 y todo al máximo en shape/detune/sub, con mezcla 0, 0.5 y 1.
        const double hi = (corner & 1) ? 1.0 : 0.0, mix = corner == 0 ? 0.0 : corner == 3 ? 1.0 : 0.5;
        const opxy::i32 params[8] = {at(waves[w1]), at(hi), at(hi), at(mix),
                                     at(waves[w2]), at(hi), at(octaves[o]), at(hi)};
        const Result r = run(params, notes[n], 40);
        if (!r.sounding) { std::printf("FAIL silencio w1=%u w2=%u oct=%u nota=%u esquina=%u\n", w1, w2, o, n, corner); ok = false; }
        if (!r.bounded) { std::printf("FAIL supera el pico declarado w1=%u w2=%u oct=%u nota=%u esquina=%u\n", w1, w2, o, n, corner); ok = false; }
        if (n == 1 && std::fabs(r.dc) > 0.05) { std::printf("FAIL componente continua %.3f w1=%u w2=%u esquina=%u\n", r.dc, w1, w2, corner); ok = false; }
    }
    // El sub debe sumar energía: osc 1 solo frente a osc 1 + sub.
    const opxy::i32 dry[8] = {at(0.1), 0, 0, 0, at(0.1), 0, at(0.5), 0};
    const opxy::i32 wet[8] = {at(0.1), 0, 0, 0, at(0.1), 0, at(0.5), kFull};
    if (run(wet, 42852281u, 40).rms <= run(dry, 42852281u, 40).rms * 1.2) { std::puts("FAIL el sub no se oye"); ok = false; }
    if (ok) std::puts("OK todas las combinaciones suenan, sin saturar y sin componente continua");
    return ok ? 0 : 1;
}
