// engine/core/param.h - how a knob describes itself. Modules and modulation sources both use it,
// so the protocol, the UI and preset files treat every knob the same way.
#pragma once
#include "dsp.h"

namespace rig {

constexpr int MAXPAR = 8;          // knobs per module or source

enum class Curve { Lin, Log };

// A named stop on a knob ("1/4", "SAW UP"), in real units. A knob with stops snaps to them when you
// turn it by hand and its readout shows the name; cross-modulation still moves it continuously, so a
// source that wants whole positions rounds (see nearestStop). Optional: most knobs have none.
struct ParamStop { float v; const char* name; };

struct ParamDesc {
  const char* symbol;              // protocol name: param_set <inst> <symbol> <0..1>, src_set <source> <symbol> <0..1>
  const char* label;               // knob label in the UI
  float min, max;                  // real-world range
  float def;                       // default, normalised 0..1
  const char* unit;                // "dB", "ms", "Hz", "%", "s", "x", "bpm", "beats" or ""
  Curve curve;                     // Lin, or Log (min and max must be > 0)
  int decimals;                    // digits shown in the UI
  bool sign;                       // show a leading + for positive values
  const ParamStop* stops = nullptr; // named stops, ascending (optional)
  int nstops = 0;
  float map(float v) const { return curve == Curve::Log ? min * powf(max / min, v) : min + (max - min) * v; }
  int nearestStop(float real) const {          // index of the stop closest to a real value (0 without stops)
    int b = 0; for (int i = 1; i < nstops; i++) if (fabsf(stops[i].v - real) < fabsf(stops[b].v - real)) b = i;
    return b;
  }
};

} // namespace rig
