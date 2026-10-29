// engine/sources/rand.h - RANDOM: a new value every RATE steps per beat (sample and hold), gliding
// to it over SLEW. Output -1..1, evenly spread. Default 2 per beat, no glide.
//
// TEMPLATE for a new modulation source (docs/ADDING_A_SOURCE.md). Copy to engine/sources/<id>.h,
// rename RAND/RandomSH/"rand", and keep the shape: description first, then the struct.
#pragma once
#include "../core/source.h"

namespace rig {

// The knobs. Each default is normalised (0..1); centre the range on the default (0.5 on a log curve).
//                 symbol   label    min    max     default unit        curve       decimals sign
inline constexpr ParamDesc RAND_PARAMS[] = {
  {"rate", "RATE", 0.5f, 8.f,   0.5f, "per beat", Curve::Log, 1, false},   // 2 per beat
  {"slew", "SLEW", 0.f,  500.f, 0.f,  "ms",       Curve::Lin, 0, false},   // jumps
};
//                                       id      orb label  one-line description  ext      meters                  knobs
inline constexpr SourceDesc RAND_DESC = {"rand", "RANDOM", "sample and hold", nullptr, 1, {"steps", nullptr}, 2, RAND_PARAMS};

struct RandomSH : Source {
  // All state lives in members (two instances in one rack must never share anything), sized up
  // front: nothing in update() may allocate.
  float ph = 0, target = 0, value = 0; unsigned seed = 0x9E3779B9u; int steps = 0;
  const SourceDesc& desc() const override { return RAND_DESC; }
  void init(float s) override { sr = s; }
  // Once per block (c.n samples, ~2.7 ms at 128). c.in is the dry guitar, c.bpm the tempo,
  // knob(c, i) knob i in real units AFTER cross-modulation, so it can change every block: work out
  // coefficients here, cheaply, and never let a jump in a knob make a jump in the output by itself.
  float update(const SourceContext& c) override {
    ph += c.dt * c.bpm / 60.f * knob(c, 0);
    if (ph >= 1.f) { ph -= floorf(ph); seed = seed * 1664525u + 1013904223u; target = (seed >> 8) / 8388608.f - 1.f; steps++; }
    const float slew = knob(c, 1) / 1000.f;
    value = slew <= 0.f ? target : value + (1.f - expf(-c.dt / slew)) * (target - value);
    return clampf(value, -1.f, 1.f);                  // always -1..1, always finite
  }
  void meters(float* out) const override { out[0] = (float)steps; }   // up to 2 display values, named in the desc
};

} // namespace rig
