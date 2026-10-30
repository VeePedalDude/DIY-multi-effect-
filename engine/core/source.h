// engine/core/source.h - the contract every modulation source follows.
//
// A source turns anything (the guitar signal, tempo, physics, a sensor) into one
// value in -1..1 per block. Sources fed from outside the audio thread (a heart
// strap, a gamepad) receive their data through ext(). Sources have knobs too (an LFO's rate,
// an envelope's attack...), described exactly like a module's and set with `src_set`.
#pragma once
#include "param.h"

namespace rig {

constexpr int MAXSRCMETERS = 2;

struct SourceDesc {
  const char* id;                  // protocol name: mod_connect <inst> <symbol> <id> <depth>
  const char* name;                // orb label: "PICK ADSR"
  const char* sub;                 // one-line description
  const char* ext;                 // meaning of `ext <id> <value>`, or nullptr if unused
  int nmeters;
  const char* meters[MAXSRCMETERS];
  int nparams = 0;
  const ParamDesc* params = nullptr;
  bool taps = false;               // each patch can read it at its own phase and multiple (core/tap.h): cycle() and at()
};

struct SourceContext {
  const float* in;                 // dry guitar input for this block
  int n;
  float sr, dt, bpm;               // dt = n / sr
  const float* p;                  // this source's knobs, normalised 0..1 (see knob())
};

struct Source {
  float sr = 48000;
  virtual ~Source() {}
  virtual const SourceDesc& desc() const = 0;
  virtual void init(float s) { sr = s; }
  virtual float update(const SourceContext& c) = 0;      // returns -1..1
  virtual void ext(float v) { (void)v; }
  virtual void meters(float* out) const { (void)out; }
  // Sources with desc().taps: a periodic shape that each patch can read at its own phase offset and
  // multiple of the cycle (per-destination PHASE and DIV, core/tap.h). cycle() gives the whole
  // cycles done and the phase 0..1 after this block's update(); at() the output at phase `ph` of
  // cycle number `n` (n picks random values), with this block's settings. Both are read-only.
  virtual void cycle(unsigned& n, float& ph) const { n = 0; ph = 0; }
  virtual float at(float ph, unsigned n) const { (void)ph; (void)n; return 0.f; }
  float knob(const SourceContext& c, int i) const { return desc().params[i].map(c.p[i]); }   // real value of knob i
};

} // namespace rig
