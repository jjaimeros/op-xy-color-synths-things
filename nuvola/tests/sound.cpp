// Comprobaciones de sonido de Nuvola (python sdk.py test --machine <carpeta>).
#include "../engine.cpp"
#include <cmath>
#include <cstdio>

namespace {
const opxy::i32 kFull = 0x7fffffff;
opxy::i32 at(double v) { return static_cast<opxy::i32>(v * kFull); }

struct Result { bool sounding, bounded; double rms, dc, freq, peak, worst; };

// Frecuencia estimada por cruces por cero ascendentes (señal suavizada).
Result run(const opxy::i32 (&params)[8], opxy::u32 increment, unsigned blocks) {
    static nuvola::Voice voice;
    static nuvola::Shared shared;
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = increment;
    opxy::i32 out[opxy::block_frames];
    opxy::i32 p[8];
    for (unsigned i = 0; i < 8; ++i) p[i] = params[i];
    nuvola::initialize(voice, shared, 1);
    nuvola::start(voice, shared, 1);
    Result r = {false, true, 0, 0, 0, 0, 0};
    double win = 0; unsigned wn = 0;
    double sum = 0, sq = 0, lp = 0, prev = 0; unsigned n = 0, crossings = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        nuvola::render(voice, shared, 1, out, p, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            r.sounding = r.sounding || out[i] != 0;
            r.bounded = r.bounded && out[i] >= -1073741824 && out[i] <= 1073741824;
            sum += out[i]; sq += double(out[i]) * out[i]; ++n;
            const double a = std::fabs(out[i] / 1073741824.0);
            if (a > r.peak) r.peak = a;
            // nivel RMS en ventanas de 50 ms: el pico de volumen que se oye
            win += a * a; if (++wn == 2205) { const double w = std::sqrt(win / wn); if (w > r.worst) r.worst = w; win = 0; wn = 0; }
            lp += (out[i] - lp) * 0.05;
            if (prev < 0 && lp >= 0) ++crossings;
            prev = lp;
        }
    }
    r.rms = std::sqrt(sq / n) / 1073741824.0; r.dc = sum / n / 1073741824.0;
    r.freq = crossings * 44100.0 / n;
    return r;
}
}

int main() {
    bool ok = true;
    const double waves[5] = {0.1, 0.3, 0.5, 0.7, 0.9};  // saw, sqr, pulse, sine, saw/sqr
    const opxy::u32 notes[3] = {6085000u, 42852281u, 194000000u};  // ~62 Hz, 440 Hz, ~2 kHz
    for (unsigned w = 0; w < 5; ++w)
    for (unsigned c = 0; c < 5; ++c)
    for (unsigned d = 0; d < 3; ++d)
    for (unsigned z = 0; z < 3; ++z)
    for (unsigned n = 0; n < 3; ++n) {
        const opxy::i32 p[8] = {at(waves[w]), at((c + 0.5) / 5), at(d * 0.5), at(z * 0.5), 0, 0, 0, 0};
        const Result r = run(p, notes[n], 300);
        if (!r.sounding) { std::printf("FAIL silencio w=%u c=%u\n", w, c); ok = false; }
        if (!r.bounded) { std::printf("FAIL satura w=%u c=%u d=%u z=%u n=%u\n", w, c, d, z, n); ok = false; }
        if (std::fabs(r.dc) > 0.05) { std::printf("FAIL continua %.3f w=%u c=%u\n", r.dc, w, c); ok = false; }
    }
    // Picos de volumen con 5 nubes y wind al máximo, 6 s: ventana más fuerte / media.
    for (unsigned w = 0; w < 5; ++w) {
        const opxy::i32 p[8] = {at(waves[w]), kFull, kFull, at(0.5), 0, 0, 0, 0};
        const Result r = run(p, 42852281u / 4, 4134);
        std::printf("onda %u, 5 nubes: rms %.3f, ventana más fuerte %.3f (x%.2f), pico %.3f\n", w, r.rms, r.worst, r.worst / r.rms, r.peak);
        if (r.worst > r.rms * 1.35) { std::puts("FAIL picos de volumen"); ok = false; }
    }
    // Afinación: saw, 1 nube, sin wind ni sun -> 220 y 440 Hz.
    const opxy::i32 pure[8] = {at(0.1), 0, 0, 0, 0, 0, 0, 0};
    const double f440 = run(pure, 42852281u, 700).freq, f220 = run(pure, 42852281u / 2, 700).freq;
    std::printf("afinación: %.1f Hz y %.1f Hz (esperado 440 y 220)\n", f440, f220);
    if (std::fabs(f440 - 440) > 5 || std::fabs(f220 - 220) > 3) { std::puts("FAIL no sigue la nota"); ok = false; }
    // Nivel parecido con 1 y 5 nubes.
    const opxy::i32 one[8] = {at(0.1), 0, at(0.5), 0, 0, 0, 0, 0}, eight[8] = {at(0.1), kFull, at(0.5), 0, 0, 0, 0, 0};
    const double r1 = run(one, 42852281u, 700).rms, r8 = run(eight, 42852281u, 700).rms;
    std::printf("nivel 1 nube %.3f, 5 nubes %.3f\n", r1, r8);
    if (r8 < r1 * 0.5 || r8 > r1 * 1.6) { std::puts("FAIL el nivel cambia demasiado con las nubes"); ok = false; }
    if (ok) std::puts("OK todas las combinaciones suenan, afinan y no saturan");
    return ok ? 0 : 1;
}
