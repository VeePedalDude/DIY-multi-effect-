// engine/core/module.h - the contract every effect module follows.
//
// A module describes itself (name, parameters with real-world ranges and units,
// display meters), so the engine, the text protocol and the UI all read the same
// facts from one place. Parameters arrive normalised 0..1; call map(i, v) to get
// the real value (dB, ms, Hz, %), using the same curve the UI shows.
#pragma once
#include "dsp.h"

namespace rig {

constexpr int MAXPAR = 8;          // parameters per module
constexpr int MAXMODMETERS = 2;    // display values a module can report

enum class Curve { Lin, Log };

struct ParamDesc {
  const char* symbol;              // protocol name: param_set <inst> <symbol> <0..1>
  const char* label;               // knob label in the UI
  float min, max;                  // real-world range
  float def;                       // default, normalised 0..1
  const char* unit;                // "dB", "ms", "Hz", "%", "s" or ""
  Curve curve;                     // Lin, or Log (min and max must be > 0)
  int decimals;                    // digits shown in the UI
  bool sign;                       // show a leading + for positive values
  float map(float v) const { return curve == Curve::Log ? min * powf(max / min, v) : min + (max - min) * v; }
};

struct ModuleDesc {
  const char* id;                  // short stable id: "holo"
  const char* name;                // face title: "HOLOGRAM"
  const char* kind;                // subtitle: "delay · dotted 8th"
  int nparams;
  const ParamDesc* params;
  int nmeters;
  const char* meters[MAXMODMETERS];  // names of values reported by meters()
};

// Shared per-block facts every module may read.
struct Context {
  float sr = 48000;
  float bpm = 120;
};

struct Module {
  float sr = 48000;
  virtual ~Module() {}
  virtual const ModuleDesc& desc() const = 0;
  virtual void init(float s) = 0;
  virtual void reset() = 0;
  // e[] = effective normalised params (0..1, already modulated) for this block.
  virtual void process(const float* inL, const float* inR, float* oL, float* oR, int n,
                       const float* e, const Context& ctx) = 0;
  // Optional display values (gate open, delay time...), up to MAXMODMETERS.
  virtual void meters(float* out) const { (void)out; }
  float map(int i, float v) const { return desc().params[i].map(v); }
};

} // namespace rig
