/* Generates one 30 s, 8 kHz synthetic song from a seed: build/synth <seed> <out.wav> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rng.h"
#include "wav.h"

#define SR 8000
#define SECONDS 30
#define NYQUIST_GUARD_HZ 3800.0
#define MAX_PARTIALS 12
#define MAX_STEPS 128
#define NO_NOTE (-1000)

typedef struct {
    int partials;
    double rolloff;
    int odd_only;
    double attack, decay, sustain, release;
    double vibrato_hz, vibrato_depth;
} timbre;

typedef struct {
    float *buf;
    size_t n;
    fp_rng rng;
} song;

static double rnd(fp_rng *r, double lo, double hi)
{
    return lo + (hi - lo) * fp_rng_f32(r);
}

static int rint_in(fp_rng *r, int lo, int hi)
{
    return lo + (int)(fp_rng_u64(r) % (uint64_t)(hi - lo + 1));
}

static double adsr(const timbre *t, double age, double held)
{
    if (age < t->attack)
        return age / t->attack;
    if (age < t->attack + t->decay)
        return 1.0 - (1.0 - t->sustain) * (age - t->attack) / t->decay;
    if (age < held)
        return t->sustain;
    return t->sustain * exp(-(age - held) / t->release);
}

static void add_note(song *s, const timbre *t, double start, double held, double hz, double amp)
{
    const double total = held + 5.0 * t->release;
    const size_t i0 = (size_t)(start * SR);
    const size_t len = (size_t)(total * SR);
    double w[MAX_PARTIALS];
    for (int p = 0; p < t->partials; p++)
        w[p] = 1.0 / pow(t->odd_only ? 2 * p + 1 : p + 1, t->rolloff);
    const double phase0 = rnd(&s->rng, 0, 2 * M_PI);
    double phase = 0.0;
    for (size_t k = 0; k < len && i0 + k < s->n; k++) {
        const double age = (double)k / SR;
        const double env = adsr(t, age, held);
        const double f = hz * (1.0 + t->vibrato_depth * sin(2 * M_PI * t->vibrato_hz * age));
        phase += 2 * M_PI * f / SR;
        double v = 0.0;
        for (int c = 0; c < t->partials; c++) {
            const int p = t->odd_only ? 2 * c + 1 : c + 1;
            if (f * p > NYQUIST_GUARD_HZ)
                break;
            v += w[c] * sin(p * phase + phase0);
        }
        s->buf[i0 + k] += (float)(amp * env * v);
    }
}

static void add_kick(song *s, double start, double amp)
{
    const size_t i0 = (size_t)(start * SR);
    double phase = 0.0;
    for (size_t k = 0; k < (size_t)(0.25 * SR) && i0 + k < s->n; k++) {
        const double age = (double)k / SR;
        phase += 2 * M_PI * (50.0 + 110.0 * exp(-age * 30.0)) / SR;
        s->buf[i0 + k] += (float)(amp * exp(-age * 14.0) * sin(phase));
    }
}

static void add_snare(song *s, double start, double amp)
{
    const size_t i0 = (size_t)(start * SR);
    for (size_t k = 0; k < (size_t)(0.2 * SR) && i0 + k < s->n; k++) {
        const double age = (double)k / SR;
        const double noise = 2.0 * fp_rng_f32(&s->rng) - 1.0;
        const double body = sin(2 * M_PI * 190.0 * age);
        s->buf[i0 + k] += (float)(amp * exp(-age * 22.0) * (0.8 * noise + 0.4 * body));
    }
}

static void add_hat(song *s, double start, double amp)
{
    const size_t i0 = (size_t)(start * SR);
    double prev = 0.0;
    for (size_t k = 0; k < (size_t)(0.05 * SR) && i0 + k < s->n; k++) {
        const double age = (double)k / SR;
        const double w = 2.0 * fp_rng_f32(&s->rng) - 1.0;
        s->buf[i0 + k] += (float)(amp * exp(-age * 90.0) * (w - prev));
        prev = w;
    }
}

static const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
static const int MINOR[7] = {0, 2, 3, 5, 7, 8, 10};

static const int PROGRESSIONS[6][4] = {
    {0, 4, 5, 3}, {0, 5, 3, 4}, {5, 3, 0, 4}, {0, 3, 4, 3}, {0, 2, 5, 4}, {5, 4, 3, 4},
};

static double midi_hz(int m)
{
    return 440.0 * pow(2.0, (m - 69) / 12.0);
}

static int scale_midi(const int *scale, int root, int degree)
{
    int oct = degree >= 0 ? degree / 7 : -((6 - degree) / 7);
    return root + 12 * oct + scale[degree - 7 * oct];
}

static timbre random_timbre(fp_rng *r, int pad)
{
    timbre t;
    t.partials = rint_in(r, 4, MAX_PARTIALS);
    t.rolloff = rnd(r, 0.8, 1.7);
    t.odd_only = rint_in(r, 0, 2) == 0;
    t.attack = pad ? rnd(r, 0.08, 0.25) : rnd(r, 0.005, 0.04);
    t.decay = rnd(r, 0.05, 0.3);
    t.sustain = pad ? rnd(r, 0.6, 0.9) : rnd(r, 0.3, 0.7);
    t.release = pad ? rnd(r, 0.15, 0.4) : rnd(r, 0.04, 0.15);
    t.vibrato_hz = rnd(r, 4.5, 6.5);
    t.vibrato_depth = pad ? 0.0 : rnd(r, 0.0, 0.004);
    return t;
}

typedef struct {
    int degree[MAX_STEPS];
    int length[MAX_STEPS];
} melody;

/* Section-local random walk that lands on a chord tone at the start of each chord. */
static void make_melody(fp_rng *r, melody *m, int steps, int steps_per_chord, const int *prog, int base)
{
    int d = base + 2;
    for (int i = 0; i < steps; i++) {
        const int chord = prog[(i / steps_per_chord) % 4];
        if (i % steps_per_chord == 0) {
            d = base + chord + 2 * rint_in(r, 0, 2);
            m->degree[i] = d;
        } else if (rint_in(r, 0, 99) < 25) {
            m->degree[i] = NO_NOTE;
        } else {
            d += rint_in(r, -2, 2);
            if (d < base - 2)
                d = base - 2;
            if (d > base + 9)
                d = base + 9;
            m->degree[i] = d;
        }
        m->length[i] = rint_in(r, 0, 2) == 0 ? 2 : 1;
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <seed> <out.wav>\n", argv[0]);
        return 2;
    }
    song s;
    fp_rng_seed(&s.rng, strtoull(argv[1], NULL, 10));
    s.n = (size_t)SR * SECONDS;
    s.buf = calloc(s.n, sizeof *s.buf);
    if (!s.buf)
        return 1;
    fp_rng *r = &s.rng;

    const double bpm = rnd(r, 80, 160);
    const double beat = 60.0 / bpm;
    const int minor = rint_in(r, 0, 1);
    const int *scale = minor ? MINOR : MAJOR;
    const int root = rint_in(r, 45, 56);
    const int *prog = PROGRESSIONS[rint_in(r, 0, 5)];

    const int total_beats = (int)(SECONDS / beat);
    int chord_beats = total_beats / 16;
    if (chord_beats < 1)
        chord_beats = 1;
    if (chord_beats > 4)
        chord_beats = 4;
    const int section_beats = chord_beats * 4;
    const int steps_per_chord = chord_beats * 2;
    const int steps = section_beats * 2;

    timbre lead = random_timbre(r, 0);
    timbre pad = random_timbre(r, 1);
    timbre bass = random_timbre(r, 0);
    bass.partials = rint_in(r, 3, 6);
    bass.rolloff = rnd(r, 1.0, 2.0);
    const int use_arp = rint_in(r, 0, 1);
    timbre arp = random_timbre(r, 0);

    melody verse, chorus;
    make_melody(r, &verse, steps, steps_per_chord, prog, 7);
    make_melody(r, &chorus, steps, steps_per_chord, prog, 11);
    const int verse_bass_pattern = rint_in(r, 0, 2);
    const int chorus_bass_pattern = rint_in(r, 0, 2);

    for (int b = 0; (b + 1) * beat < SECONDS + beat; b++) {
        const int in_section = b % section_beats;
        const int is_chorus = (b / section_beats) % 2;
        const int chord_idx = (in_section / chord_beats) % 4;
        const int chord = prog[chord_idx];
        const double t0 = b * beat;

        if (in_section % chord_beats == 0) {
            const double held = chord_beats * beat;
            for (int k = 0; k < 3; k++)
                add_note(&s, &pad, t0, held, midi_hz(scale_midi(scale, root + 12, chord + 2 * k)), 0.10);
        }

        const int bass_pat = is_chorus ? chorus_bass_pattern : verse_bass_pattern;
        const double broot = midi_hz(scale_midi(scale, root - 12, chord));
        if (bass_pat == 0) {
            add_note(&s, &bass, t0, beat * 0.9, broot, 0.28);
        } else if (bass_pat == 1) {
            add_note(&s, &bass, t0, beat * 0.45, broot, 0.28);
            add_note(&s, &bass, t0 + beat * 0.5, beat * 0.45, in_section % 2 ? broot * 1.5 : broot, 0.24);
        } else if (in_section % 2 == 0) {
            add_note(&s, &bass, t0, beat * 1.8, broot, 0.28);
        }

        if (b % 2 == 0)
            add_kick(&s, t0, 0.55);
        else
            add_snare(&s, t0, 0.30);
        if (is_chorus && b % 4 == 3)
            add_kick(&s, t0 + beat * 0.5, 0.40);
        add_hat(&s, t0, 0.18);
        add_hat(&s, t0 + beat * 0.5, is_chorus ? 0.15 : 0.08);
        if (is_chorus) {
            add_hat(&s, t0 + beat * 0.25, 0.07);
            add_hat(&s, t0 + beat * 0.75, 0.07);
        }

        const melody *mel = is_chorus ? &chorus : &verse;
        for (int h = 0; h < 2; h++) {
            const int step = in_section * 2 + h;
            if (mel->degree[step] == NO_NOTE)
                continue;
            const double st = t0 + h * beat * 0.5;
            add_note(&s, &lead, st, mel->length[step] * beat * 0.5 * 0.9,
                     midi_hz(scale_midi(scale, root, mel->degree[step])), 0.16);
        }

        if (use_arp && is_chorus) {
            for (int h = 0; h < 4; h++) {
                const int deg = chord + 7 + 2 * ((h + in_section) % 3);
                add_note(&s, &arp, t0 + h * beat * 0.25, beat * 0.2, midi_hz(scale_midi(scale, root, deg)), 0.07);
            }
        }
    }

    float peak = 0.0f;
    for (size_t i = 0; i < s.n; i++)
        if (fabsf(s.buf[i]) > peak)
            peak = fabsf(s.buf[i]);
    if (peak > 0.0f)
        for (size_t i = 0; i < s.n; i++)
            s.buf[i] *= 0.8f / peak;

    printf("%.1f BPM, root MIDI %d %s\n", bpm, root, minor ? "minor" : "major");

    int rc = wav_write(argv[2], s.buf, s.n, SR);
    free(s.buf);
    if (rc) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    return 0;
}
