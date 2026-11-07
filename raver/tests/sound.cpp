// Comprobaciones de sonido de Raver (python sdk.py test --machine <carpeta>).
#include "../engine.cpp"
#include <cmath>
#include <cstdio>

namespace {
const opxy::i32 kFull = 0x7fffffff;
opxy::i32 at(double v) { return static_cast<opxy::i32>(v * kFull); }
struct Result { bool sounding, bounded; double rms, bright; };
// rms: nivel. bright: nivel de la diferencia entre muestras, que crece con los agudos.
Result run(double wave, double detune, double fenv, double lpf, double noise, double sine, double reso,
           opxy::u32 increment, unsigned from, unsigned blocks) {
    static raver::Voice voice;
    static raver::Shared shared;
    const opxy::i32 p[8] = {at(wave), at(detune), at(fenv), at(lpf), at(noise), at(sine), at(reso), 0};
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = increment;
    opxy::i32 out[opxy::block_frames];
    raver::initialize(voice, shared, 1);
    raver::start(voice, shared, 1);
    Result r = {false, true, 0, 0};
    double sq = 0, dq = 0, last = 0; unsigned n = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        raver::render(voice, shared, 1, out, p, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            r.sounding = r.sounding || out[i] != 0;
            r.bounded = r.bounded && out[i] >= -1073741824 && out[i] <= 1073741824;
            const double v = out[i] / 1073741824.0;
            if (b >= from) { sq += v * v; dq += (v - last) * (v - last); ++n; }
            last = v;
        }
    }
    r.rms = std::sqrt(sq / n); r.bright = std::sqrt(dq / n);
    return r;
}
}

int main() {
    bool ok = true;
    const double waves[3] = {1.0 / 6, 0.5, 5.0 / 6};
    const opxy::u32 notes[3] = {6085000u, 42852281u, 194000000u};
    for (unsigned w = 0; w < 3; ++w)
    for (unsigned d = 0; d < 3; ++d)
    for (unsigned n = 0; n < 3; ++n)
    for (unsigned l = 0; l < 3; ++l)
    for (unsigned q = 0; q < 2; ++q)
    for (unsigned x = 0; x < 2; ++x) {
        const Result r = run(waves[w], d * 0.5, 0.5, l * 0.5, x, x, q, notes[n], 0, 300);
        if (!r.sounding) { std::printf("FAIL silencio w=%u d=%u n=%u l=%u\n", w, d, n, l); ok = false; }
        if (!r.bounded) { std::printf("FAIL pico w=%u d=%u n=%u l=%u q=%u\n", w, d, n, l, q); ok = false; }
    }
    // El filtro funciona: cerrado apaga los agudos y abierto los deja pasar.
    const Result closed = run(waves[0], 0.5, 0, 0.1, 0, 0, 0, notes[1], 100, 600);
    const Result open = run(waves[0], 0.5, 0, 1, 0, 0, 0, notes[1], 100, 600);
    if (closed.bright * 20 > open.bright) { std::printf("FAIL LPF no filtra %f %f\n", closed.bright, open.bright); ok = false; }
    // La envolvente abre el filtro al empezar la nota y se cierra después.
    const Result attack = run(waves[0], 0.5, 1, 0.3, 0, 0, 0, notes[1], 0, 30);
    const Result tail = run(waves[0], 0.5, 1, 0.3, 0, 0, 0, notes[1], 1500, 1800);
    if (attack.bright < tail.bright * 2) { std::printf("FAIL envolvente %f %f\n", attack.bright, tail.bright); ok = false; }
    // La resonancia realza el corte.
    const Result dry = run(waves[0], 0.5, 0, 0.5, 0, 0, 0, notes[0], 100, 600);
    const Result reso = run(waves[0], 0.5, 0, 0.5, 0, 0, 1, notes[0], 100, 600);
    if (reso.bright < dry.bright * 1.5) { std::printf("FAIL reso %f %f\n", reso.bright, dry.bright); ok = false; }
    // Noise y Sine se suman.
    const Result plain = run(waves[2], 0, 0, 1, 0, 0, 0, notes[1], 100, 600);
    const Result noisy = run(waves[2], 0, 0, 1, 1, 0, 0, notes[1], 100, 600);
    if (noisy.bright < plain.bright * 3) { std::printf("FAIL noise %f %f\n", noisy.bright, plain.bright); ok = false; }
    std::printf("closed %.4f open %.4f attack %.4f tail %.4f dry %.4f reso %.4f plain %.4f noisy %.4f\n",
                closed.bright, open.bright, attack.bright, tail.bright, dry.bright, reso.bright, plain.bright, noisy.bright);
    return ok ? 0 : 1;
}
