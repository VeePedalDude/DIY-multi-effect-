// engine/core/notes.h - named stops shared by tempo-synced knobs (core/param.h ParamStop): note values
// in beats per cycle, and the FREE / SYNC switch. An LFO's RATE and an orbit's RATE land on the same values.
#pragma once
#include "param.h"

namespace rig {

inline constexpr ParamStop NOTE_STOPS[] = {
  {0.5f, "1/8"}, {2.f / 3.f, "1/4T"}, {0.75f, "1/8."}, {1.f, "1/4"}, {4.f / 3.f, "1/2T"}, {1.5f, "1/4."},
  {2.f, "1/2"}, {8.f / 3.f, "1/1T"}, {3.f, "1/2."}, {4.f, "1 bar"}, {6.f, "1 bar."}, {8.f, "2 bars"},
  {12.f, "3 bars"}, {16.f, "4 bars"}, {32.f, "8 bars"}, {64.f, "16 bars"}, {128.f, "32 bars"}};
constexpr int NNOTE_STOPS = 17;
inline constexpr ParamStop SYNC_STOPS[] = {{0, "FREE"}, {1, "SYNC"}};

} // namespace rig
