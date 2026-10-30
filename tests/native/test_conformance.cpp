// test_conformance.cpp - the format every source and module must follow, checked for every type in
// the registries. A new source or module is covered by these tests the moment it's registered; a
// failure names the type and what it broke (docs/ADDING_A_SOURCE.md, docs/ADDING_A_MODULE.md).
#include "framework.h"
#include <set>
#include <array>
using namespace rigtest;

static const char* UNITS[] = {"", "dB", "ms", "s", "Hz", "%", "x", "bpm", "beats", "per beat", "st", "ct", "deg"};
static bool symbolOk(const char* s) { if (!s || !(s[0] >= 'a' && s[0] <= 'z')) return false; for (int i = 0; s[i]; i++) if (i > 15 || !((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) return false; return true; }
// Problems with a knob list, as text ("" = none).
static std::string knobProblems(int n, const ParamDesc* ps) {
  std::string out; std::set<std::string> seen;
  if (n < 0 || n > MAXPAR) return "more than 8 knobs";
  if (n > 0 && !ps) return "nparams > 0 but no params";
  for (int i = 0; i < n; i++) {
    const ParamDesc& q = ps[i]; std::string w = std::string(" knob ") + (q.symbol ? q.symbol : "?") + ":";
    if (!symbolOk(q.symbol)) out += w + " symbol must be [a-z][a-z0-9_]*, up to 16;";
    else if (!seen.insert(q.symbol).second) out += w + " symbol used twice;";
    if (!q.label || !q.label[0]) out += w + " no label;";
    if (!(q.max > q.min)) out += w + " max must be above min;";
    if (q.curve == Curve::Log && !(q.min > 0)) out += w + " a log knob needs min > 0;";
    if (!(q.def >= 0 && q.def <= 1)) out += w + " default must be 0..1 (normalised);";
    if (q.decimals < 0 || q.decimals > 4) out += w + " decimals 0..4;";
    if (q.nstops < 0 || (q.nstops > 0) != (q.stops != nullptr)) out += w + " nstops and stops must agree;";
    for (int k = 0; k < q.nstops && q.stops; k++) {
      if (!q.stops[k].name || !q.stops[k].name[0]) out += w + " a stop without a name;";
      if (!(q.stops[k].v >= q.min && q.stops[k].v <= q.max)) out += w + " a stop outside min..max;";
      if (k && !(q.stops[k].v > q.stops[k - 1].v)) out += w + " stops must ascend;";
    }
    bool unit = false; for (auto u : UNITS) unit = unit || (q.unit && !strcmp(q.unit, u)); if (!unit) out += w + " unit not in the list (docs);";
  }
  return out;
}

TEST("90 conformance: every source type's description follows the format") {
  std::set<std::string> ids; int bad = 0;
  printf("      %-8s %-16s knobs settings table meters ext\n", "type", "name");
  for (int t = 0; t < NSRCTYPES; t++) {
    const SourceDesc& d = sourceTypeDesc(t); std::string p = knobProblems(d.nparams, d.params);
    if (d.nsettings < 0 || d.nsettings > MAXCFG || (d.nsettings > 0 && !d.settings)) p += " settings: 0..24, with a list;";
    else { std::string s = knobProblems(d.nsettings > MAXPAR ? 0 : d.nsettings, d.settings); for (int i = 0; i < d.nsettings; i += MAXPAR) s += knobProblems(std::min(MAXPAR, d.nsettings - i), d.settings + i); if (!s.empty()) p += " settings:" + s; }
    for (int i = 0; i < d.nsettings; i++) for (int k = 0; k < d.nparams; k++) if (d.settings && !strcmp(d.settings[i].symbol, d.params[k].symbol)) p += std::string(" setting ") + d.settings[i].symbol + " has a knob's symbol;";
    if (d.rows < 0 || d.rows > MAXROWS || d.ncols < 0 || d.ncols > MAXCOLS || ((d.rows > 0) != (d.ncols > 0)) || (d.ncols > 0 && !d.cols)) p += " table: up to 32 rows x 6 columns, both or neither;";
    else if (d.ncols) { std::string s = knobProblems(d.ncols, d.cols); if (!s.empty()) p += " table:" + s; }
    if (!ModRack::validId(d.id) || strlen(d.id) > 12) p += " id must be [a-z][a-z0-9_]*, up to 12 (room for instance numbers);";
    else if (!ids.insert(d.id).second) p += " id used by another type;";
    if (!d.name || !d.name[0] || strlen(d.name) > 16) p += " name 1..16 characters;";
    if (!d.sub) p += " sub missing;";
    if (d.nmeters < 0 || d.nmeters > MAXSRCMETERS) p += " more than 2 meters;";
    for (int m = 0; m < d.nmeters && m < MAXSRCMETERS; m++) if (!d.meters[m] || !d.meters[m][0]) p += " meter without a name;";
    printf("      %-8s %-16s %5d %8d %2dx%-2d %6d %s\n", d.id, d.name, d.nparams, d.nsettings, d.rows, d.ncols, d.nmeters, d.ext ? "yes" : "-");
    if (!p.empty()) { printf("      ! %s:%s\n", d.id, p.c_str()); bad++; }
  }
  CHECK(bad == 0, "%d source types checked, %d with problems", NSRCTYPES, bad);
}

// Feed one instance directly (no engine): returns false and says why if an output is out of -1..1 or not finite.
static bool torture(int t, std::string& why) {
  const SourceDesc& d = sourceTypeDesc(t);
  std::vector<std::vector<float>> knobSets;
  std::vector<float> k(MAXPAR); for (int p = 0; p < d.nparams; p++) k[p] = d.params[p].def; knobSets.push_back(k);
  knobSets.push_back(std::vector<float>(MAXPAR, 0.f)); knobSets.push_back(std::vector<float>(MAXPAR, 1.f));
  unsigned r = 11; for (int s = 0; s < 3; s++) { for (auto& v : k) { r = r * 1664525u + 1013904223u; v = (r >> 8) / 16777216.f; } knobSets.push_back(k); }
  // settings and table: defaults, all 0, all 1, then random, alongside each knob set
  std::vector<std::vector<float>> cfgSets; std::vector<std::vector<std::array<float, MAXCOLS>>> tabSets;
  for (size_t ks = 0; ks < knobSets.size(); ks++) {
    std::vector<float> cf(MAXCFG, 0.f); std::vector<std::array<float, MAXCOLS>> tb(MAXROWS);
    for (int i = 0; i < d.nsettings; i++) { float v = d.settings[i].def; if (ks == 1) v = 0; if (ks == 2) v = 1; if (ks > 2) { r = r * 1664525u + 1013904223u; v = (r >> 8) / 16777216.f; } cf[i] = v; }
    for (int row = 0; row < d.rows; row++) for (int j = 0; j < d.ncols; j++) { float v = d.cols[j].def; if (ks == 1) v = 0; if (ks == 2) v = 1; if (ks > 2) { r = r * 1664525u + 1013904223u; v = (r >> 8) / 16777216.f; } tb[row][j] = v; }
    cfgSets.push_back(cf); tabSets.push_back(tb);
  }
  const char* inputs[] = {"silence", "riff", "full-scale noise", "DC +1", "square +-1"};
  for (float sr : {44100.f, 96000.f}) for (int blk : {1, 37, 128}) for (size_t ks = 0; ks < knobSets.size(); ks++) for (int in = 0; in < 5; in++) {
    Source* s = sourceTypes()[t].make(); s->init(sr);
    if (d.ext) { s->ext(1e9f); s->ext(-1e9f); s->ext(0.5f); }
    float buf[128]; unsigned n = 3; const auto& g = riffSignal();
    for (int i = 0, pos = 0; i < (int)(1.5f * sr / blk); i++, pos += blk) {
      for (int j = 0; j < blk; j++) { n = n * 1664525u + 1013904223u;
        buf[j] = in == 0 ? 0.f : in == 1 ? g[(pos + j) % g.size()] : in == 2 ? (n >> 8) / 8388608.f - 1.f : in == 3 ? 1.f : (((pos + j) / 50) % 2 ? 1.f : -1.f); }
      SourceContext c{buf, blk, sr, blk / sr, 120.f, knobSets[ks].data(), cfgSets[ks].data(), reinterpret_cast<const float (*)[MAXCOLS]>(tabSets[ks].data())};
      float v = s->update(c);
      if (d.taps) { unsigned cn; float cp; s->cycle(cn, cp);    // a tapping source: every phase of this cycle and the next, too
        if (!(cp >= 0.f && cp < 1.f)) v = NAN;
        for (float q : {0.f, 0.25f, 0.5f, 0.75f, 0.999f}) for (unsigned dn : {0u, 1u}) { const float a = s->at(q, cn + dn); if (!std::isfinite(a) || a < -1.f || a > 1.f) v = a; } }
      if (!std::isfinite(v) || v < -1.f || v > 1.f) {
        char m[200]; snprintf(m, sizeof m, "%s returned %g (%s, knob set %zu, block %d, %.0f Hz)", d.id, v, inputs[in], ks, blk, sr); why = m; delete s; return false;
      }
    }
    float mt[MAXSRCMETERS] = {0}; s->meters(mt); for (float x : mt) if (!std::isfinite(x)) { why = std::string(d.id) + ": a meter isn't finite"; delete s; return false; }
    delete s;
  }
  return true;
}

TEST("91 conformance: every source type stays in -1..1 and finite, whatever it's fed") {
  int bad = 0;
  for (int t = 0; t < NSRCTYPES; t++) { std::string why; if (!torture(t, why)) { printf("      ! %s\n", why.c_str()); bad++; } }
  CHECK(bad == 0, "%d types x 6 knob settings x 5 inputs x 3 block sizes x 2 rates: %d out of range", NSRCTYPES, bad);
}

TEST("92 conformance: every source type keeps its state to itself and never allocates in update()") {
  int shared = 0; long heap = 0;
  for (int t = 0; t < NSRCTYPES; t++) {
    Source *a = sourceTypes()[t].make(), *b = sourceTypes()[t].make(), *c = sourceTypes()[t].make();
    a->init(SR); b->init(SR); c->init(SR);
    const SourceDesc& d = sourceTypeDesc(t); float knobs[MAXPAR] = {0}; for (int p = 0; p < d.nparams; p++) knobs[p] = d.params[p].def;
    const auto& g = riffSignal(); float zero[128] = {0}; bool same = true;
    for (size_t i = 0; i + 128 <= std::min<size_t>(g.size(), (size_t)(4 * SR)); i += 128) {
      SourceContext ca{&g[i], 128, SR, 128 / SR, 120.f, knobs}, cz{zero, 128, SR, 128 / SR, 120.f, knobs};
      float vb = 0, vc = 0;
      heap += heapCallsDuring([&] { a->update(ca); vb = b->update(cz); });
      vc = c->update(cz);                           // c runs alone with the same input as b
      same = same && vb == vc;
    }
    if (!same) { printf("      ! %s: an instance's output depends on another instance (static or shared state)\n", d.id); shared++; }
    delete a; delete b; delete c;
  }
  CHECK(shared == 0, "%d types: two instances fed different signals stay independent", NSRCTYPES);
  if (heapCounting()) CHECK(heap == 0, "heap calls inside update(): %ld", heap); else CHECK(true, "heap counting needs glibc; skipped");
}

// Effect modules: the same checks, through the module contract.
template <class T> static std::string moduleCheck(long& heap) {
  T* m = new T(); m->init(SR); const ModuleDesc& d = m->desc();
  std::string p = knobProblems(d.nparams, d.params);
  if (!symbolOk(d.id)) p += " id must be [a-z][a-z0-9_]*;";
  if (!d.name || !d.name[0]) p += " no name;";
  if (d.nmeters < 0 || d.nmeters > MAXMODMETERS) p += " more than 2 meters;";
  float iL[128], iR[128], oL[128], oR[128], e[MAXPAR]; Context ctx; ctx.sr = SR; unsigned r = 9; float worst = 0;
  for (int set = 0; set < 4 && p.empty(); set++) {
    for (int k = 0; k < MAXPAR; k++) { r = r * 1664525u + 1013904223u; e[k] = set == 0 ? (k < d.nparams ? d.params[k].def : 0.f) : set == 1 ? 0.f : set == 2 ? 1.f : (r >> 8) / 16777216.f; }
    for (int b = 0; b < (int)(2 * SR / 128); b++) {
      for (int i = 0; i < 128; i++) { r = r * 1664525u + 1013904223u; iL[i] = iR[i] = ((r >> 8) / 8388608.f - 1.f) * (b % 2 ? 1.f : 0.05f); }
      heap += heapCallsDuring([&] { m->process(iL, iR, oL, oR, 128, e, ctx); });
      for (int i = 0; i < 128; i++) { if (!std::isfinite(oL[i]) || !std::isfinite(oR[i])) { p += " output not finite;"; break; } worst = fmaxf(worst, fmaxf(fabsf(oL[i]), fabsf(oR[i]))); }
    }
    m->reset();
  }
  if (worst > 64.f) p += " output above +36 dBFS on full-scale input;";
  printf("      %-6s %-12s knobs %d, peak out %.1f on full-scale noise\n", d.id, d.name, d.nparams, worst);
  delete m;
  return p;
}
TEST("93 conformance: every module type follows the format, stays finite and never allocates in process()") {
  int bad = 0; long heap = 0;
#define RIG_CHECK_MOD(T, member, cat) { std::string p = moduleCheck<T>(heap); if (!p.empty()) { printf("      ! %s:%s\n", #member, p.c_str()); bad++; } }
  RIG_MODULES(RIG_CHECK_MOD)
#undef RIG_CHECK_MOD
  CHECK(bad == 0, "%d module types x 4 knob settings on loud and quiet noise: %d with problems", NMODTYPES, bad);
  if (heapCounting()) CHECK(heap == 0, "heap calls inside process(): %ld", heap); else CHECK(true, "heap counting needs glibc; skipped");
}
