// engine/modules/registry.h - THE list of effect modules, in signal-chain order.
// Adding a module: write engine/modules/<id>.h, include it here, add one X(...) line.
// X(Type, member) - member is the engine field name and should match the module's id.
#pragma once
#include "gate.h"
#include "amp.h"
#include "cab.h"
#include "holo.h"
#include "tess.h"

#define RIG_MODULES(X) \
  X(Gate, gate)        \
  X(Amp,  amp)         \
  X(Cab,  cab)         \
  X(Holo, holo)        \
  X(Tess, tess)
