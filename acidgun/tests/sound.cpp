// Pruebas de sonido de Acidgun: python sdk.py test --machine ../acidgun
#include "../engine.cpp"
#include <cstdio>
#include <cmath>

namespace {
const unsigned B = opxy::block_frames;
acidgun::Voice voice;
acidgun::Shared shared;
opxy::i32 params[8];
opxy::u32 pitch[B];
opxy::i32 out[B];

opxy::u32 inc_of(double hz) { return static_cast<opxy::u32>(hz / 44100.0 * 4294967296.0); }
void set_pitch(double hz) { for (unsigned i = 0; i < B; ++i) pitch[i] = inc_of(hz); }
opxy::i32 q(double v) { return static_cast<opxy::i32>(v * 2147483647.0); }
void block() { acidgun::render(voice, shared, 1, out, params, pitch); }

// Frecuencia por cruces por cero ascendentes (filtro abierto, sin resonancia).
double measure(unsigned blocks) {
    int crossings = 0; opxy::i32 prev = 0; unsigned first = 0, lastc = 0, n = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        block();
        for (unsigned i = 0; i < B; ++i, ++n) {
            if (prev < 0 && out[i] >= 0) { if (!crossings) first = n; lastc = n; ++crossings; }
            prev = out[i];
        }
    }
    return crossings > 1 ? 44100.0 * (crossings - 1) / (lastc - first) : 0;
}
}

int main() {
    bool ok = true;
    // 1. Todas las esquinas de los controles: suena, sin pasarse del pico declarado, sin DC.
    for (unsigned combo = 0; combo < 256; ++combo) {
        for (unsigned j = 0; j < 8; ++j) params[j] = (combo >> j) & 1 ? 0x7fffffff : 0;
        acidgun::initialize(voice, shared, 1);
        acidgun::start(voice, shared, 1);
        set_pitch(combo & 16 ? 55.0 : 220.0);
        bool sounding = false, bounded = true;
        double dc = 0;
        for (unsigned b = 0; b < 700; ++b) {
            if (b == 350) { acidgun::start(voice, shared, 1); set_pitch(110.0); }
            block();
            for (unsigned i = 0; i < B; ++i) {
                sounding = sounding || out[i] > 1000000 || out[i] < -1000000;
                bounded = bounded && out[i] >= -1073741824 && out[i] <= 1073741824;
                dc += out[i] / 2147483648.0;
            }
        }
        dc /= 700.0 * B;
        if (!bounded) { std::printf("FAIL combo %u por encima del pico\n", combo); ok = false; }
        // Con el corte cerrado y Env Mod 0 puede quedar casi en silencio: solo se exige
        // sonido si hay corte o envolvente.
        if (!sounding && (combo & 5)) { std::printf("FAIL combo %u en silencio\n", combo); ok = false; }
        if (std::fabs(dc) > 0.02) { std::printf("FAIL combo %u DC %.3f\n", combo, dc); ok = false; }
    }

    // 2. Afinación: filtro abierto, tune 0 / +12 / -12 / +7.
    const double tunes[4] = {0.5, 1.0, 0.0, 0.5 + 7.0 / 24.0};
    const double expect[4] = {220.0, 440.0, 110.0, 220.0 * std::pow(2.0, 7.0 / 12.0)};
    for (unsigned t = 0; t < 4; ++t) {
        for (unsigned j = 0; j < 8; ++j) params[j] = 0;
        params[0] = q(0.9); params[5] = q(tunes[t]);
        params[4] = 0x7fffffff;  // sqr: cruces limpios
        acidgun::initialize(voice, shared, 1); acidgun::start(voice, shared, 1);
        set_pitch(220.0);
        measure(20);
        const double f = measure(200);
        const double cents = 1200.0 * std::log2(f / expect[t]);
        std::printf("tune %.3f: %.2f Hz (esperado %.2f, %+.1f cents)\n", tunes[t], f, expect[t], cents);
        if (std::fabs(cents) > 3) { std::puts("FAIL afinación"); ok = false; }
    }

    // 3. Slide: de 110 a 220 Hz; empieza abajo y llega.
    for (unsigned j = 0; j < 8; ++j) params[j] = 0;
    params[0] = q(0.9); params[4] = 0x7fffffff; params[5] = q(0.5); params[6] = q(0.66);  // 150 ms
    acidgun::initialize(voice, shared, 1); acidgun::start(voice, shared, 1);
    set_pitch(110.0); measure(100);
    acidgun::start(voice, shared, 1); set_pitch(220.0);
    const double early = measure(12), late = (measure(150), measure(100));
    std::printf("slide: al empezar %.1f Hz, al final %.1f Hz\n", early, late);
    if (!(early < 180.0 && std::fabs(late - 220.0) < 2.0)) { std::puts("FAIL slide"); ok = false; }

    // 4. Nivel: limpio vs drive a tope (no debe dispararse).
    double rms[2];
    for (unsigned d = 0; d < 2; ++d) {
        for (unsigned j = 0; j < 8; ++j) params[j] = 0;
        params[0] = q(0.4); params[1] = q(0.6); params[2] = q(0.5); params[3] = q(0.5);
        params[7] = d ? 0x7fffffff : 0;
        acidgun::initialize(voice, shared, 1);
        set_pitch(55.0);
        double acc = 0;
        for (unsigned b = 0; b < 1400; ++b) {
            if (b % 100 == 0) acidgun::start(voice, shared, 1);
            block();
            for (unsigned i = 0; i < B; ++i) acc += (out[i] / 2147483648.0) * (out[i] / 2147483648.0);
        }
        rms[d] = std::sqrt(acc / (1400.0 * B));
    }
    std::printf("rms limpio %.3f, drive %.3f (x%.2f)\n", rms[0], rms[1], rms[1] / rms[0]);
    if (rms[1] / rms[0] > 3.0 || rms[1] / rms[0] < 0.7) { std::puts("FAIL nivel del drive"); ok = false; }

    // 5. Mono: al empezar otra voz, la anterior se calla y la nueva sigue sin salto.
    {
        static acidgun::Voice v2;
        for (unsigned j = 0; j < 8; ++j) params[j] = 0;
        params[0] = q(0.5); params[1] = q(0.3); params[2] = q(0.3); params[3] = q(0.5);
        acidgun::initialize(voice, shared, 1); acidgun::initialize(v2, shared, 2);
        acidgun::start(voice, shared, 1); set_pitch(110.0);
        for (unsigned b = 0; b < 100; ++b) block();
        const opxy::i32 before = out[B - 1];
        acidgun::start(v2, shared, 2);
        opxy::i32 o1[B], o2[B];
        acidgun::render(voice, shared, 1, o1, params, pitch);
        acidgun::render(v2, shared, 2, o2, params, pitch);
        bool silent = true;
        for (unsigned i = 0; i < B; ++i) silent = silent && o1[i] == 0;
        const double jump = std::fabs((o2[0] - before) / 2147483648.0);
        std::printf("mono: voz vieja callada %s, salto al cambiar %.4f\n", silent ? "si" : "NO", jump);
        if (!silent || jump > 0.02) { std::puts("FAIL mono"); ok = false; }
    }
    // 6. Legato: cambio de altura sin nota nueva, con slide: se desliza.
    {
        for (unsigned j = 0; j < 8; ++j) params[j] = 0;
        params[0] = q(0.9); params[4] = 0x7fffffff; params[5] = q(0.5); params[6] = q(0.66);
        acidgun::initialize(voice, shared, 1); acidgun::start(voice, shared, 1);
        set_pitch(110.0); measure(100);
        set_pitch(220.0);  // sin start(): legato
        const double early = measure(12), late = (measure(200), measure(100));
        std::printf("legato: al empezar %.1f Hz, al final %.1f Hz\n", early, late);
        if (!(early < 180.0 && std::fabs(late - 220.0) < 2.0)) { std::puts("FAIL legato"); ok = false; }
    }

    std::puts(ok ? "OK" : "FALLOS");
    return ok ? 0 : 1;
}
