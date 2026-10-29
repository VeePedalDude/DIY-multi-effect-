// engine/core/param.h - how a knob describes itself. Modules and modulation sources both use it,
// so the protocol, the UI and preset files treat every knob the same way.
#pragma once
#include "dsp.h"

namespace rig {

constexpr int MAXPAR = 8;          // knobs per module or source

enum class Curve { Lin, Log };

struct ParamDesc {
  const char* symbol;              // protocol name: param_set <inst> <symbol> <0..1>, src_set <source> <symbol> <0..1>
  const char* label;               // knob label in the UI
  float min, max;                  // real-world range
  float def;                       // default, normalised 0..1
  const char* unit;                // "dB", "ms", "Hz", "%", "s", "x", "bpm", "beats" or ""
  Curve curve;                     // Lin, or Log (min and max must be > 0)
  int decimals;                    // digits shown in the UI
  bool sign;                       // show a leading + for positive values
  float map(float v) const { return curve == Curve::Log ? min * powf(max / min, v) : min + (max - min) * v; }
};

} // namespace rig
