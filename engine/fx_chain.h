// engine/fx_chain.h - the effect chain's building blocks: module types as a table, and the chain
// order packed into one atomic word so the control thread can change it while audio runs.
//
//  - Module *types* are the registry (engine/modules/registry.h, generated from the category folders
//    by tools/gen_registry.py): one file each. The chain holds up to MAXMOD *instances* of them in
//    slots, in any order, several of one type if you like ("holo", "holo2"). A fresh chain is the
//    list in engine/modules/default_chain.txt, ids = type ids.
//  - The order is a list of slots. It fits in 64 bits (4 bits for the length, 4 per slot), so the
//    control thread publishes a new one with a single atomic store and the audio thread reads it
//    once per block. Engine (engine.h) does the rest: fades, dips, freeing.
// Nothing in here names a specific module.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include "modules/registry.h"

namespace rig {

#define RIG_COUNT(T, m, c) +1
constexpr int NMODTYPES = 0 RIG_MODULES(RIG_COUNT);
constexpr int NMODCATS = 0 RIG_MODULE_CATEGORIES(RIG_COUNT);
#undef RIG_COUNT
constexpr int MAXMOD = 12;                // instances in the chain (the order packs into 64 bits)
constexpr int MOD_ID_LEN = 16;            // id, including the terminating zero
static_assert(MAXMOD <= 15, "the chain order packs a slot into 4 bits");

struct ModuleType { const ModuleDesc& (*desc)(); Module* (*make)(); const char* category; };
#define RIG_MTYPE(T, m, c) ModuleType{ []() -> const ModuleDesc& { static T* proto = new T(); return proto->desc(); }, []() -> Module* { return new T(); }, #c },
inline const ModuleType* moduleTypes() { static const ModuleType t[NMODTYPES] = { RIG_MODULES(RIG_MTYPE) }; return t; }
#undef RIG_MTYPE
inline const ModuleDesc& moduleTypeDesc(int t) { return moduleTypes()[t].desc(); }
// The categories (the folders, and the tabs of the add panel), in order.
struct Category { const char* id; const char* label; const char* blurb; };
#define RIG_MCAT(c, label, blurb) Category{ #c, label, blurb },
inline const Category* moduleCategories() { static const Category t[NMODCATS] = { RIG_MODULE_CATEGORIES(RIG_MCAT) }; return t; }
#undef RIG_MCAT
// What a fresh chain holds, in order (engine/modules/default_chain.txt).
inline const char* const* defaultChain(int& n) { static const char* const ids[] = { RIG_DEFAULT_CHAIN }; n = (int)(sizeof ids / sizeof ids[0]); return ids; }
inline int moduleTypeIndex(const char* type) { for (int t = 0; t < NMODTYPES; t++) if (!strcmp(moduleTypeDesc(t).id, type)) return t; return -1; }

enum ChainError { CHAIN_BAD_TYPE = -1, CHAIN_FULL = -2, CHAIN_BAD_ID = -3, CHAIN_NO_SUCH = -4 };

// A chain order: n slots, first to last.
struct Order {
  int n = 0; int s[MAXMOD] = {0};
  uint64_t pack() const { uint64_t w = (uint64_t)n; for (int i = 0; i < n; i++) w |= (uint64_t)s[i] << (4 + 4 * i); return w; }
  static Order unpack(uint64_t w) { Order o; o.n = (int)(w & 15); for (int i = 0; i < o.n; i++) o.s[i] = (int)((w >> (4 + 4 * i)) & 15); return o; }
  int indexOf(int slot) const { for (int i = 0; i < n; i++) if (s[i] == slot) return i; return -1; }
  void insert(int at, int slot) { if (n >= MAXMOD) return; at = at < 0 || at > n ? n : at; for (int i = n; i > at; i--) s[i] = s[i - 1]; s[at] = slot; n++; }
  void erase(int slot) { int i = indexOf(slot); if (i < 0) return; for (; i < n - 1; i++) s[i] = s[i + 1]; n--; }
};

inline bool validInstanceId(const char* id) {
  if (!id || !(id[0] >= 'a' && id[0] <= 'z')) return false;
  int n = 0; for (; id[n]; n++) { const char c = id[n]; if (n >= MOD_ID_LEN - 1 || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false; }
  return true;
}

} // namespace rig
