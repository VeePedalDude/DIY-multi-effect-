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
constexpr int MAXCFG = 24;                 // settings per source (not modulatable; see SourceDesc)
constexpr int MAXROWS = 32, MAXCOLS = 6;   // a source's table: rows x columns (per-step or per-vertex values)

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
  // Settings: like knobs (same ParamDesc, normalised 0..1, saved in presets) but not patch targets, for
  // choices nobody needs to modulate (a shape, a seed, a direction). `src_cfg <source> <symbol> <0..1>`.
  int nsettings = 0;
  const ParamDesc* settings = nullptr;
  // A table: `rows` rows of `ncols` values (per step, per vertex...), each column described like a knob.
  // `src_cell <source> <row> <symbol> <0..1>`.
  int rows = 0, ncols = 0;
  const ParamDesc* cols = nullptr;
};

struct SourceContext {
  const float* in;                 // dry guitar input for this block
  int n;
  float sr, dt, bpm;               // dt = n / sr
  const float* p;                  // this source's knobs, normalised 0..1 (see knob())
  const float* cfg = nullptr;      // its settings, normalised 0..1 (see setting())
  const float (*tab)[MAXCOLS] = nullptr;   // its table, normalised 0..1 (see cell())
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
  // Optional: numbers for the UI to draw the source with (a shape's vertices...), worked out from the
  // knobs and settings given (normalised), never from the running state. Returns how many it wrote.
  // Called on the UI's own copy of the engine (rig_view), never on the audio thread.
  virtual int view(const float* knobs, const float* cfg, float* out, int max) const { (void)knobs; (void)cfg; (void)out; (void)max; return 0; }
  float knob(const SourceContext& c, int i) const { return desc().params[i].map(c.p[i]); }   // real value of knob i
  float setting(const SourceContext& c, int i) const {            // real value of setting i (its default without a rack)
    const ParamDesc& q = desc().settings[i]; return q.map(c.cfg ? c.cfg[i] : q.def); }
  float cell(const SourceContext& c, int row, int col) const {    // real value of table cell (row, col)
    const ParamDesc& q = desc().cols[col]; return q.map(c.tab ? c.tab[row][col] : q.def); }
};

} // namespace rig
