#pragma once
// PULSE engine (Nov 16 2026; spec: liquidnick_site_work sessions/2026-11-16_bass_music_research.md, "The triggerable
// LFO / VCA voice", Copilot spec + Gemini panel). A triggerable LFO / VCA voice: one oscillator from 0.01 Hz to audio,
// WAVE morph sine - tri - saw - square (polyBLEP on the saw / square edges), PHASE start, pitch sweep with an exp curve,
// ADSR (exp / lin / log), 2-pole LP / BP morph filter with a soft-limited resonance, LEVEL (clean VCA up to 1, saturation
// above), FLUTTER + TIGHTNESS. No Rack headers: dev/pulse_test.cpp renders it offline (tools/pulse_test.sh).
//
// Modes: FREE  = free-running LFO / drone, the VCA stays open, TRIG / GATE re-sync the phase + sweep and run ENV;
//                EOC = every oscillator cycle.
//        LOOP  = the envelope cycles A - D - R by itself (while GATE is high, or always when GATE is unpatched); every
//                cycle restarts the phase + sweep; EOC = every cycle's end.
//        1-SHOT = TRIG plays A - D - R, GATE plays ADSR (holds SUSTAIN); EOC when the release ends.
// Guards: a restart crossfades the old oscillator into the new phase over 2 ms (no clicks), the AUDIO out is DC-blocked,
// has an optional 30 Hz roll-off and a soft cap at +-5 V.
#include <cmath>
#include <cstdint>

namespace pulse {

static const float kPi = 3.14159265358979f;
static const float kFlutterSt = 1.2f;     // FLUTTER 1 = about +-1.2 semitones of periodic wobble
static const float kXfadeSec = 0.002f;    // phase-reset crossfade

enum Mode { FREE = 0, LOOP = 1, ONESHOT = 2 };
enum Curve { EXP = 0, LIN = 1, LOG = 2 };

struct Params {
    int mode;
    float tuneHz, wave, phaseDeg;
    float sweepSt, sweepTime;
    float att, dec, sus, rel;
    int curve;
    float cutoffHz, res, morph;
    float level;
    float flutter, tight;
    bool rolloff, lfoUni;
    Params()
        : mode(ONESHOT), tuneHz(38.f), wave(0.f), phaseDeg(90.f), sweepSt(24.89f), sweepTime(0.6f), att(0.0005f),
          dec(0.9f), sus(0.f), rel(0.25f), curve(EXP), cutoffHz(20000.f), res(0.f), morph(0.f), level(1.f),
          flutter(0.f), tight(0.5f), rolloff(false), lfoUni(false) {}
};

// factory sounds (module menu; RANDROYD can apply them by number)
static const int kNumPresets = 8;   // 3-7: Gemini's presets (Nov 16 2026; WAVE 0..1 there = 0..3 here)
inline const char* presetName(int i) {
    static const char* n[kNumPresets] = {"ULTRA-LOW KICK", "SMOOTH SUB", "FLUTTER PAD", "808 BOOM", "SUB DROP", "WOBBLE LFO", "TREMOLO LFO", "PSY CHIRP"};
    return (i >= 0 && i < kNumPresets) ? n[i] : "";
}
inline Params preset(int i) {
    Params p;
    if (i == 0) {          // 160 -> 38 Hz over ~600 ms, punchy 90 deg start, long boom
        p.mode = ONESHOT; p.tuneHz = 38.f; p.wave = 0.f; p.phaseDeg = 90.f; p.sweepSt = 24.89f; p.sweepTime = 0.6f;
        p.att = 0.0005f; p.dec = 1.2f; p.sus = 0.f; p.rel = 0.3f; p.curve = EXP; p.cutoffHz = 20000.f; p.res = 0.f;
        p.morph = 0.f; p.level = 1.6f; p.flutter = 0.f; p.tight = 0.5f; p.rolloff = false;
    } else if (i == 1) {   // gate-held sub: a hint of triangle, a tiny attack chirp, low-passed, rolled off under 30 Hz
        p.mode = ONESHOT; p.tuneHz = 55.f; p.wave = 0.35f; p.phaseDeg = 0.f; p.sweepSt = 2.f; p.sweepTime = 0.04f;
        p.att = 0.003f; p.dec = 0.3f; p.sus = 0.8f; p.rel = 0.15f; p.curve = EXP; p.cutoffHz = 400.f; p.res = 0.1f;
        p.morph = 0.f; p.level = 1.f; p.flutter = 0.f; p.tight = 0.5f; p.rolloff = true;
    } else if (i == 2) {   // slow looping tri-saw pad, loose tape wobble
        p.mode = LOOP; p.tuneHz = 110.f; p.wave = 1.7f; p.phaseDeg = 0.f; p.sweepSt = 0.f; p.sweepTime = 0.2f;
        p.att = 1.5f; p.dec = 2.f; p.sus = 0.6f; p.rel = 3.f; p.curve = LIN; p.cutoffHz = 1200.f; p.res = 0.35f;
        p.morph = 0.3f; p.level = 1.2f; p.flutter = 0.6f; p.tight = 0.25f; p.rolloff = true;
    } else if (i == 3) {   // 808 BOOM: E1, two-octave drop in 75 ms, long tail, a touch of drive
        p.mode = ONESHOT; p.tuneHz = 41.2f; p.wave = 0.24f; p.phaseDeg = 90.f; p.sweepSt = 24.f; p.sweepTime = 0.075f;
        p.att = 0.002f; p.dec = 1.45f; p.sus = 0.f; p.rel = 0.25f; p.curve = EXP; p.cutoffHz = 220.f; p.res = 0.2f;
        p.morph = 0.f; p.level = 1.55f; p.flutter = 0.f; p.tight = 0.5f; p.rolloff = false;
    } else if (i == 4) {   // SUB DROP: a long falling sub for drops (E3 -> A0 over 2.4 s), slow loose wobble
        p.mode = ONESHOT; p.tuneHz = 27.5f; p.wave = 0.15f; p.phaseDeg = 0.f; p.sweepSt = 31.f; p.sweepTime = 2.4f;
        p.att = 0.015f; p.dec = 3.2f; p.sus = 0.f; p.rel = 0.5f; p.curve = EXP; p.cutoffHz = 180.f; p.res = 0.3f;
        p.morph = 0.f; p.level = 1.25f; p.flutter = 0.12f; p.tight = 0.25f; p.rolloff = false;
    } else if (i == 5) {   // WOBBLE LFO: a looping 2.33 Hz shape for a filter / VCA (use the LFO out)
        p.mode = LOOP; p.tuneHz = 2.33f; p.wave = 1.f; p.phaseDeg = 0.f; p.sweepSt = 0.f; p.sweepTime = 0.01f;
        p.att = 0.04f; p.dec = 0.18f; p.sus = 0.5f; p.rel = 0.15f; p.curve = EXP; p.cutoffHz = 650.f; p.res = 0.55f;
        p.morph = 0.15f; p.level = 1.4f; p.flutter = 0.35f; p.tight = 0.65f; p.rolloff = false;
    } else if (i == 6) {   // TREMOLO LFO: 6 Hz for a VCA (use the LFO out)
        p.mode = LOOP; p.tuneHz = 6.f; p.wave = 0.45f; p.phaseDeg = 0.f; p.sweepSt = 0.f; p.sweepTime = 0.01f;
        p.att = 0.01f; p.dec = 0.12f; p.sus = 0.6f; p.rel = 0.2f; p.curve = EXP; p.cutoffHz = 4200.f; p.res = 0.1f;
        p.morph = 0.f; p.level = 1.f; p.flutter = 0.15f; p.tight = 0.45f; p.rolloff = false;
    } else if (i == 7) {   // PSY CHIRP: A6 -> A2 in 60 ms through a resonant band-pass, tight flutter
        p.mode = ONESHOT; p.tuneHz = 110.f; p.wave = 1.98f; p.phaseDeg = 45.f; p.sweepSt = 48.f; p.sweepTime = 0.06f;
        p.att = 0.001f; p.dec = 0.14f; p.sus = 0.f; p.rel = 0.04f; p.curve = EXP; p.cutoffHz = 2800.f; p.res = 0.78f;
        p.morph = 0.85f; p.level = 1.6f; p.flutter = 0.55f; p.tight = 0.92f; p.rolloff = false;
    }
    return p;
}

// ---------------------------------------------------------------- helpers
inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float wrap01(float x) { return x - std::floor(x); }
// tanh-like, exact 1 at |x| >= 3
inline float satur(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
// identity up to the knee, then bends smoothly toward ceil (never reaches it)
inline float softLim(float x, float knee, float ceil) {
    float a = std::fabs(x);
    if (a <= knee) return x;
    float d = a - knee, r = ceil - knee;
    float y = knee + d / (1.f + d / r);
    return x < 0.f ? -y : y;
}
inline float blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}
// one shape at phase p (0 = upward zero crossing for sine / tri / saw)
inline float shapeOf(int i, float p, float dt) {
    switch (i) {
        case 0: return std::sin(2.f * kPi * p);
        case 1: return p < 0.25f ? 4.f * p : (p < 0.75f ? 2.f - 4.f * p : 4.f * p - 4.f);
        case 2: { float q = wrap01(p + 0.5f); return 2.f * q - 1.f - blep(q, dt); }
        default: return (p < 0.5f ? 1.f : -1.f) + blep(p, dt) - blep(wrap01(p + 0.5f), dt);
    }
}
// WAVE 0..3: sine - tri - saw - square, neighbours crossfaded
inline float morphWave(float w, float p, float dt) {
    w = clampf(w, 0.f, 3.f);
    int i = (int) w;
    if (i >= 3) return shapeOf(3, p, dt);
    float f = w - (float) i;
    if (f < 1e-4f) return shapeOf(i, p, dt);
    return shapeOf(i, p, dt) * (1.f - f) + shapeOf(i + 1, p, dt) * f;
}
inline float curveShape(float p, int c) {
    if (c == LIN) return p;
    float q = 1.f - p;
    if (c == LOG) return p * p * p;   // slow start
    return 1.f - q * q * q;           // EXP: fast start, long tail (analog-like)
}

// ---------------------------------------------------------------- FLUTTER (one per module, shared by the voices)
// TIGHTNESS t (Copilot, from wow / flutter research): rate = 0.15 * 80^t Hz; wow mix (1-t)^1.5 (sine), fast mix sqrt(t)
// (triangle: a periodic buzz), random jitter depth*0.35*(1-t)^2 slewed by 0.35*(1-t)^2 + 2 ms, AM depth*0.30*(1-t).
// Loose = wandering tape wobble, tight = fast shallow periodic buzz.
struct Flutter {
    float ph = 0.f, noise = 0.f, target = 0.f, hold = 0.f;
    uint32_t rng = 0x9e3779b9u;
    float lastD = -1.f, lastT = -1.f, lastSr = 0.f;
    float rate = 0.f, wm = 0.f, fm = 0.f, jit = 0.f, amAmt = 0.f, slewK = 0.f;
    float pitchSt = 0.f, am = 1.f;   // outputs
    float rnd() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) * (2.f / 16777216.f) - 1.f; }
    void process(float depth, float t, float sr) {
        depth = clampf(depth, 0.f, 1.f);
        t = clampf(t, 0.f, 1.f);
        if (depth != lastD || t != lastT || sr != lastSr) {
            lastD = depth; lastT = t; lastSr = sr;
            rate = 0.15f * std::pow(80.f, t);
            wm = std::pow(1.f - t, 1.5f);
            fm = std::sqrt(t);
            float l2 = (1.f - t) * (1.f - t);
            jit = depth * 0.35f * l2;
            amAmt = depth * 0.30f * (1.f - t);
            slewK = 1.f - std::exp(-1.f / ((0.35f * l2 + 0.002f) * sr));
        }
        ph += rate / sr;
        if (ph >= 1.f) ph -= 1.f;
        if (depth <= 0.f) { pitchSt = 0.f; am = 1.f; return; }
        float wow = std::sin(2.f * kPi * ph);
        float fast = shapeOf(1, ph, 0.f);
        hold -= 1.f / sr;
        if (hold <= 0.f) { target = rnd(); hold = 1.f / (4.f * rate + 1.f); }
        noise += (target - noise) * slewK;
        float per = (wm * wow + fm * fast) / (wm + fm);   // -1..1
        pitchSt = kFlutterSt * (depth * (wm + fm) * per + jit * noise);
        am = 1.f - amAmt * 0.5f * (1.f - per);
    }
};

// ---------------------------------------------------------------- one voice
struct VoiceIn {
    bool trig = false;          // rising edge this sample (TRIG jack or the TRIG key)
    bool gate = false, gatePatched = false;
    float pitchOct = 0.f;       // V/OCT + RATE CV, octaves
    float cutoffOct = 0.f;      // CUTOFF CV, octaves
    float vca = 1.f;            // VCA CV 0..1
};
struct VoiceOut {
    float audio = 0.f, lfo = 0.f, env = 0.f, hz = 0.f;
    bool eoc = false;
};

struct Voice {
    // oscillator + 2 ms crossfade ghost
    float ph = 0.f, dt = 0.f, gph = 0.f, gdt = 0.f;
    int xf = 0;
    float sweep = 0.f;          // 1 at a restart, decays exp
    // envelope
    int stage = 0;              // 0 idle, 1 A, 2 D, 3 S, 4 R
    float level = 0.f, from = 0.f, to = 0.f, p = 0.f;
    bool gateWas = false;
    // filter
    float ic1 = 0.f, ic2 = 0.f, fcLast = -1.f, resLast = -1.f, k = 2.f, a1 = 0.f, a2 = 0.f, a3 = 0.f;
    // DC block + 30 Hz roll-off
    float dcx = 0.f, dcy = 0.f, hz1 = 0.f, hz2 = 0.f;
    bool started = false;
};

struct Engine {
    float sr = 48000.f;
    int xfLen = 96;
    float dcR = 0.f;
    float hb0 = 1.f, hb1 = 0.f, hb2 = 0.f, ha1 = 0.f, ha2 = 0.f;   // 30 Hz Butterworth high-pass
    float sweepT = -1.f, sweepK = 0.f, tuneHz = -1.f, tuneOct = 0.f;
    Flutter flutter;

    void init(float sampleRate) {
        sr = sampleRate;
        xfLen = (int) std::lround(kXfadeSec * sr);
        if (xfLen < 1) xfLen = 1;
        dcR = std::exp(-2.f * kPi * 5.f / sr);
        float K = std::tan(kPi * 30.f / sr), q = 0.70710678f, n = 1.f / (1.f + K / q + K * K);
        hb0 = n; hb1 = -2.f * n; hb2 = n;
        ha1 = 2.f * (K * K - 1.f) * n; ha2 = (1.f - K / q + K * K) * n;
        sweepT = -1.f;
    }
    // once per sample, before the voices
    void tick(const Params& P, float flutterDepth) {
        if (P.sweepTime != sweepT) { sweepT = P.sweepTime; sweepK = std::exp(-5.f / (std::fmax(sweepT, 0.001f) * sr)); }
        if (P.tuneHz != tuneHz) { tuneHz = P.tuneHz; tuneOct = std::log2(std::fmax(tuneHz, 0.001f)); }
        flutter.process(flutterDepth, P.tight, sr);
    }

    void restart(Voice& v, const Params& P) {
        if (v.started) { v.gph = v.ph; v.gdt = v.dt; v.xf = xfLen; }   // fade the running oscillator out over 2 ms
        v.ph = wrap01(P.phaseDeg / 360.f);
        v.sweep = 1.f;
        v.started = true;
    }
    void segment(Voice& v, int stage, float to) { v.stage = stage; v.from = v.level; v.to = to; v.p = 0.f; }
    float stageTime(const Params& P, int stage) {
        float t = stage == 1 ? P.att : (stage == 2 ? P.dec : P.rel);
        return std::fmax(t, 0.0002f);
    }

    VoiceOut process(Voice& v, const VoiceIn& in, const Params& P) {
        VoiceOut o;
        bool gateRise = in.gatePatched && in.gate && !v.gateWas;
        bool gateFall = in.gatePatched && !in.gate && v.gateWas;
        v.gateWas = in.gatePatched && in.gate;
        bool holding = in.gatePatched && in.gate;
        bool loopOn = P.mode == LOOP && (!in.gatePatched || in.gate);

        // ---- events
        if (in.trig || gateRise || (loopOn && v.stage == 0)) {
            restart(v, P);
            segment(v, 1, 1.f);
        } else if (gateFall && (v.stage == 1 || v.stage == 2 || v.stage == 3)) {
            segment(v, 4, 0.f);
        }

        // ---- envelope
        if (v.stage == 3) {
            v.level = P.sus;
            if (!holding) segment(v, 4, 0.f);
        } else if (v.stage != 0) {
            v.p += 1.f / (stageTime(P, v.stage) * sr);
            float to = v.stage == 2 ? P.sus : v.to;
            if (v.p >= 1.f) {
                v.level = to;
                if (v.stage == 1) segment(v, 2, P.sus);
                else if (v.stage == 2) {
                    if (P.mode != LOOP && holding) v.stage = 3;
                    else segment(v, 4, 0.f);
                } else {   // release done
                    v.stage = 0;
                    v.level = 0.f;
                    if (P.mode != FREE) o.eoc = true;
                    if (loopOn) { restart(v, P); segment(v, 1, 1.f); }
                }
            } else {
                v.level = v.from + (to - v.from) * curveShape(v.p, P.curve);
            }
        }

        // ---- oscillator
        v.sweep *= sweepK;
        float oct = tuneOct + in.pitchOct + (P.sweepSt * v.sweep + flutter.pitchSt) / 12.f;
        float hz = clampf(std::exp2(oct), 0.001f, 0.45f * sr);
        v.dt = hz / sr;
        float osc = morphWave(P.wave, v.ph, v.dt);
        if (v.xf > 0) {
            float x = 1.f - (float) v.xf / (float) xfLen;
            float g = morphWave(P.wave, v.gph, v.gdt);
            osc = g * (1.f - x) + osc * x;
            v.gph = wrap01(v.gph + v.gdt);
            v.xf--;
        }
        v.ph += v.dt;
        if (v.ph >= 1.f) { v.ph -= std::floor(v.ph); if (P.mode == FREE) o.eoc = true; }
        v.started = true;
        o.hz = hz;
        o.lfo = P.lfoUni ? (osc + 1.f) * 5.f : osc * 5.f;
        o.env = v.level * 10.f;

        // ---- audio: filter -> VCA (env, flutter AM, VCA CV) -> LEVEL / drive -> DC block -> 30 Hz -> +-5 V cap
        float amp = (P.mode == FREE ? 1.f : v.level) * flutter.am * clampf(in.vca, 0.f, 1.f);
        if (amp <= 0.f && v.stage == 0 && std::fabs(v.dcy) < 1e-6f && std::fabs(v.hz1) + std::fabs(v.hz2) < 1e-6f) {
            // idle and silent: skip the filter (it restarts from rest; the attack covers its first samples)
            v.ic1 = v.ic2 = v.dcx = v.dcy = v.hz1 = v.hz2 = 0.f;
            o.audio = 0.f;
            return o;
        }
        float fc = clampf(P.cutoffHz * std::exp2(in.cutoffOct), 10.f, 0.45f * sr);
        if (std::fabs(fc - v.fcLast) > v.fcLast * 0.002f || P.res != v.resLast) {
            v.fcLast = fc; v.resLast = P.res;
            float g = std::tan(kPi * fc / sr);
            v.k = 2.f - 1.96f * clampf(P.res, 0.f, 1.f);
            v.a1 = 1.f / (1.f + g * (g + v.k));
            v.a2 = g * v.a1;
            v.a3 = g * v.a2;
        }
        float v3 = osc - v.ic2;
        float v1 = v.a1 * v.ic1 + v.a2 * v3;
        float v2 = v.ic2 + v.a2 * v.ic1 + v.a3 * v3;
        v.ic1 = softLim(2.f * v1 - v.ic1, 2.f, 4.f);   // the resonance saturates instead of running away
        v.ic2 = 2.f * v2 - v.ic2;
        float m = clampf(P.morph, 0.f, 1.f);
        float y = v2 * (1.f - m) + 2.f * v1 * m;
        y *= amp;
        float L = std::fmax(P.level, 0.f);
        if (L <= 1.f) y *= L;
        else {
            float a = L - 1.f, pre = 1.f + 2.f * a;
            float ys = satur(y * pre) / satur(pre);
            float b = clampf(a * 4.f, 0.f, 1.f);
            y = y * (1.f - b) + ys * b;
        }
        float vo = y * 5.f;
        float dc = vo - v.dcx + dcR * v.dcy;
        v.dcx = vo; v.dcy = dc;
        vo = dc;
        if (P.rolloff) {   // transposed direct form II
            float out = hb0 * vo + v.hz1;
            v.hz1 = hb1 * vo - ha1 * out + v.hz2;
            v.hz2 = hb2 * vo - ha2 * out;
            vo = out;
        } else { v.hz1 = v.hz2 = 0.f; }
        o.audio = softLim(vo, 4.f, 5.f);
        if (!std::isfinite(o.audio)) { v.ic1 = v.ic2 = v.dcx = v.dcy = v.hz1 = v.hz2 = 0.f; o.audio = 0.f; }
        return o;
    }
};

}  // namespace pulse
