// engine/modules/voice/polyv.h - POLY VOICE: your guitar's chords played by tape keyboards or a polysynth.
// No per-string pickup: core/polytrack.h finds up to six notes in the guitar every ~5 ms and gives each
// its own voice (eight voices). Voices follow the guitar's level (peaks held 25 ms, square-rooted so the
// keyboard plays evenly), shared between the notes as the spectrum says, through ATTACK and RELEASE; a
// new pick restarts a voice still sounding that note. Inspired by pedals that do this (EHX's MEL9 and
// SYNTH9); nothing of theirs is used: tracking, voices and tables are this file's and polytrack.h's own.
//  MODEL   TAPE STRINGS, TAPE FLUTE, TAPE CHOIR: original additive tape voices (no samples), snapped to
//          the nearest key, with tape wow and flutter, hiss, a slow swell of their own and the tape
//          running out: a held note fades after 8 s. SYNTH SAW, SYNTH PULSE, SYNTH BRASS: band-limited
//          oscillators (two detuned saws, a pulse with slow PWM, brass saws) plus a sub octave, through a
//          resonant low-pass per voice that opens with each new note; they follow bends and slides.
//          A change of model crossfades over 20 ms.
//  EFFECT  the voices' level; DRY  your guitar's level (both 0-100%).
//  ATTACK  1 ms - 2 s, RELEASE 10 ms - 4 s: how each voice follows its note.
//  TONE    200 Hz - 12 kHz: the tape's high-cut, or the synth filter's cutoff.
//  MOD     the tape's age (wow, flutter and hiss), or how far the synth filter opens on each note.
//  OCTAVE  -1, 0 or +1 octave.
// Meters: "voices" (sounding) and "notes" (up to three MIDI notes, packed (n1+1) + 129 (n2+1) + ...).
#pragma once
#include "../../core/module.h"
#include "../../core/polytrack.h"

namespace rig {

inline constexpr ParamStop POLYV_MODELS[] = {{0, "TAPE STRINGS"}, {1, "TAPE FLUTE"}, {2, "TAPE CHOIR"}, {3, "SYNTH SAW"}, {4, "SYNTH PULSE"}, {5, "SYNTH BRASS"}};
inline constexpr ParamStop POLYV_OCTAVES[] = {{-1, "-1 OCT"}, {0, "0"}, {1, "+1 OCT"}};
inline constexpr ParamDesc POLYV_PARAMS[] = {
  {"model",   "MODEL",   0.f,   5.f,     0.f,     "",   Curve::Lin, 0, false, POLYV_MODELS, 6},
  {"effect",  "EFFECT",  0.f,   100.f,   0.8f,    "%",  Curve::Lin, 0, false},
  {"dry",     "DRY",     0.f,   100.f,   0.3f,    "%",  Curve::Lin, 0, false},
  {"attack",  "ATTACK",  1.f,   2000.f,  0.4474f, "ms", Curve::Log, 0, false},   // 30 ms
  {"release", "RELEASE", 10.f,  4000.f,  0.6157f, "ms", Curve::Log, 0, false},   // 400 ms
  {"tone",    "TONE",    200.f, 12000.f, 0.7317f, "Hz", Curve::Log, 0, false},   // 4 kHz
  {"mod",     "MOD",     0.f,   100.f,   0.35f,   "%",  Curve::Lin, 0, false},
  {"octave",  "OCTAVE", -1.f,   1.f,     0.5f,    "",   Curve::Lin, 0, false, POLYV_OCTAVES, 3},
};
enum { PV_MODEL, PV_EFFECT, PV_DRY, PV_ATTACK, PV_RELEASE, PV_TONE, PV_MOD, PV_OCTAVE };
inline constexpr ModuleDesc POLYV_DESC = {"polyv", "POLY VOICE", "chords to tape keys or polysynth", 8, POLYV_PARAMS, 2, {"voices", "notes"}};

constexpr int PV_TABLE = 2048, PV_MIPS = 9, PV_TAPES = 3, PV_VOICES = PolyAlloc::MAXV;
inline bool pvTape(int m) { return m < 3; }

struct PolyVoice : Module {
  PolyTracker trk; PolyAlloc alloc; Fft<PV_TABLE> tabFft;
  float tab[PV_TAPES][PV_MIPS][PV_TABLE + 1];            // one cycle per tape voice, band-limited per octave
  struct VS { float ph[3] = {0, 0, 0}, hz = 0, env = 0, age = 0, fenv = 0, ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1.4f; int mip = 0; } vs[PV_VOICES];
  float effS = 0.8f, dryS = 0.3f, wowPh = 0, wowRnd = 0, wowTarget = 0, flutPh = 0, pwmPh = 0, tapeLp = 0, hissLp = 0, xf = 1;
  float ef = 0, es = 0, el = 0, el2 = 0, maxLv = 1;                // input level: quick (picks), slow, and the one voices follow
  static constexpr float QUICK = 0.06f;                    // a voice whose note went within this long was a glitch
  int model = 0, prevModel = 0, counter = 0, sounding = 0, refr = 0, hold = 0; unsigned rng = 0x2468ACE1u;
  const ModuleDesc& desc() const override { return POLYV_DESC; }

  // ---------------------------------------------------------------- tape voices (control thread, init)
  static float bump(float f, float fc, float q) { const float r = f / fc - fc / f; return 1.f / (1.f + q * q * r * r); }
  static float tapeAmp(int t, int h, float f) {           // harmonic h of a note at f0 = f / h
    const float roll = 1.f / (1.f + powf(f / 6500.f, 4.f));
    if (t == 0) return roll / h * (0.35f + 1.4f * bump(f, 520.f, 2.f) + 1.1f * bump(f, 1650.f, 1.5f) + 0.7f * bump(f, 3100.f, 1.6f));   // strings: bowed body
    if (t == 1) { static constexpr float fl[6] = {0, 1.f, 0.24f, 0.10f, 0.045f, 0.03f}; return roll * (h <= 5 ? fl[h] : 0.012f / h); }   // flute: nearly a sine
    return roll / h * (0.06f + 1.f * bump(f, 700.f, 6.f) + 0.6f * bump(f, 1220.f, 8.f) + 0.35f * bump(f, 2600.f, 10.f));                // choir: an "ah"
  }
  void buildTables() {
    static_assert(PV_TABLE <= 4096, "tables are built on the stack");
    tabFft.init(PV_TABLE); float re[PV_TABLE], im[PV_TABLE]; unsigned seed = 12345u;
    for (int t = 0; t < PV_TAPES; t++) for (int m = 0; m < PV_MIPS; m++) {
      const float top = 27.5f * exp2f((float)(m + 1)), f0 = top * 0.707f;   // fundamentals up to `top` use this table
      const int hmax = (int)fminf(PV_TABLE / 2 - 1, floorf(0.45f * sr / top));
      memset(re, 0, sizeof re); memset(im, 0, sizeof im);
      for (int h = 1; h <= hmax; h++) {
        seed = seed * 1664525u + 1013904223u; const float phs = t == 1 ? 0.f : (seed >> 8) * (6.2831853f / 16777216.f);
        const float a = tapeAmp(t, h, f0 * h);
        re[h] = a * cosf(phs); im[h] = a * sinf(phs); re[PV_TABLE - h] = re[h]; im[PV_TABLE - h] = -im[h];
      }
      tabFft.run(re, im, true);
      double ss = 0; for (int i = 0; i < PV_TABLE; i++) ss += (double)re[i] * re[i];
      const float g = ss > 0 ? 0.42f / (float)sqrt(ss / PV_TABLE) : 0.f;      // every table at the same loudness
      for (int i = 0; i < PV_TABLE; i++) tab[t][m][i] = re[i] * g;
      tab[t][m][PV_TABLE] = tab[t][m][0];
    }
  }
  void init(float s) override { sr = s; trk.init(sr); buildTables(); reset(); }
  void reset() override {
    trk.reset(); alloc.reset(); for (auto& v : vs) v = VS();
    effS = map(PV_EFFECT, POLYV_PARAMS[PV_EFFECT].def) / 100.f; dryS = map(PV_DRY, POLYV_PARAMS[PV_DRY].def) / 100.f;
    tapeLp = hissLp = 0; xf = 1; counter = 0; sounding = 0; ef = es = el = el2 = 0; maxLv = 1; refr = 0; hold = 0;
  }
  float noise() { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) * (2.f / 16777216.f) - 1.f; }

  // ---------------------------------------------------------------- one voice, one sample
  float tapeOsc(int t, const VS& v, int which) const {
    const int m = v.mip;
    const float x = v.ph[which] * PV_TABLE; const int i = (int)x; const float f = x - i;
    const float* T = tab[t][m]; return T[i] + (T[i + 1] - T[i]) * f;
  }
  static float blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
  }
  static float saw(float ph, float dt) { return 2.f * ph - 1.f - blep(ph, dt); }
  static float pulse(float ph, float dt, float pw) { float q = ph - pw; if (q < 0) q += 1.f; return (ph < pw ? 1.f : -1.f) + blep(ph, dt) - blep(q, dt); }
  // each model's three oscillators, as ratios of the note (tape: two tapes a few cents apart; synth: a
  // detuned pair and a sub octave)
  static constexpr float RATIO[6][3] = {{1.f, 1.0029f, 0.9971f}, {1.f, 1.0029f, 0.9971f}, {1.f, 1.0029f, 0.9971f},
                                         {1.0035f, 0.9965f, 0.5f}, {1.f, 1.f, 0.5f}, {1.002f, 0.998f, 1.f}};
  static constexpr float LEVEL[6] = {1.26f, 1.f, 1.32f, 0.82f, 0.48f, 1.f};   // every model about as loud (test 165)
  float voiceSample(int mdl, VS& v, float dt, float pw) const {
    const float* r = RATIO[mdl];
    switch (mdl) {
      case 0: return 0.6f * (tapeOsc(0, v, 0) + tapeOsc(0, v, 1));
      case 1: return tapeOsc(1, v, 0);
      case 2: return 0.45f * (tapeOsc(2, v, 0) + tapeOsc(2, v, 1) + tapeOsc(2, v, 2));
      case 3: return 0.5f * (saw(v.ph[0], dt * r[0]) + saw(v.ph[1], dt * r[1])) + 0.35f * pulse(v.ph[2], dt * r[2], 0.5f);
      case 4: return 0.8f * pulse(v.ph[0], dt, pw) + 0.35f * pulse(v.ph[2], dt * r[2], 0.5f);
      default: return 0.5f * (saw(v.ph[0], dt * r[0]) + saw(v.ph[1], dt * r[1]));
    }
  }

  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    const int mdl = (int)lrintf(clampf(map(PV_MODEL, e[PV_MODEL]), 0.f, 5.f));
    if (mdl != model && xf >= 1.f) { prevModel = model; model = mdl; xf = 0.f; }
    const float att = map(PV_ATTACK, e[PV_ATTACK]) / 1000.f, rel = map(PV_RELEASE, e[PV_RELEASE]) / 1000.f;
    const float cA = 1.f - expf(-1.f / (att * sr)), cR = 1.f - expf(-1.f / (rel * sr)), cS = 1.f - expf(-1.f / (0.02f * sr));
    const float tone = map(PV_TONE, e[PV_TONE]), mod = map(PV_MOD, e[PV_MOD]) / 100.f, oct = exp2f(roundf(map(PV_OCTAVE, e[PV_OCTAVE])));
    const float effT = map(PV_EFFECT, e[PV_EFFECT]) / 100.f, dryT = map(PV_DRY, e[PV_DRY]) / 100.f;
    const float tapeA = 1.f - expf(-2 * PI_F * fminf(tone, 0.45f * sr) / sr), glide = 1.f - expf(-1.f / (0.015f * sr)), xfStep = 1.f / (0.02f * sr);
    const float swellTau[3] = {0.12f, 0.05f, 0.16f}, cQ = 1.f - expf(-1.f / (0.015f * sr));
    const float efA = 1.f - expf(-1.f / (0.0005f * sr)), efR = 1.f - expf(-1.f / (0.03f * sr)), esC = 1.f - expf(-1.f / (0.1f * sr));
    const float elR = expf(-1.f / (0.01f * sr)), el2C = 1.f - expf(-1.f / (0.008f * sr)); const int holdN = (int)(0.025f * sr);
    float envOf[PV_VOICES];
    for (int i = 0; i < n; i++) {
      const float x = 0.5f * (inL[i] + inR[i]), ax = fabsf(x);
      // the guitar's level: voices follow it (the tracker's own levels lag a 170 ms window); a quick rise is a pick
      ef += (ax - ef) * (ax > ef ? efA : efR); es += (ef - es) * esC; if (ax >= el) { el = ax; hold = holdN; } else if (hold > 0) hold--; else el *= elR;   // peaks held 25 ms (two periods of a low E)
      el2 += (el - el2) * el2C;
      ef = undenorm(ef); es = undenorm(es); el = undenorm(el); el2 = undenorm(el2);
      if (refr > 0) refr--; else if (ef > 2.f * es + 0.004f) { alloc.onset(); refr = (int)(0.06f * sr); }
      if (trk.push(x)) {                                      // a new hop: notes to voices
        for (int k = 0; k < PV_VOICES; k++) envOf[k] = vs[k].env;
        alloc.update(trk.notes, trk.nnotes, pvTape(model), envOf);
        maxLv = 1e-9f; for (int k = 0; k < PV_VOICES; k++) if (alloc.v[k].on) maxLv = fmaxf(maxLv, alloc.v[k].level);
        for (int k = 0; k < PV_VOICES; k++) if (alloc.v[k].fresh) { vs[k].age = 0.f; vs[k].fenv = 1.f; vs[k].mip = -1; if (vs[k].env < 1e-4f) { vs[k].hz = 0.f; vs[k].ic1 = vs[k].ic2 = 0.f; } alloc.v[k].fresh = false; }
      }
      // the tape transport: slow wow (a sine and a wandering drift) and quick flutter, shared by every key
      wowPh += 0.55f / sr; if (wowPh >= 1.f) { wowPh -= 1.f; wowTarget = noise(); }
      wowRnd += (wowTarget - wowRnd) * (1.f / sr);
      flutPh += 7.3f / sr; if (flutPh >= 1.f) flutPh -= 1.f;
      pwmPh += 0.7f / sr; if (pwmPh >= 1.f) pwmPh -= 1.f;
      const float wowC = mod * (16.f * sinf(2 * PI_F * wowPh) + 10.f * wowRnd) + mod * 5.f * sinf(2 * PI_F * flutPh);   // cents
      const float tapeRatio = exp2f(wowC / 1200.f), pw = 0.5f + 0.32f * sinf(2 * PI_F * pwmPh);
      const bool upd = (counter++ & 15) == 0;
      float wetL = 0.f, wetR = 0.f, tapeL = 0.f, tapeR = 0.f, tapeEnv = 0.f; int on = 0;
      for (int k = 0; k < PV_VOICES; k++) {
        VS& v = vs[k]; const auto& a = alloc.v[k];
        if (!a.on && v.env < 1e-5f) { v.env = 0.f; continue; }
        const float target = a.hz * oct;
        if (v.hz <= 0.f || pvTape(model)) v.hz = target; else v.hz += (target - v.hz) * glide;
        float hz = v.hz;
        if (upd || v.mip < 0) v.mip = (int)clampf(ceilf(log2f(fmaxf(hz * 1.01f, 1.f) / 27.5f)) - 1.f, 0.f, PV_MIPS - 1.f);   // its table (1% for wow)
        if (pvTape(model)) hz *= tapeRatio;
        if (model == 1 || prevModel == 1) hz *= exp2f(12.f * fminf(1.f, fmaxf(0.f, v.age - 0.4f) / 0.6f) * sinf(2 * PI_F * 5.2f * v.age) / 1200.f);   // the flute's vibrato
        const float dt = fminf(hz / sr, 0.45f);
        // its level: the guitar's (square-rooted: an even keyboard), shared by the notes as the spectrum says
        const float tgt = a.on ? 0.5f * sqrtf(el2) * (a.level / maxLv) : 0.f;
        v.env += (tgt - v.env) * (tgt > v.env ? cA : (a.on || v.age > QUICK ? cR : cQ));   // a note gone within 60 ms was a glitch: let it go fast
        v.age += 1.f / sr;
        // the model now (weight xf) and, while crossfading, the one before: tape voices with their own swell
        // and run-out go to the tape bus, synth voices through this voice's filter
        float tapeS = 0.f, synS = 0.f; int synM = -1;
        for (int side = 0; side < (xf < 1.f ? 2 : 1); side++) {
          const int mdl = side ? prevModel : model; const float w = side ? 1.f - xf : xf;
          float s = voiceSample(mdl, v, dt, pw) * w * LEVEL[mdl];
          if (pvTape(mdl)) {
            const float gT = (1.f - expf(-v.age / swellTau[mdl])) * (v.age > 8.f ? fmaxf(0.f, 1.f - (v.age - 8.f) / 0.6f) : 1.f);
            s *= gT; tapeEnv += v.env * gT * w;
            if (mdl == 1) s += 0.06f * w * gT * noise() * (1.f - expf(-v.age / 0.02f));   // breath
            tapeS += s;
          } else { synS += s; if (synM < 0) synM = mdl; }
        }
        if (synM >= 0) {                                      // a resonant low-pass per voice, opened by each new note
          v.fenv *= synM == 5 ? 0.99997f : 0.99994f;
          if (upd || v.k != (synM == 4 ? 1.2f : synM == 5 ? 0.7f : 0.9f)) {
            const float sweep = synM == 5 ? (1.f - v.fenv) * v.fenv * 4.f : v.fenv;
            const float fc = fminf(tone * exp2f(4.f * mod * sweep), 0.42f * sr), gg = tanf(PI_F * fc / sr);
            v.k = synM == 4 ? 1.2f : synM == 5 ? 0.7f : 0.9f;
            v.a1 = 1.f / (1.f + gg * (gg + v.k)); v.a2 = gg * v.a1; v.a3 = gg * v.a2;
          }
          const float v3 = synS - v.ic2, v1 = v.a1 * v.ic1 + v.a2 * v3, v2 = v.ic2 + v.a2 * v.ic1 + v.a3 * v3;
          v.ic1 = undenorm(2.f * v1 - v.ic1); v.ic2 = undenorm(2.f * v2 - v.ic2); synS = v2;
        } else { v.ic1 = v.ic2 = 0.f; }
        for (int j = 0; j < 3; j++) { v.ph[j] += dt * RATIO[model][j]; v.ph[j] -= floorf(v.ph[j]); }
        const float g = v.env * 1.4f, pan = (k & 1) ? 0.25f : -0.25f;
        tapeL += tapeS * g * (1.f - pan); tapeR += tapeS * g * (1.f + pan); wetL += synS * g * (1.f - pan); wetR += synS * g * (1.f + pan);
        on += v.env > 0.001f;
      }
      {                                                       // the tape bus: high-cut and hiss (always running, so a model change can fade it)
        hissLp += (noise() - hissLp) * 0.35f;
        const float hiss = hissLp * (0.0006f + 0.004f * mod) * fminf(1.f, tapeEnv * 8.f);   // only while tape is moving
        tapeLp += ((tapeL + tapeR) * 0.5f - tapeLp) * tapeA; tapeLp = undenorm(tapeLp);
        const float side = (tapeL - tapeR) * 0.5f * tapeA;
        wetL += tapeLp + side + hiss; wetR += tapeLp - side + hiss;
      }
      if (xf < 1.f) xf = fminf(1.f, xf + xfStep);
      effS += (effT - effS) * cS; dryS += (dryT - dryS) * cS;
      if (fabsf(effT - effS) < 1e-4f) effS = effT;              // land exactly (a one-pole stalls a float short of it)
      if (fabsf(dryT - dryS) < 1e-4f) dryS = dryT;
      oL[i] = dryS * inL[i] + effS * tanhf(wetL); oR[i] = dryS * inR[i] + effS * tanhf(wetR);
      sounding = on;
    }
  }
  void meters(float* out) const override {
    out[0] = (float)sounding;
    int nt[3] = {-1, -1, -1}; float lv[3] = {0, 0, 0};
    for (int k = 0; k < PV_VOICES; k++) {
      if (!alloc.v[k].on || vs[k].env < 0.001f || vs[k].hz <= 0) continue;
      const int note = (int)lrintf(69.f + 12.f * log2f(vs[k].hz / 440.f)); if (note < 0 || note > 127) continue;
      for (int j = 0; j < 3; j++) if (vs[k].env > lv[j]) { for (int q = 2; q > j; q--) { nt[q] = nt[q - 1]; lv[q] = lv[q - 1]; } nt[j] = note; lv[j] = vs[k].env; break; }
    }
    out[1] = (float)((nt[0] + 1) + 129 * (nt[1] + 1) + 129 * 129 * (nt[2] + 1));
  }
};

} // namespace rig
