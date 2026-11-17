// PULSE (Nov 16 2026; free single SynTHBloK-Pulse, was the paid MTX-PULSE; spec: liquidnick_site_work sessions/2026-11-16_bass_music_research.md, "The
// triggerable LFO / VCA voice", Copilot spec + Gemini panel). A triggerable LFO / VCA voice, 14 HP: one oscillator from
// 0.01 Hz to audio (TUNE exp, V/OCT, RATE CV), WAVE morph sine - tri - saw - square, PHASE start (90 deg = punchy sub
// kick), modes FREE / LOOP / 1-SHOT on lit keys, TRIG (hard reset, 2 ms crossfade) + GATE, P.SWEEP +-48 st with an exp
// curve, ADSR 0.5 ms .. 30 s (EXP / LIN / LOG), 2-pole LP / BP morph filter, LEVEL (clean VCA up to 1, drive above),
// FLUTTER + TIGHTNESS. Poly 1-16 on TRIG / GATE / V/OCT. Outs: AUDIO (DC-blocked, 30 Hz roll-off option, soft cap
// +-5 V), LFO (bi / uni), ENV 0-10 V, EOC.
// RANDROYD roles (planned): sub-kick / 808 boom, flutter bass or counter, slow pad / FX modulator (EOC cross-patched).
// Param names are plain words for RANDROYD's setP ("Tune", "Pitch sweep", "Decay", "Flutter", ...).
// Engine: src/Pulse_dsp.hpp (offline test tools/pulse_test.sh).
#include "plugin.hpp"
#include "OledUi.hpp"
#include "Pulse_dsp.hpp"

namespace {

static const char* kModeNames[3] = {"FREE", "LOOP", "1-SHOT"};

struct Pulse : Module {
    enum ParamId {
        MODE_PARAM, TRIG_PARAM, TUNE_PARAM, WAVE_PARAM, PHASE_PARAM, LEVEL_PARAM, SWEEP_PARAM, SWEEPTIME_PARAM,
        FLUTTER_PARAM, TIGHT_PARAM, CUTOFF_PARAM, RES_PARAM, MORPH_PARAM, ATTACK_PARAM, DECAY_PARAM, SUSTAIN_PARAM,
        RELEASE_PARAM, CURVE_PARAM, LFOUNI_PARAM, ROLLOFF_PARAM, NUM_PARAMS
    };
    enum InputId { TRIG_INPUT, GATE_INPUT, VOCT_INPUT, RATE_INPUT, FLUTTER_INPUT, CUTOFF_INPUT, VCA_INPUT, NUM_INPUTS };
    enum OutputId { AUDIO_OUTPUT, LFO_OUTPUT, ENV_OUTPUT, EOC_OUTPUT, NUM_OUTPUTS };

    pulse::Engine eng;
    pulse::Params P;
    pulse::Voice voices[16];
    dsp::SchmittTrigger trigST[16], gateST[16];
    dsp::BooleanTrigger keyTrig;
    dsp::PulseGenerator eocPulse[16];
    dsp::ClockDivider paramDiv;
    float lastSr = 0.f;
    int channels = 1;
    int preset = -1;
    float trigFlash = 0.f;     // the TRIG key lights for 60 ms on every trigger (display only)
    float shownHz = 38.f;      // voice 1, for the display

    Pulse() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configSwitch(MODE_PARAM, 0.f, 2.f, 2.f, "Mode", {"FREE (free-running LFO, VCA open)", "LOOP (the envelope cycles by itself)", "1-SHOT (TRIG = A-D-R, GATE = ADSR)"});
        configButton(TRIG_PARAM, "Trigger (all voices)");
        configParam(TUNE_PARAM, std::log2(0.01f), std::log2(8000.f), std::log2(38.f), "Tune", " Hz", 2.f);
        configParam(WAVE_PARAM, 0.f, 3.f, 0.f, "Wave", "", 0.f, 1.f);
        configParam(PHASE_PARAM, 0.f, 360.f, 90.f, "Phase", " deg");
        configParam(LEVEL_PARAM, 0.f, 4.f, 1.f, "Level", "x");
        configParam(SWEEP_PARAM, -48.f, 48.f, 24.89f, "Pitch sweep", " st");
        configParam(SWEEPTIME_PARAM, std::log2(0.005f), std::log2(4.f), std::log2(0.6f), "Sweep time", " ms", 2.f, 1000.f);
        configParam(FLUTTER_PARAM, 0.f, 1.f, 0.f, "Flutter", "%", 0.f, 100.f);
        configParam(TIGHT_PARAM, 0.f, 1.f, 0.5f, "Tightness", "%", 0.f, 100.f);
        configParam(CUTOFF_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(20000.f), "Cutoff", " Hz", 2.f);
        configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
        configParam(MORPH_PARAM, 0.f, 1.f, 0.f, "LP-BP morph", "%", 0.f, 100.f);
        const float tLo = std::log2(0.0005f), tHi = std::log2(30.f);
        configParam(ATTACK_PARAM, tLo, tHi, std::log2(0.0005f), "Attack", " ms", 2.f, 1000.f);
        configParam(DECAY_PARAM, tLo, tHi, std::log2(0.9f), "Decay", " ms", 2.f, 1000.f);
        configParam(SUSTAIN_PARAM, 0.f, 1.f, 0.f, "Sustain", "%", 0.f, 100.f);
        configParam(RELEASE_PARAM, tLo, tHi, std::log2(0.25f), "Release", " ms", 2.f, 1000.f);
        configSwitch(CURVE_PARAM, 0.f, 2.f, 0.f, "Envelope curve", {"EXP", "LIN", "LOG"});
        configSwitch(LFOUNI_PARAM, 0.f, 1.f, 0.f, "LFO polarity", {"Bipolar +-5 V", "Unipolar 0-10 V"});
        configSwitch(ROLLOFF_PARAM, 0.f, 1.f, 0.f, "30 Hz roll-off", {"Off", "On"});
        paramQuantities[WAVE_PARAM]->description = "0 sine, 1 triangle, 2 saw, 3 square (morphs between neighbours)";
        paramQuantities[PHASE_PARAM]->description = "Where a trigger restarts the wave: 90 deg = punchy sub kick, 0 = clean";
        paramQuantities[LEVEL_PARAM]->description = "The VCA: clean up to 1, saturation above (the output stays under +-5 V)";
        paramQuantities[SWEEP_PARAM]->description = "Pitch envelope on every restart, exp curve (kick: +24.9 st = 160 -> 38 Hz)";
        paramQuantities[SWEEPTIME_PARAM]->description = "How long the pitch sweep takes to settle";
        paramQuantities[FLUTTER_PARAM]->description = "Tape wow / flutter depth (pitch + a little AM)";
        paramQuantities[TIGHT_PARAM]->description = "Loose = slow wandering wobble, tight = fast shallow buzz (0.15 - 12 Hz)";
        configInput(TRIG_INPUT, "Trigger (restarts the wave + sweep + envelope)");
        configInput(GATE_INPUT, "Gate (ADSR; in LOOP: loops while high)");
        configInput(VOCT_INPUT, "V/Oct");
        configInput(RATE_INPUT, "Rate CV (1 V/oct)");
        configInput(FLUTTER_INPUT, "Flutter CV (10 V = full depth)");
        configInput(CUTOFF_INPUT, "Cutoff CV (1 V/oct)");
        configInput(VCA_INPUT, "VCA CV (0-10 V)");
        configOutput(AUDIO_OUTPUT, "Audio");
        configOutput(LFO_OUTPUT, "LFO (the raw wave, before the filter / VCA)");
        configOutput(ENV_OUTPUT, "Envelope (0-10 V)");
        configOutput(EOC_OUTPUT, "End of cycle (trigger)");
        paramDiv.setDivision(8);
    }

    static float t2(float v) { return std::pow(2.f, v); }
    void readParams() {
        P.mode = (int) std::round(params[MODE_PARAM].getValue());
        P.tuneHz = t2(params[TUNE_PARAM].getValue());
        P.wave = params[WAVE_PARAM].getValue();
        P.phaseDeg = params[PHASE_PARAM].getValue();
        P.level = params[LEVEL_PARAM].getValue();
        P.sweepSt = params[SWEEP_PARAM].getValue();
        P.sweepTime = t2(params[SWEEPTIME_PARAM].getValue());
        P.flutter = params[FLUTTER_PARAM].getValue();
        P.tight = params[TIGHT_PARAM].getValue();
        P.cutoffHz = t2(params[CUTOFF_PARAM].getValue());
        P.res = params[RES_PARAM].getValue();
        P.morph = params[MORPH_PARAM].getValue();
        P.att = t2(params[ATTACK_PARAM].getValue());
        P.dec = t2(params[DECAY_PARAM].getValue());
        P.sus = params[SUSTAIN_PARAM].getValue();
        P.rel = t2(params[RELEASE_PARAM].getValue());
        P.curve = (int) std::round(params[CURVE_PARAM].getValue());
        P.lfoUni = params[LFOUNI_PARAM].getValue() > 0.5f;
        P.rolloff = params[ROLLOFF_PARAM].getValue() > 0.5f;
    }

    void applyPreset(int i) {
        if (i < 0 || i >= pulse::kNumPresets) return;
        pulse::Params p = pulse::preset(i);
        params[MODE_PARAM].setValue((float) p.mode);
        params[TUNE_PARAM].setValue(std::log2(p.tuneHz));
        params[WAVE_PARAM].setValue(p.wave);
        params[PHASE_PARAM].setValue(p.phaseDeg);
        params[LEVEL_PARAM].setValue(p.level);
        params[SWEEP_PARAM].setValue(p.sweepSt);
        params[SWEEPTIME_PARAM].setValue(std::log2(p.sweepTime));
        params[FLUTTER_PARAM].setValue(p.flutter);
        params[TIGHT_PARAM].setValue(p.tight);
        params[CUTOFF_PARAM].setValue(std::log2(p.cutoffHz));
        params[RES_PARAM].setValue(p.res);
        params[MORPH_PARAM].setValue(p.morph);
        params[ATTACK_PARAM].setValue(std::log2(p.att));
        params[DECAY_PARAM].setValue(std::log2(p.dec));
        params[SUSTAIN_PARAM].setValue(p.sus);
        params[RELEASE_PARAM].setValue(std::log2(p.rel));
        params[CURVE_PARAM].setValue((float) p.curve);
        params[ROLLOFF_PARAM].setValue(p.rolloff ? 1.f : 0.f);
        preset = i;
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate != lastSr) { eng.init(args.sampleRate); lastSr = args.sampleRate; readParams(); }
        if (paramDiv.process()) readParams();
        int C = std::max(1, std::max(inputs[TRIG_INPUT].getChannels(), std::max(inputs[GATE_INPUT].getChannels(), inputs[VOCT_INPUT].getChannels())));
        C = std::min(C, 16);
        channels = C;
        bool key = keyTrig.process(params[TRIG_PARAM].getValue() > 0.5f);
        float depth = P.flutter;
        if (inputs[FLUTTER_INPUT].isConnected()) depth += inputs[FLUTTER_INPUT].getVoltage() * 0.1f;
        eng.tick(P, depth);
        bool gatePatched = inputs[GATE_INPUT].isConnected();
        bool anyTrig = key;
        for (int c = 0; c < C; c++) {
            pulse::VoiceIn in;
            in.trig = key || trigST[c].process(inputs[TRIG_INPUT].getPolyVoltage(c), 0.1f, 1.f);
            in.gatePatched = gatePatched;
            if (gatePatched) { gateST[c].process(inputs[GATE_INPUT].getPolyVoltage(c), 0.1f, 1.f); in.gate = gateST[c].isHigh(); }
            in.pitchOct = inputs[VOCT_INPUT].getPolyVoltage(c) + inputs[RATE_INPUT].getPolyVoltage(c);
            in.cutoffOct = inputs[CUTOFF_INPUT].getPolyVoltage(c);
            in.vca = inputs[VCA_INPUT].isConnected() ? inputs[VCA_INPUT].getPolyVoltage(c) * 0.1f : 1.f;
            if (in.trig) anyTrig = true;
            pulse::VoiceOut o = eng.process(voices[c], in, P);
            if (o.eoc) eocPulse[c].trigger(1e-3f);
            outputs[AUDIO_OUTPUT].setVoltage(o.audio, c);
            outputs[LFO_OUTPUT].setVoltage(o.lfo, c);
            outputs[ENV_OUTPUT].setVoltage(o.env, c);
            outputs[EOC_OUTPUT].setVoltage(eocPulse[c].process(args.sampleTime) ? 10.f : 0.f, c);
            if (c == 0) shownHz = o.hz;
        }
        for (int o = 0; o < NUM_OUTPUTS; o++) outputs[o].setChannels(C);
        if (anyTrig) trigFlash = 0.06f;
        else if (trigFlash > 0.f) trigFlash -= args.sampleTime;
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        preset = -1;
        for (int c = 0; c < 16; c++) voices[c] = pulse::Voice();
    }

    json_t* dataToJson() override {
        json_t* r = json_object();
        json_object_set_new(r, "preset", json_integer(preset));
        return r;
    }
    // "preset": n also APPLIES that preset when the patch data asks for it ("apply": true; for RANDROYD, like GRIT)
    void dataFromJson(json_t* r) override {
        if (json_t* j = json_object_get(r, "preset")) preset = (int) json_integer_value(j);
        if (json_t* a = json_object_get(r, "apply")) if (json_is_true(a)) applyPreset(preset);
    }
};

inline void gText(NVGcontext* vg, float x, float y, float size, NVGcolor c, int align, const std::string& t, float spacing = 0.f) {
    std::shared_ptr<window::Font> font = APP->window->uiFont;
    if (!font) return;
    nvgFontFaceId(vg, font->handle);
    nvgFontSize(vg, size);
    nvgTextLetterSpacing(vg, spacing);
    nvgTextAlign(vg, align);
    nvgFillColor(vg, c);
    nvgText(vg, x, y, t.c_str(), NULL);
}

static std::string hzText(float hz) {
    if (hz < 1.f) return string::f("%.3f Hz", hz);
    if (hz < 20.f) return string::f("%.2f Hz", hz);
    static const char* nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int midi = (int) std::round(69.f + 12.f * std::log2(hz / 440.f));
    int n = ((midi % 12) + 12) % 12, oct = midi / 12 - 1;
    return hz < 1000.f ? string::f("%.1f Hz %s%d", hz, nn[n], oct) : string::f("%.2fk %s%d", hz / 1000.f, nn[n], oct);
}

// the OLED: mode (big) + the sweep / flutter line, Hz + note of voice 1 (right), one cycle of the wave (left, a dot riding
// voice 1's oscillator; idle: where a restart starts it) and the envelope shape (right, a dot riding it; segment widths follow the log of their times)
struct PulseDisplay : TransparentWidget {
    Pulse* m = NULL;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        NVGcontext* vg = args.vg;
        float W = box.size.x, H = box.size.y;
        oled::box(args, W, H);
        pulse::Params P = m ? m->P : pulse::Params();
        float hz = m ? m->shownHz : 38.f;
        gText(vg, 4.f, 8.5f, 10.f, oled::amber(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, kModeNames[clamp(P.mode, 0, 2)]);
        gText(vg, W - 4.f, 8.5f, 8.f, oled::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, hzText(hz));
        std::string sub = string::f("SWEEP %+.1f st", P.sweepSt);
        if (P.flutter > 0.f) sub += string::f("  FLUTTER %d%%", (int) std::round(P.flutter * 100.f));
        if (m && m->channels > 1) sub += string::f("  %d VOICES", m->channels);
        if (m && m->preset >= 0) sub = std::string(pulse::presetName(m->preset)) + "  " + sub;
        gText(vg, 4.f, 17.5f, 5.5f, oled::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, sub);
        nvgBeginPath(vg);
        oled::divider(args, 3.f, 22.f, W - 3.f, 22.f);
        oled::divider(args, W * 0.42f, 25.f, W * 0.42f, H - 3.f);
        oled::dividerStroke(args);
        // wave
        float x0 = 5.f, x1 = W * 0.42f - 4.f, top = 26.f, bot = H - 4.f, mid = (top + bot) * 0.5f, amp = (bot - top) * 0.45f;
        nvgBeginPath(vg);
        for (int i = 0; i <= 64; i++) {
            float p = i / 64.f, y = mid - amp * pulse::morphWave(P.wave, p, 0.f);
            float x = x0 + (x1 - x0) * p;
            if (i == 0) nvgMoveTo(vg, x, y); else nvgLineTo(vg, x, y);
        }
        nvgStrokeColor(vg, oled::amber()); nvgStrokeWidth(vg, 1.1f); nvgStroke(vg);
        // the dot rides the running oscillator (voice 1), like the envelope's dot (Nov 16 2026, Nick: "little ball doesn't
        // move on the wave readout, the ADSR one does"); idle (LOOP / 1-SHOT between hits) it rests where a restart starts
        float ps = pulse::wrap01(P.phaseDeg / 360.f);
        if (m && (P.mode == pulse::FREE || m->voices[0].stage != 0)) ps = pulse::wrap01(m->voices[0].ph);
        nvgBeginPath(vg);
        nvgCircle(vg, x0 + (x1 - x0) * ps, mid - amp * pulse::morphWave(P.wave, ps, 0.f), 1.8f);
        nvgFillColor(vg, oled::litFill()); nvgFill(vg);
        // envelope
        float e0 = W * 0.42f + 5.f, e1 = W - 5.f;
        float wa = std::log(1.f + P.att * 1000.f), wd = std::log(1.f + P.dec * 1000.f), wr = std::log(1.f + P.rel * 1000.f);
        bool hold = P.mode == pulse::ONESHOT;
        float ws = hold ? (wa + wd + wr) * 0.3f : 0.f, tot = std::max(wa + wd + ws + wr, 1e-3f);
        float sx = (e1 - e0) / tot, eTop = 26.f, eBot = H - 4.f, eh = eBot - eTop;
        float xa = e0 + wa * sx, xd = xa + wd * sx, xs = xd + ws * sx;
        nvgBeginPath(vg);
        const int N = 24;
        nvgMoveTo(vg, e0, eBot);
        for (int i = 1; i <= N; i++) nvgLineTo(vg, e0 + (xa - e0) * i / N, eBot - eh * pulse::curveShape(i / (float) N, P.curve));
        for (int i = 1; i <= N; i++) nvgLineTo(vg, xa + (xd - xa) * i / N, eBot - eh * (1.f + (P.sus - 1.f) * pulse::curveShape(i / (float) N, P.curve)));
        nvgLineTo(vg, xs, eBot - eh * P.sus);
        for (int i = 1; i <= N; i++) nvgLineTo(vg, xs + (e1 - xs) * i / N, eBot - eh * P.sus * (1.f - pulse::curveShape(i / (float) N, P.curve)));
        nvgStrokeColor(vg, P.mode == pulse::FREE ? oled::dim() : oled::amber()); nvgStrokeWidth(vg, 1.1f); nvgStroke(vg);
        if (m) {
            const pulse::Voice& v = m->voices[0];
            float px = -1.f;
            if (v.stage == 1) px = e0 + (xa - e0) * clamp(v.p, 0.f, 1.f);
            else if (v.stage == 2) px = xa + (xd - xa) * clamp(v.p, 0.f, 1.f);
            else if (v.stage == 3) px = (xd + xs) * 0.5f;
            else if (v.stage == 4) px = xs + (e1 - xs) * clamp(v.p, 0.f, 1.f);
            if (px >= 0.f) {
                nvgBeginPath(vg);
                nvgCircle(vg, px, eBot - eh * clamp(v.level, 0.f, 1.f), 1.8f);
                nvgFillColor(vg, oled::litFill()); nvgFill(vg);
            }
        }
        if (P.mode == pulse::LOOP) gText(vg, e1, eTop + 2.f, 5.f, oled::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, "LOOP");
    }
};

static const float PW = 71.12f;   // 14 HP
static const float KX[4] = {9.5f, 26.87f, 44.25f, 61.62f};                 // knob columns
static const float JX[6] = {7.f, 18.42f, 29.85f, 41.27f, 52.7f, 64.12f};   // jack columns
static const float KY[4] = {44.5f, 57.f, 69.5f, 82.f};                     // knob rows
static const float JY[2] = {103.3f, 116.f};
static const char* KN[4][4] = {{"TUNE", "WAVE", "PHASE", "DRIVE/VCA"},
                               {"P.SWEEP", "S.TIME", "FLUTTER", "TIGHT"},
                               {"CUTOFF", "RES", "LP>BP", ""},
                               {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"}};

// the value ring round each knob, the same as MTX-DRUM's (DRRing): a dim track and an orange arc to the value
// (Nov 16 2026, Nick: "make sure all the new ones look the same as older ones")
struct PulseRing : TransparentWidget {
    Module* module = NULL;
    int param = 0;
    float width = 1.8f;
    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        float v = module ? module->paramQuantities[param]->getScaledValue() : 0.5f;
        float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f, rr = box.size.x * 0.5f - width * 0.8f;
        float a0 = M_PI * 0.75f, a1 = M_PI * 2.25f;
        nvgBeginPath(args.vg);
        nvgArc(args.vg, cx, cy, rr, a0, a1, NVG_CW);
        nvgStrokeColor(args.vg, nvgRGBA(255, 180, 90, 40));
        nvgStrokeWidth(args.vg, width);
        nvgStroke(args.vg);
        if (v > 0.001f) {
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, rr, a0, a0 + (a1 - a0) * v, NVG_CW);
            nvgStrokeColor(args.vg, nvgRGBA(255, 112, 15, 235));
            nvgStrokeWidth(args.vg, width);
            nvgStroke(args.vg);
        }
    }
};

struct PulseWidget : ModuleWidget {
    oled::Cell* cell(Pulse* module, int param, float x, float y, float w, const char* label, int setTo, bool latch) {
        oled::Cell* c = createParam<oled::Cell>(mm2px(Vec(x, y)), module, param);
        c->box.size = mm2px(Vec(w, oled::kCellH));
        c->module = module; c->label = label; c->setTo = setTo; c->latch = latch; c->fontSize = 6.f;
        addParam(c);
        return c;
    }
    PulseWidget(Pulse* module) {
        setModule(module);
        box.size = Vec(mm2px(PW), RACK_GRID_HEIGHT);
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(PW - 3.5f, 3.5f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(3.5f, 125.f))));
        addChild(createWidgetCentered<SocketScrew>(mm2px(Vec(PW - 3.5f, 125.f))));
        PulseDisplay* d = new PulseDisplay;
        d->m = module;
        d->box.pos = mm2px(Vec(5.f, 10.5f));
        d->box.size = mm2px(Vec(PW - 10.f, 18.f));
        addChild(d);
        // mode keys + TRIG
        float cw = (PW - 10.f - 3.f) / 4.f;
        for (int i = 0; i < 3; i++) cell(module, Pulse::MODE_PARAM, 5.f + i * (cw + 1.f), 30.3f, cw, kModeNames[i], i, false);
        oled::Cell* t = cell(module, Pulse::TRIG_PARAM, 5.f + 3 * (cw + 1.f), 30.3f, cw, "TRIG", -1, false);
        t->jack = Pulse::TRIG_INPUT;
        if (module) t->flash = &module->trigFlash;
        // knobs
        const int K[4][4] = {{Pulse::TUNE_PARAM, Pulse::WAVE_PARAM, Pulse::PHASE_PARAM, Pulse::LEVEL_PARAM},
                             {Pulse::SWEEP_PARAM, Pulse::SWEEPTIME_PARAM, Pulse::FLUTTER_PARAM, Pulse::TIGHT_PARAM},
                             {Pulse::CUTOFF_PARAM, Pulse::RES_PARAM, Pulse::MORPH_PARAM, -1},
                             {Pulse::ATTACK_PARAM, Pulse::DECAY_PARAM, Pulse::SUSTAIN_PARAM, Pulse::RELEASE_PARAM}};
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) {
                if (K[r][c] < 0) continue;
                float d = (r == 0 && c == 0) ? 12.f : 10.f;   // ring box: a bit wider than the knob (MTX-DRUM: 11 mm round a small knob)
                PulseRing* ring = createWidget<PulseRing>(mm2px(Vec(KX[c] - d * 0.5f, KY[r] - d * 0.5f)));
                ring->box.size = mm2px(Vec(d, d)); ring->module = module; ring->param = K[r][c];
                addChild(ring);
                if (r == 0 && c == 0) addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(KX[c], KY[r])), module, K[r][c]));
                else addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(KX[c], KY[r])), module, K[r][c]));
            }
        // the free slot: LFO polarity + 30 Hz roll-off keys
        float sw = 14.f, sx = KX[3] - sw * 0.5f;
        cell(module, Pulse::LFOUNI_PARAM, sx, KY[2] - 5.3f, sw, "LFO UNI", -1, true);
        cell(module, Pulse::ROLLOFF_PARAM, sx, KY[2] + 0.4f, sw, "30 HZ HP", -1, true);
        // envelope curve keys under the ADSR
        float ew = (PW - 10.f - 2.f) / 3.f;
        const char* cn[3] = {"EXP", "LIN", "LOG"};
        for (int i = 0; i < 3; i++) cell(module, Pulse::CURVE_PARAM, 5.f + i * (ew + 1.f), 88.6f, ew, cn[i], i, false);
        // jacks
        const int J1[6] = {Pulse::TRIG_INPUT, Pulse::GATE_INPUT, Pulse::VOCT_INPUT, Pulse::RATE_INPUT, Pulse::FLUTTER_INPUT, Pulse::CUTOFF_INPUT};
        for (int i = 0; i < 6; i++) addInput(createInputCentered<SmallJack>(mm2px(Vec(JX[i], JY[0])), module, J1[i]));
        addInput(createInputCentered<SmallJack>(mm2px(Vec(JX[0], JY[1])), module, Pulse::VCA_INPUT));
        const int J2[4] = {Pulse::AUDIO_OUTPUT, Pulse::LFO_OUTPUT, Pulse::ENV_OUTPUT, Pulse::EOC_OUTPUT};
        for (int i = 0; i < 4; i++) addOutput(createOutputCentered<SmallJack>(mm2px(Vec(JX[i + 2], JY[1])), module, J2[i]));
    }
    void appendContextMenu(Menu* menu) override {
        Pulse* m = dynamic_cast<Pulse*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("Presets", pulse::presetName(m->preset), [=](Menu* sub) {
            for (int i = 0; i < pulse::kNumPresets; i++)
                sub->addChild(createCheckMenuItem(pulse::presetName(i), "", [=]() { return m->preset == i; }, [=]() { m->applyPreset(i); }));
        }));
    }
    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        float w = box.size.x, h = box.size.y;
        nvgBeginPath(vg); nvgRect(vg, 0, 0, w, h); nvgFillColor(vg, nvgRGB(0x11, 0x11, 0x11)); nvgFill(vg);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, h, nvgRGBA(255, 255, 255, 9), nvgRGBA(0, 0, 0, 41))); nvgFill(vg);
        nvgBeginPath(vg); nvgRect(vg, 0, 0, w, mm2px(7.f)); nvgFillColor(vg, nvgRGB(8, 8, 8)); nvgFill(vg);
        nvgBeginPath(vg); nvgRect(vg, 0, mm2px(7.f) - 0.45f, w, 0.9f); nvgFillColor(vg, nvgRGBA(255, 122, 0, 190)); nvgFill(vg);
        float ci = mm2px(2.5f);
        nvgBeginPath(vg); nvgRoundedRect(vg, ci, mm2px(9.f), w - 2.f * ci, mm2px(113.f), mm2px(1.2f));
        nvgFillColor(vg, nvgRGB(0x16, 0x16, 0x16)); nvgFill(vg);
        nvgStrokeColor(vg, nvgRGB(0x2c, 0x2c, 0x2c)); nvgStrokeWidth(vg, mm2px(0.25f)); nvgStroke(vg);
        // the outputs' plate
        nvgBeginPath(vg);
        nvgRoundedRect(vg, mm2px(JX[2] - 5.2f), mm2px(JY[1] - 7.8f), mm2px(JX[5] - JX[2] + 10.4f), mm2px(12.f), mm2px(1.f));
        nvgFillColor(vg, nvgRGB(0x0b, 0x0b, 0x0b)); nvgFill(vg);
        const NVGcolor lab = nvgRGB(0xbb, 0xbb, 0xbb), dim = nvgRGB(0x80, 0x80, 0x80), org = nvgRGB(0xff, 0x8a, 0x1e);
        oled::mtxTitle(vg, "PULSE");
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) {
                if (!KN[r][c][0]) continue;
                float ly = KY[r] - (r == 0 && c == 0 ? 7.6f : 6.6f);
                gText(vg, mm2px(KX[c]), mm2px(ly), 7.f, r == 0 && c == 0 ? org : lab, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, KN[r][c], 0.6f);
            }
        nvgBeginPath(vg); nvgRect(vg, mm2px(4.f), mm2px(94.6f), w - mm2px(8.f), 0.6f); nvgFillColor(vg, nvgRGB(0x2c, 0x2c, 0x2c)); nvgFill(vg);
        const char* J1[6] = {"TRIG", "GATE", "V/OCT", "RATE", "FLUT", "CUT"};
        for (int i = 0; i < 6; i++) gText(vg, mm2px(JX[i]), mm2px(JY[0] - 5.4f), 5.5f, lab, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, J1[i]);
        gText(vg, mm2px(JX[0]), mm2px(JY[1] - 5.4f), 5.5f, lab, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, "VCA");
        const char* J2[4] = {"AUDIO", "LFO", "ENV", "EOC"};
        for (int i = 0; i < 4; i++) gText(vg, mm2px(JX[i + 2]), mm2px(JY[1] - 5.4f), 5.5f, org, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, J2[i]);
        gText(vg, w * 0.5f, mm2px(127.9f), mm2px(1.944f) * 1.4f, dim, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, "NoiZee ToyZ");
        ModuleWidget::draw(args);
    }
};

}  // namespace

Model* modelSynTHBloKPulse = createModel<Pulse, PulseWidget>("Pulse");
