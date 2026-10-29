// engine/sources/registry.h - THE list of modulation sources, in UI order.
// Adding a source: write engine/sources/<id>.h, include it here, add one X(...) line.
#pragma once
#include "lfo.h"
#include "env.h"
#include "adsr.h"
#include "orbit.h"
#include "heart.h"
#include "ctrl.h"

#define RIG_SOURCES(X)  \
  X(Lfo,       lfo)     \
  X(EnvFollow, env)     \
  X(PickAdsr,  adsr)    \
  X(Orbit,     orbit)   \
  X(Heart,     heart)   \
  X(Ctrl,      ctrl)
