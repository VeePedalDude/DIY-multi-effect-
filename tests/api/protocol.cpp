// tests/api/protocol.cpp - the text protocol through the C API (rig_exec), as every host uses it:
// each command and its errors (the same numbers the browser always gave), the rack/chain change
// flags, and the threading rig-host relies on: commands on a control thread while another thread
// plays audio, checked for finite output and that nothing gets lost. Build with -fsanitize=thread
// to have ThreadSanitizer watch the same run (see hosts/rig-host/README.md).
#include "../../engine/api/rig_api.cpp"
#include <cstdio>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

static int fails = 0;
#define CHECK(cond, ...) do { bool _ok = (cond); printf("  [%s] ", _ok ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!_ok) fails++; } while (0)
static int run(const std::string& line) { snprintf(rig_cmd_buf(), rig_cmd_len(), "%s", line.c_str()); return rig_exec(); }
static void play(int blocks) { for (int b = 0; b < blocks; b++) { for (int i = 0; i < 128; i++) rig_in()[i] = 0.2f * sinf(i * 0.07f); rig_process(128); } }

int main() {
  rig_init(48000);
  printf("protocol: every command, and its errors\n");
  struct Case { const char* line; int want; int changed; };
  const Case cases[] = {
    {"param_set holo feedback 0.6", 0, 0}, {"param_set 3 feedback 0.6", 0, 0}, {"param_set nope feedback 0.5", -1, 0},
    {"param_set holo nope 0.5", -103, 0}, {"param_set holo feedback 1.5", -207, 0}, {"param_set holo feedback", -207, 0},
    {"mod_connect holo halo lfo 0.3", 0, 0}, {"mod_connect holo halo lfo 0.3 90 3/2", 0, 0}, {"mod_connect holo halo lfo 0.3 0 0", -207, 0},
    {"mod_connect holo halo nobody 0.3", -207, 0}, {"mod_disconnect holo halo", 0, 0},
    {"bypass gate 1", 0, 0}, {"bypass gate 0", 0, 0}, {"bypass 99 1", -1, 0},
    {"set_bpm 90", 0, 0}, {"set_bpm 20", -207, 0}, {"master -6", 0, 0}, {"master 12", -207, 0},
    {"ext ctrl 0.5", 0, 0}, {"ext nobody 0.5", -207, 0}, {"input_channel 1", 0, 0},
    {"tuner 1", 0, 0}, {"tuner 1 mute 0", 0, 0}, {"tuner 2", -207, 0}, {"tuner 0", 0, 0}, {"tuner_ref 432", 0, 0}, {"tuner_ref 500", -207, 0},
    {"src_add lfo", 0, 1}, {"src_add lfo wobble", 0, 1}, {"src_add lfo wobble", -2, 0}, {"src_add nosuch", -1, 0},
    {"src_set wobble rate 0.25", 0, 0}, {"src_set wobble nope 0.25", -103, 0}, {"src_set wobble rate 2", -207, 0},
    {"src_mod wobble rate env 0.4", 0, 0}, {"src_mod wobble rate env 0.4 45 2", 0, 0}, {"src_mod wobble rate nobody 0.4", -207, 0}, {"src_unmod wobble rate", 0, 0},
    {"src_remove wobble", 0, 1}, {"src_remove wobble", -1, 0},
    {"fx_add holo", 0, 2}, {"fx_add tilt bright 0", 0, 2}, {"fx_add tilt bright", -2, 0}, {"fx_add nosuch", -1, 0}, {"fx_add holo - x", -207, 0},
    {"fx_move bright 3", 0, 2}, {"fx_move bright x", -207, 0}, {"fx_remove bright", 0, 2}, {"fx_remove bright", -1, 0},
    {"", -902, 0}, {"frobnicate 1", -902, 0},
  };
  int bad = 0;
  for (const Case& c : cases) {
    const int got = run(c.line), ch = rig_exec_changed();
    if (got != c.want || ch != c.changed) { printf("      ! \"%s\": %d (flags %d), want %d (flags %d)\n", c.line, got, ch, c.want, c.changed); bad++; }
    play(4);                                                     // let fades run, as audio would
  }
  CHECK(bad == 0, "%d commands: every status and change flag as the browser's protocol gave", (int)(sizeof cases / sizeof cases[0]));
  CHECK(rig_input_channel() == 1, "input_channel is kept for the host (%d)", rig_input_channel());
  CHECK(strstr(rig_chain(), "\"holo2\"") && !strstr(rig_chain(), "\"bright\""), "the chain JSON follows (holo2 added, bright gone)");

  // Settings and table cells: find a source type that has them, add one, and set them.
  int withCfg = -1, withTable = -1;
  for (int k = 0; k < rig::NSRCTYPES; k++) { if (rig::sourceTypeDesc(k).nsettings && withCfg < 0) withCfg = k; if (rig::sourceTypeDesc(k).rows && withTable < 0) withTable = k; }
  if (withCfg >= 0) {
    const rig::SourceDesc& d = rig::sourceTypeDesc(withCfg); run(std::string("src_add ") + d.id + " cfgtest");
    const int ok = run(std::string("src_cfg cfgtest ") + d.settings[0].symbol + " 0.5"), badSym = run("src_cfg cfgtest nope 0.5"), badV = run(std::string("src_cfg cfgtest ") + d.settings[0].symbol + " 7");
    CHECK(ok == 0 && badSym == -103 && badV == -207, "src_cfg on %s: 0, unknown setting -103, out of range -207", d.id);
    run("src_remove cfgtest");
  } else CHECK(true, "no source type has settings; src_cfg skipped");
  if (withTable >= 0) {
    const rig::SourceDesc& d = rig::sourceTypeDesc(withTable); run(std::string("src_add ") + d.id + " celltest");
    const std::string col = d.cols[0].symbol;
    const int ok = run("src_cell celltest 0 " + col + " 0.5"), badRow = run("src_cell celltest " + std::to_string(d.rows) + " " + col + " 0.5"), badSym = run("src_cell celltest 0 nope 0.5");
    CHECK(ok == 0 && badRow == -207 && badSym == -103, "src_cell on %s: 0, row past the table -207, unknown column -103", d.id);
    run("src_remove celltest");
  } else CHECK(true, "no source type has a table; src_cell skipped");

  printf("threads: a control thread sends commands while another plays audio (rig-host's arrangement)\n");
  std::atomic<bool> stop{false}; std::atomic<long> blocks{0}; std::atomic<int> nonFinite{0};
  std::thread audio([&] {
    while (!stop.load()) {
      for (int i = 0; i < 64; i++) rig_in()[i] = 0.25f * sinf((float)(blocks.load() * 64 + i) * 0.05f);
      rig_process(64);
      for (int i = 0; i < 64; i++) if (!std::isfinite(rig_out_l()[i]) || !std::isfinite(rig_out_r()[i])) nonFinite++;
      blocks++;
    }
  });
  const char* types[] = {"tilt", "ladder", "holo", "svf", "geq"}; int sent = 0, errs = 0;
  for (int round = 0; round < 400; round++) {
    char id[32]; snprintf(id, sizeof id, "t%d", round % 7);
    const int a = run(std::string("fx_add ") + types[round % 5] + " " + id);           // may be refused when full or taken: fine
    run(std::string("param_set ") + id + " " + (round % 2 ? "mix" : "cutoff") + " 0.4");
    snprintf(id, sizeof id, "s%d", round % 5);
    run(std::string("src_add lfo ") + id); run(std::string("mod_connect holo halo ") + id + " 0.3"); run(std::string("src_set ") + id + " rate 0.3");
    if (round % 3 == 0) { run(std::string("src_remove ") + id); snprintf(id, sizeof id, "t%d", (round + 3) % 7); run(std::string("fx_remove ") + id); }
    if (round % 5 == 0) run("fx_move holo 0");
    rig_collect();
    sent += 7; (void)a;
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  // Clean up what's left, let the fades finish, and see the engine still answers sensibly.
  for (int i = 0; i < 7; i++) { char id[32]; snprintf(id, sizeof id, "t%d", i); run(std::string("fx_remove ") + id); }
  for (int i = 0; i < 5; i++) { char id[32]; snprintf(id, sizeof id, "s%d", i); run(std::string("src_remove ") + id); }
  std::this_thread::sleep_for(std::chrono::milliseconds(100)); rig_collect();
  stop = true; audio.join();
  errs = run("param_set holo feedback 0.5");
  CHECK(nonFinite.load() == 0 && blocks.load() > 1000, "%d commands while %ld audio blocks played: every sample finite", sent, blocks.load());
  CHECK(errs == 0 && !strstr(rig_chain(), "\"t0\"") && !strstr(rig_rack(), "\"s0\""), "afterwards the chain and rack are back to what was left, and commands still work");
  printf(fails ? "\n%d FAILED\n" : "\nALL PROTOCOL CHECKS PASSED\n", fails);
  return fails ? 1 : 0;
}
