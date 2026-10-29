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
  float knob(const SourceContext& c, int i) const { return desc().params[i].map(c.p[i]); }   // real value of knob i
};

} // namespace rig
