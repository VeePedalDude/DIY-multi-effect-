/*
 * Chorus Lab - RIG/01 chorus effect (CLAP plugin)
 * Copyright (C) 2026 KC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eight selectable voicings sharing one stereo delay line:
 *   Classic (BBD-style), Dimension, Ensemble, Tri-Chorus, Vibrato,
 *   Detune, Lo-Fi (clocked), Random.
 * Voicing changes crossfade between the old and new engine (equal power,
 * smoothstep shaped, 30 ms). All other params are smoothed (5 ms).
 *
 * v1.1 tone stages (all bypassed at their default, so v1.0 presets are unchanged):
 *   input:  Drive (asymmetric preamp, dry + wet)
 *   wet in: pre-emphasis -> 2:1 compressor -> BBD-style clip -> delay line
 *   wet out: 1:2 expander -> de-emphasis -> Low Cut -> Lush (allpass diffusion)
 *   feedback loop: Shift (pitch shifter, -12..+12 semitones)
 *   output: Mono Bass (lows below the crossover summed to mono)
 */

#include <clap/clap.h>

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#define CL_HAVE_SSE 1
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CL_TWO_PI (2.0 * M_PI)

/* ------------------------------------------------------------------ */
/* Parameters                                                          */
/* ------------------------------------------------------------------ */

enum {
   P_VOICING = 0,
   P_RATE = 1,
   P_DEPTH = 2,
   P_MIX = 3,
   P_TONE = 4,
   P_FEEDBACK = 5,
   P_WIDTH = 6,
   P_TIME = 7,
   P_LEVEL = 8,
   P_DRIVE = 9,
   P_COMPANDER = 10,
   P_EMPHASIS = 11,
   P_LOWCUT = 12,
   P_MONOBASS = 13,
   P_LUSH = 14,
   P_SHIFT = 15,
   P_COUNT = 16
};

enum {
   V_CLASSIC = 0,
   V_DIMENSION,
   V_ENSEMBLE,
   V_TRI,
   V_VIBRATO,
   V_DETUNE,
   V_LOFI,
   V_RANDOM,
   V_COUNT
};

static const char *const k_voicing_names[V_COUNT] = {
   "Classic", "Dimension", "Ensemble", "Tri-Chorus",
   "Vibrato", "Detune", "Lo-Fi", "Random",
};

typedef struct {
   clap_id id;
   const char *name;
   double min, max, def;
   uint32_t flags;
   const char *module; /* CLAP module path (panel group) */
} param_desc_t;

#define PF_MOD (CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)

static const param_desc_t k_params[P_COUNT] = {
   /* Voicing is discrete: switching topology is crossfaded, not modulatable. */
   {P_VOICING, "Voicing", 0.0, (double)(V_COUNT - 1), 0.0,
    CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM, "Main"},
   {P_RATE, "Rate", 0.05, 10.0, 0.8, PF_MOD, "Main"},
   {P_DEPTH, "Depth", 0.0, 100.0, 50.0, PF_MOD, "Main"},
   {P_MIX, "Mix", 0.0, 100.0, 50.0, PF_MOD, "Main"},
   {P_TONE, "Tone", 0.0, 100.0, 70.0, PF_MOD, "Main"},
   {P_FEEDBACK, "Feedback", -90.0, 90.0, 0.0, PF_MOD, "Feedback"},
   {P_WIDTH, "Width", 0.0, 100.0, 100.0, PF_MOD, "Space"},
   {P_TIME, "Time", 50.0, 200.0, 100.0, PF_MOD, "Space"},
   {P_LEVEL, "Level", -24.0, 12.0, 0.0, PF_MOD, "Main"},
   {P_DRIVE, "Drive", 0.0, 100.0, 0.0, PF_MOD, "Character"},
   {P_COMPANDER, "Compander", 0.0, 100.0, 0.0, PF_MOD, "Character"},
   {P_EMPHASIS, "Emphasis", 0.0, 100.0, 0.0, PF_MOD, "Character"},
   {P_LOWCUT, "Low Cut", 20.0, 800.0, 20.0, PF_MOD, "Low End"},
   {P_MONOBASS, "Mono Bass", 20.0, 400.0, 20.0, PF_MOD, "Low End"},
   {P_LUSH, "Lush", 0.0, 100.0, 0.0, PF_MOD, "Space"},
   {P_SHIFT, "Shift", -12.0, 12.0, 0.0, PF_MOD, "Feedback"},
};

/* Order the host lists params in (CLAP param index). Ids never change; only the
 * order does: most-used controls first, then grouped by panel. */
static const clap_id k_order[P_COUNT] = {
   P_VOICING, P_RATE,      P_DEPTH,    P_MIX,      P_TONE, P_LEVEL, /* Main      */
   P_DRIVE,   P_COMPANDER, P_EMPHASIS,                              /* Character */
   P_LUSH,    P_WIDTH,     P_TIME,                                  /* Space     */
   P_FEEDBACK, P_SHIFT,                                             /* Feedback  */
   P_LOWCUT,  P_MONOBASS,                                           /* Low End   */
};

#define OFF_HZ 20.5 /* Low Cut / Mono Bass at or below this are bypassed */

static double clampd(double v, double lo, double hi) {
   return v < lo ? lo : (v > hi ? hi : v);
}

/* ------------------------------------------------------------------ */
/* Voicing tables                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
   double base_ms; /* centre delay at Time = 100 % */
   double span_ms; /* +/- sweep at Depth = 100 %, Time = 100 % */
   int voices;
} voicing_shape_t;

static const voicing_shape_t k_shape[V_COUNT] = {
   /* Classic   */ {7.0, 4.0, 1},
   /* Dimension */ {6.0, 1.2, 2},
   /* Ensemble  */ {8.0, 2.5, 3},
   /* Tri       */ {10.0, 4.0, 3},
   /* Vibrato   */ {5.0, 3.0, 1},
   /* Detune    */ {4.0, 25.0, 2}, /* span = pitch-shift window */
   /* Lo-Fi     */ {9.0, 4.0, 1},
   /* Random    */ {9.0, 4.0, 3},
};

#define MAX_VOICES 3
#define MAX_DELAY_MS 120.0 /* > worst case (detune: (4 + 25) * 2 + slack) */
#define SHIFT_WIN_MS 50.0  /* feedback pitch-shifter window */
#define N_ALLPASS 4        /* Lush: allpass stages per channel */
static const double k_ap_ms[2][N_ALLPASS] = {{3.7, 6.1, 9.3, 13.9}, {4.3, 6.7, 10.1, 15.1}};
#define EMPH_HZ 1800.0     /* pre/de-emphasis shelf corner */
#define COMP_REF 0.1       /* compander unity level (about -20 dBFS) */
#define COMP_FLOOR 1e-4    /* compander envelope floor (-80 dBFS) */
#define COMP_TAU_MS 10.0   /* compander rectifier time constant (NE570-like) */
#define XFADE_MS 30.0
#define SMOOTH_MS 5.0
#define COEF_INTERVAL 16 /* filter coefficients refresh on a global sample grid */

/* ------------------------------------------------------------------ */
/* DSP building blocks                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
   float ic1, ic2;
} svf_t;

/* Cytomic TPT state-variable lowpass, Q = 0.707 */
static inline float svf_lp(svf_t *s, float x, float g, float k) {
   float a1 = 1.0f / (1.0f + g * (g + k));
   float a2 = g * a1;
   float a3 = g * a2;
   float v3 = x - s->ic2;
   float v1 = a1 * s->ic1 + a2 * v3;
   float v2 = s->ic2 + a2 * s->ic1 + a3 * v3;
   s->ic1 = 2.0f * v1 - s->ic1;
   s->ic2 = 2.0f * v2 - s->ic2;
   return v2;
}

/* Same TPT SVF, returning the 2-pole highpass. */
static inline float svf_hp(svf_t *s, float x, float g, float k) {
   float a1 = 1.0f / (1.0f + g * (g + k));
   float a2 = g * a1;
   float a3 = g * a2;
   float v3 = x - s->ic2;
   float v1 = a1 * s->ic1 + a2 * v3;
   float v2 = s->ic2 + a2 * s->ic1 + a3 * v3;
   s->ic1 = 2.0f * v1 - s->ic1;
   s->ic2 = 2.0f * v2 - s->ic2;
   return x - k * v1 - v2;
}

static inline uint32_t xorshift32(uint32_t *s) {
   uint32_t x = *s;
   x ^= x << 13;
   x ^= x >> 17;
   x ^= x << 5;
   *s = x;
   return x;
}

static inline double rand_bipolar(uint32_t *s) {
   return ((double)(xorshift32(s) >> 8) / 8388608.0) - 1.0; /* [-1, 1) */
}

typedef struct {
   double phase;            /* main LFO phase [0,1) */
   double fast_phase;       /* ensemble shimmer LFO */
   double rnd_phase;        /* random segment phase */
   double rnd_prev[MAX_VOICES], rnd_next[MAX_VOICES];
   double rnd_smooth[MAX_VOICES];
   uint32_t rng;
   double dt_d[2][2];       /* detune: [voice][tap] delay in samples */
   int dt_act[2];           /* detune: active tap per voice */
   uint32_t dt_xf[2];       /* detune: splice crossfade position */
   int dt_init;
   double dt_cents;         /* detune: cents the cached slopes were computed for */
   double dt_slope[2];      /* detune: cached delay slope per voice (0 when cents == 0) */
   double pan_w;            /* width the cached pan gains belong to (-1 = none) */
   float pan_l[MAX_VOICES], pan_r[MAX_VOICES];
   double clk_phase[2];     /* lo-fi clocks */
   float hold[2];
   svf_t tone[2];
   svf_t tone2[2];
   float g, k;              /* tone filter coefficients */
} engine_t;

typedef struct {
   double cur, target, coef;
} smoother_t;

static inline double smooth_tick(smoother_t *s) {
   if (s->cur != s->target) {
      double d = s->target - s->cur;
      if (fabs(d) <= 1e-9 * fabs(s->target) + 1e-18)
         s->cur = s->target; /* settle exactly instead of stalling a rounding step short */
      else
         s->cur += d * s->coef;
   }
   return s->cur;
}

/* ------------------------------------------------------------------ */
/* Plugin instance                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
   clap_plugin_t plugin;
   const clap_host_t *host;
   const clap_host_params_t *host_params;

   /* Knob values shared with the main thread (bit patterns of doubles). */
   _Atomic uint64_t shared_value[P_COUNT];
   atomic_int main_dirty; /* main thread changed values; audio thread re-reads */

   /* Audio-thread copies */
   double value[P_COUNT]; /* knob position */
   double mod[P_COUNT];   /* modulation offset */

   double sr;
   int active;

   float *line[2];
   uint32_t line_size, line_mask, w;
   uint64_t sample_count;

   smoother_t sm[P_COUNT];
   smoother_t sm_dry, sm_wet, sm_gain;

   engine_t eng[V_COUNT];
   int voicing_cur, voicing_prev, voicing_pending;
   uint32_t xfade_pos, xfade_len;

   float fb_l, fb_r;         /* feedback signal from previous sample */
   double rnd_coef;          /* random-LFO smoothing coefficient (depends on sr only) */
   double dc_x[2], dc_y[2], dc_r;

   /* v1.1 tone stages */
   double drv_g, drv_c, drv_t, drv_norm;   /* drive shaper, refreshed on the coef grid */
   double drv_dcx[2], drv_dcy[2], drv_dcr; /* DC blocker after the asymmetric shaper */
   double emph_G;                          /* one-pole TPT coefficient at EMPH_HZ */
   double pre_s[2], de_s[2];               /* pre/de-emphasis one-pole states */
   double env_c[2], env_e[2], env_coef;    /* compressor / expander rectifier envelopes */
   int comp_warm, emph_warm;               /* stage state is tracking (engaged or engaging) */
   int lowcut_warm, monob_warm, lush_warm;
   float lush_in, lush_in_step;            /* diffuser input fade-in after engaging */
   int sh_warm;                            /* shifter is recording (engaged or engaging) */
   int v11_active;                         /* any v1.1 stage engaged, engaging or settling */
   float sh_in;                            /* shifter input fade-in after engaging */
   double gc[2], gc_step[2], ge[2], ge_step[2]; /* compander gains, ramped across the grid */
   svf_t lowcut[2], monob[2];
   float lowcut_g, monob_g;
   float *ap_buf;                          /* Lush allpass delay memory (one block) */
   uint32_t ap_len[2][N_ALLPASS], ap_off[2][N_ALLPASS], ap_idx[2][N_ALLPASS];
   float *sh_buf[2];                       /* feedback pitch-shifter delay memory */
   uint32_t sh_mask, sh_w;
   double sh_slope, sh_st, sh_win;         /* delay change per sample, cached shift, window */
   double sh_d[2];                         /* two read taps (delay, samples) */
   int sh_act;                             /* active tap */
   uint32_t sh_xf, sh_xlen;                /* splice crossfade position / length */
} chorus_t;

static uint64_t d2u(double d) {
   uint64_t u;
   memcpy(&u, &d, sizeof u);
   return u;
}
static double u2d(uint64_t u) {
   double d;
   memcpy(&d, &u, sizeof d);
   return d;
}

static double eff_value(const chorus_t *c, int id) {
   const param_desc_t *p = &k_params[id];
   return clampd(c->value[id] + c->mod[id], p->min, p->max);
}

/* Clear per-run state of an engine that is fading in (filters, hold, detune taps).
 * LFO phases and RNG keep running so the result stays deterministic. */
static void engine_restart(engine_t *e) {
   memset(e->tone, 0, sizeof e->tone);
   memset(e->tone2, 0, sizeof e->tone2);
   e->hold[0] = e->hold[1] = 0.0f;
   e->clk_phase[0] = e->clk_phase[1] = 0.0;
   e->dt_init = 0;
}

/* Recompute smoother targets from knob + mod for one param. */
static void update_target(chorus_t *c, int id) {
   double v = eff_value(c, id);
   switch (id) {
   case P_VOICING: {
      int nv = (int)lrint(clampd(c->value[P_VOICING], 0.0, V_COUNT - 1));
      if (c->xfade_pos < c->xfade_len)
         c->voicing_pending = nv;
      else if (nv != c->voicing_cur) {
         c->voicing_prev = c->voicing_cur;
         c->voicing_cur = nv;
         c->xfade_pos = 0;
         c->voicing_pending = nv;
         engine_restart(&c->eng[nv]);
      } else
         c->voicing_pending = nv;
      break;
   }
   case P_MIX: {
      double a = v / 100.0 * (M_PI / 2.0);
      c->sm_dry.target = cos(a);
      c->sm_wet.target = sin(a);
      break;
   }
   case P_LEVEL:
      c->sm_gain.target = pow(10.0, v / 20.0);
      break;
   default:
      c->sm[id].target = v;
      if (id >= P_DRIVE)
         c->v11_active = 1; /* the full path re-evaluates; it drops back when all are off */
      break;
   }
}

/* All v1.1 params at their (off) defaults with smoothing settled. */
static int v11_settled_off(const chorus_t *c) {
   for (int id = P_DRIVE; id < P_COUNT; ++id)
      if (c->sm[id].cur != k_params[id].def || c->sm[id].target != k_params[id].def)
         return 0;
   return 1;
}

static void snap_smoothers(chorus_t *c) {
   for (int i = 0; i < P_COUNT; ++i) {
      update_target(c, i);
      c->sm[i].cur = c->sm[i].target;
   }
   c->sm_dry.cur = c->sm_dry.target;
   c->sm_wet.cur = c->sm_wet.target;
   c->sm_gain.cur = c->sm_gain.target;
}

static void engine_reset(engine_t *e, int index) {
   memset(e, 0, sizeof *e);
   e->rng = 0x9E3779B9u ^ (uint32_t)(index * 2654435761u);
   if (e->rng == 0)
      e->rng = 1;
   for (int v = 0; v < MAX_VOICES; ++v) {
      e->rnd_prev[v] = 0.0;
      e->rnd_next[v] = rand_bipolar(&e->rng);
   }
   e->g = 0.5f;
   e->k = 1.41421356f;
   e->pan_w = -1.0;
}

static void reset_dsp(chorus_t *c) {
   if (c->line[0]) {
      memset(c->line[0], 0, sizeof(float) * c->line_size);
      memset(c->line[1], 0, sizeof(float) * c->line_size);
   }
   c->w = 0;
   c->sample_count = 0;
   for (int i = 0; i < V_COUNT; ++i)
      engine_reset(&c->eng[i], i);
   c->voicing_cur = c->voicing_prev = c->voicing_pending =
      (int)lrint(clampd(c->value[P_VOICING], 0.0, V_COUNT - 1));
   c->xfade_pos = c->xfade_len;
   c->fb_l = c->fb_r = 0.0f;
   c->dc_x[0] = c->dc_x[1] = c->dc_y[0] = c->dc_y[1] = 0.0;
   for (int ch = 0; ch < 2; ++ch) {
      c->drv_dcx[ch] = c->drv_dcy[ch] = 0.0;
      c->pre_s[ch] = c->de_s[ch] = 0.0;
      c->env_c[ch] = c->env_e[ch] = 0.0;
      c->gc[ch] = c->ge[ch] = 1.0;
      c->gc_step[ch] = c->ge_step[ch] = 0.0;
   }
   memset(c->lowcut, 0, sizeof c->lowcut);
   memset(c->monob, 0, sizeof c->monob);
   c->comp_warm = c->emph_warm = 0;
   c->lowcut_warm = c->monob_warm = c->lush_warm = 0;
   c->lush_in = 0.0f;
   c->sh_warm = 0;
   c->sh_in = 0.0f;
   c->drv_g = 1.0;
   c->drv_c = c->drv_t = 0.0;
   c->drv_norm = 1.0;
   c->lowcut_g = c->monob_g = 0.0f;
   if (c->ap_buf) {
      uint32_t tot = 0;
      for (int ch = 0; ch < 2; ++ch)
         for (int i = 0; i < N_ALLPASS; ++i) {
            tot += c->ap_len[ch][i];
            c->ap_idx[ch][i] = 0;
         }
      memset(c->ap_buf, 0, sizeof(float) * tot);
   }
   if (c->sh_buf[0]) {
      memset(c->sh_buf[0], 0, sizeof(float) * (c->sh_mask + 1));
      memset(c->sh_buf[1], 0, sizeof(float) * (c->sh_mask + 1));
   }
   c->sh_w = 0;
   c->sh_slope = 0.0;
   c->sh_st = 0.0;
   c->sh_d[0] = c->sh_d[1] = 0.5 * c->sh_win;
   c->sh_act = 0;
   c->sh_xf = c->sh_xlen;
   snap_smoothers(c);
}

/* Read the delay line at a fractional delay (samples, >= 2) with 4-point Hermite. */
static inline float tap(const chorus_t *c, int ch, double delay) {
   double maxd = (double)c->line_size - 4.0;
   if (delay < 2.0)
      delay = 2.0;
   else if (delay > maxd)
      delay = maxd;
   double pos = (double)c->w - delay;
   double fl = floor(pos);
   float f = (float)(pos - fl);
   int64_t i = (int64_t)fl;
   const float *b = c->line[ch];
   uint32_t m = c->line_mask;
   float xm1 = b[(uint32_t)(i - 1) & m];
   float x0 = b[(uint32_t)i & m];
   float x1 = b[(uint32_t)(i + 1) & m];
   float x2 = b[(uint32_t)(i + 2) & m];
   float c1 = 0.5f * (x1 - xm1);
   float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
   float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
   return ((c3 * f + c2) * f + c1) * f + x0;
}

/* Detune splice search (WSOLA-style): find the offset in [lo, hi] samples from the
 * active tap whose recent history best matches the active tap's, so the crossfade
 * joins two in-phase copies instead of cancelling. Fixed cost, no allocation. */
#define SPLICE_CORR_LEN 192
static double find_splice_ring(const float *bl, const float *br, uint32_t m, uint32_t w,
                               double d_from, int lo, int hi, int dir) {
   int64_t a0 = (int64_t)w - (int64_t)lrint(d_from);
   double best = -1e300;
   int best_off = hi;
   const int step = (hi - lo) / 300 + 1; /* bound the search to ~300 candidates at any rate */
   for (int off = lo; off <= hi; off += step) {
      int64_t b0 = a0 - (int64_t)dir * off;
      double ab = 0.0, bb = 1e-12;
      for (int k = 0; k < SPLICE_CORR_LEN; k += 2) {
         float a = bl[(uint32_t)(a0 - k) & m] + br[(uint32_t)(a0 - k) & m];
         float b = bl[(uint32_t)(b0 - k) & m] + br[(uint32_t)(b0 - k) & m];
         ab += (double)a * b;
         bb += (double)b * b;
      }
      double score = ab / sqrt(bb);
      if (score > best) {
         best = score;
         best_off = off;
      }
   }
   return d_from + (double)dir * (double)best_off;
}

static double find_splice(const chorus_t *c, double d_from, int lo, int hi, int dir) {
   return find_splice_ring(c->line[0], c->line[1], c->line_mask, c->w, d_from, lo, hi, dir);
}

static inline double tri(double ph) {
   ph -= floor(ph);
   return ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph;
}

/* tanh: Lambert continued fraction (7/6) for |x| <= 3 (abs error < 1e-6), libm beyond. */
static inline double fast_tanh(double x) {
   if (x > 3.0 || x < -3.0)
      return tanh(x);
   double x2 = x * x;
   return x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2))) /
          (135135.0 + x2 * (62370.0 + x2 * (3150.0 + 28.0 * x2)));
}

/* Hermite read from an arbitrary power-of-two ring (delay >= 2 samples). */
static inline float ring_tap(const float *b, uint32_t mask, uint32_t w, double delay) {
   double pos = (double)w - delay;
   double fl = floor(pos);
   float f = (float)(pos - fl);
   int64_t i = (int64_t)fl;
   float xm1 = b[(uint32_t)(i - 1) & mask], x0 = b[(uint32_t)i & mask];
   float x1 = b[(uint32_t)(i + 1) & mask], x2 = b[(uint32_t)(i + 2) & mask];
   float c1 = 0.5f * (x1 - xm1);
   float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
   float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
   return ((c3 * f + c2) * f + c1) * f + x0;
}

/* Float version of the same rational, for the emphasis clip stage. */
static inline float fast_tanhf(float x) {
   if (x > 3.0f || x < -3.0f)
      return tanhf(x);
   float x2 = x * x;
   return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) /
          (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
}

static inline double smoothstep(double t) { return t * t * (3.0 - 2.0 * t); }

/* Constant-power pan, p in [-1, 1] */
static inline void pan_gains(double p, float *gl, float *gr) {
   double a = (p + 1.0) * (M_PI / 4.0);
   *gl = (float)cos(a);
   *gr = (float)sin(a);
}

typedef struct {
   double rate, depth, tone, width, time;
} frame_params_t;

/* One sample of one voicing engine. Writes wet L/R and the feedback source. */
static void engine_tick(chorus_t *c, int vo, const frame_params_t *fp,
                        float *wl, float *wr, float *fbl, float *fbr) {
   engine_t *e = &c->eng[vo];
   const voicing_shape_t *sh = &k_shape[vo];
   const double sr = c->sr;
   const double ms = sr / 1000.0;
   const double time = fp->time / 100.0;
   const double depth = fp->depth / 100.0;
   const double width = fp->width / 100.0;
   const double base = sh->base_ms * time * ms;
   const double span = sh->span_ms * time * ms * depth;
   float L = 0.0f, R = 0.0f, sl = 0.0f, sr_ = 0.0f;

   /* Tone filter coefficients on a global grid (block-size independent). */
   if ((c->sample_count % COEF_INTERVAL) == 0) {
      double fc = 1500.0 * pow(12.0, fp->tone / 100.0);
      if (vo == V_CLASSIC)
         fc *= 0.6; /* BBD voicing is darker */
      if (vo == V_LOFI) {
         double clk = 256.0 / (2.0 * base / sr); /* 256-stage "BBD" clock */
         if (fc > 0.45 * clk)
            fc = 0.45 * clk;
      }
      if (fc > 0.45 * sr)
         fc = 0.45 * sr;
      e->g = (float)tan(M_PI * fc / sr);
   }

   const double inc = fp->rate / sr;

   switch (vo) {
   case V_CLASSIC:
   case V_LOFI: {
      double d = base + span * tri(e->phase);
      float a = tap(c, 0, d), b = tap(c, 1, d);
      if (vo == V_CLASSIC) {
         a = tanhf(0.7f * a) * (1.0f / 0.7f);
         b = tanhf(0.7f * b) * (1.0f / 0.7f);
      } else {
         double clk = 256.0 / (2.0 * d / sr);
         float in2[2] = {a, b};
         for (int ch = 0; ch < 2; ++ch) {
            e->clk_phase[ch] += clk / sr;
            if (e->clk_phase[ch] >= 1.0) {
               e->clk_phase[ch] -= floor(e->clk_phase[ch]);
               e->hold[ch] = roundf(in2[ch] * 127.0f) * (1.0f / 127.0f);
            }
         }
         a = e->hold[0];
         b = e->hold[1];
      }
      sl = a;
      sr_ = b;
      /* Boss-style stereo: R gets the wet inverted as width rises. */
      L = a;
      R = b * (float)(1.0 - 2.0 * width);
      e->phase += inc;
      break;
   }
   case V_VIBRATO: {
      double dl = base + span * sin(CL_TWO_PI * e->phase);
      double dr = base + span * sin(CL_TWO_PI * (e->phase + 0.25 * width));
      L = sl = tap(c, 0, dl);
      R = sr_ = tap(c, 1, dr);
      e->phase += inc;
      break;
   }
   case V_DIMENSION: {
      double s = sin(CL_TWO_PI * e->phase);
      double da = base + span * s, db = base - span * s;
      float al = tap(c, 0, da), ar = tap(c, 1, da);
      float bl = tap(c, 0, db), br = tap(c, 1, db);
      float w = (float)width;
      /* width 1: A left, B right; width 0: both centred */
      L = (al * (0.5f + 0.5f * w) + bl * (0.5f - 0.5f * w)) * 1.0f;
      R = (br * (0.5f + 0.5f * w) + ar * (0.5f - 0.5f * w)) * 1.0f;
      sl = 0.5f * (al + bl);
      sr_ = 0.5f * (ar + br);
      e->phase += inc;
      break;
   }
   case V_ENSEMBLE:
   case V_TRI:
   case V_RANDOM: {
      static const double pans_ens[3] = {-0.8, 0.0, 0.8};
      static const double pans_tri[3] = {-1.0, 0.0, 1.0};
      const double *pans = vo == V_ENSEMBLE ? pans_ens : pans_tri;
      if (width != e->pan_w) {
         e->pan_w = width;
         for (int v = 0; v < 3; ++v)
            pan_gains(pans[v] * width, &e->pan_l[v], &e->pan_r[v]);
      }
      if (vo == V_RANDOM) {
         e->rnd_phase += inc;
         if (e->rnd_phase >= 1.0) {
            e->rnd_phase -= floor(e->rnd_phase);
            for (int v = 0; v < 3; ++v) {
               e->rnd_prev[v] = e->rnd_next[v];
               e->rnd_next[v] = rand_bipolar(&e->rng);
            }
         }
      }
      const double sm_coef = c->rnd_coef;
      const double rnd_t = vo == V_RANDOM ? 0.5 - 0.5 * cos(M_PI * e->rnd_phase) : 0.0;
      /* Ensemble: voice LFOs are 120 deg apart, so derive all three from one
       * sin/cos pair per oscillator by angle addition. */
      double es[3] = {0.0, 0.0, 0.0};
      if (vo == V_ENSEMBLE) {
         const double c120 = -0.5, s120 = 0.86602540378443864676;
         double sa = sin(CL_TWO_PI * e->phase), ca = cos(CL_TWO_PI * e->phase);
         double sb = sin(CL_TWO_PI * e->fast_phase), cb = cos(CL_TWO_PI * e->fast_phase);
         es[0] = 0.85 * sa + 0.15 * sb;
         es[1] = 0.85 * (sa * c120 + ca * s120) + 0.15 * (sb * c120 + cb * s120);
         es[2] = 0.85 * (sa * c120 - ca * s120) + 0.15 * (sb * c120 - cb * s120);
      }
      for (int v = 0; v < 3; ++v) {
         double l;
         if (vo == V_ENSEMBLE) {
            l = es[v];
         } else if (vo == V_TRI) {
            l = tri(e->phase + (double)v / 3.0);
         } else {
            double target = e->rnd_prev[v] + (e->rnd_next[v] - e->rnd_prev[v]) * rnd_t;
            e->rnd_smooth[v] += (target - e->rnd_smooth[v]) * sm_coef;
            l = e->rnd_smooth[v];
         }
         double d = base + span * l;
         float tl = tap(c, 0, d), tr = tap(c, 1, d);
         L += tl * e->pan_l[v];
         R += tr * e->pan_r[v];
         sl += tl;
         sr_ += tr;
      }
      /* 3 voices, 1/sqrt(3) each, +3 dB pan-law makeup */
      L *= 0.81649658f;
      R *= 0.81649658f;
      sl *= (1.0f / 3.0f);
      sr_ *= (1.0f / 3.0f);
      e->phase += inc;
      e->fast_phase += 6.3 / sr;
      break;
   }
   case V_DETUNE: {
      /* Two voices at +cents / -cents. Each voice is a delay tap moving at a constant
       * slope (exact pitch ratio); when it reaches the window edge it splices to a
       * correlation-matched point at the far side with a 10 ms crossfade. */
      const double cents = 30.0 * depth;
      const double win = sh->span_ms * time * ms;
      const double lo_edge = base, hi_edge = base + win;
      const uint32_t xlen = (uint32_t)(0.010 * sr) + 1;
      const int search = (int)(0.5 * win);
      if (!e->dt_init) {
         for (int v = 0; v < 2; ++v) {
            e->dt_d[v][0] = e->dt_d[v][1] = base + 0.5 * win;
            e->dt_act[v] = 0;
            e->dt_xf[v] = xlen;
         }
         e->dt_init = 1;
      }
      if (width != e->pan_w) {
         e->pan_w = width;
         pan_gains(-width, &e->pan_l[0], &e->pan_r[0]);
         pan_gains(width, &e->pan_l[1], &e->pan_r[1]);
      }
      if (cents != e->dt_cents) {
         e->dt_cents = cents;
         for (int v = 0; v < 2; ++v)
            e->dt_slope[v] = 1.0 - pow(2.0, (v == 0 ? cents : -cents) / 1200.0);
      }
      for (int v = 0; v < 2; ++v) {
         const double slope = e->dt_slope[v]; /* delay change per sample (<0: pitch up) */
         const int a = e->dt_act[v], b = a ^ 1;
         float vl, vr;
         if (e->dt_xf[v] < xlen) {
            double t = smoothstep((double)(e->dt_xf[v] + 1) / (double)xlen);
            float gn = (float)t, go = (float)(1.0 - t);
            vl = go * tap(c, 0, e->dt_d[v][b]) + gn * tap(c, 0, e->dt_d[v][a]);
            vr = go * tap(c, 1, e->dt_d[v][b]) + gn * tap(c, 1, e->dt_d[v][a]);
            ++e->dt_xf[v];
         } else {
            vl = tap(c, 0, e->dt_d[v][a]);
            vr = tap(c, 1, e->dt_d[v][a]);
            const double margin = fabs(slope) * (double)xlen;
            int hit_lo = slope < 0.0 && e->dt_d[v][a] - margin <= lo_edge;
            int hit_hi = slope > 0.0 && e->dt_d[v][a] + margin >= hi_edge;
            if ((hit_lo || hit_hi) && search >= 2) {
               int hi_off = (int)win - 2, lo_off = hi_off - search;
               double nd = find_splice(c, e->dt_d[v][a], lo_off, hi_off, hit_lo ? 1 : -1);
               e->dt_d[v][b] = nd;
               e->dt_act[v] = b;
               e->dt_xf[v] = 0;
            }
         }
         e->dt_d[v][0] += slope;
         e->dt_d[v][1] += slope;
         L += vl * e->pan_l[v];
         R += vr * e->pan_r[v];
         sl += vl;
         sr_ += vr;
      }
      sl *= 0.5f;
      sr_ *= 0.5f;
      break;
   }
   default:
      break;
   }

   e->phase -= floor(e->phase);
   e->fast_phase -= floor(e->fast_phase);

   /* Tone (wet only). Lo-Fi and Classic use a 4-pole slope. */
   L = svf_lp(&e->tone[0], L, e->g, e->k);
   R = svf_lp(&e->tone[1], R, e->g, e->k);
   if (vo == V_CLASSIC || vo == V_LOFI) {
      L = svf_lp(&e->tone2[0], L, e->g, e->k);
      R = svf_lp(&e->tone2[1], R, e->g, e->k);
   }
   *wl = L;
   *wr = R;
   *fbl = sl;
   *fbr = sr_;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

static void publish_value(chorus_t *c, int id) {
   atomic_store_explicit(&c->shared_value[id], d2u(c->value[id]), memory_order_relaxed);
}

static void handle_event(chorus_t *c, const clap_event_header_t *h) {
   if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
      return;
   if (h->type == CLAP_EVENT_PARAM_VALUE) {
      const clap_event_param_value_t *ev = (const clap_event_param_value_t *)h;
      if (ev->param_id >= P_COUNT || !isfinite(ev->value))
         return;
      int id = (int)ev->param_id;
      double v = clampd(ev->value, k_params[id].min, k_params[id].max);
      if (k_params[id].flags & CLAP_PARAM_IS_STEPPED)
         v = (double)lrint(v);
      c->value[id] = v;
      publish_value(c, id);
      update_target(c, id);
   } else if (h->type == CLAP_EVENT_PARAM_MOD) {
      const clap_event_param_mod_t *ev = (const clap_event_param_mod_t *)h;
      if (ev->param_id >= P_COUNT || !isfinite(ev->amount))
         return;
      int id = (int)ev->param_id;
      if (!(k_params[id].flags & CLAP_PARAM_IS_MODULATABLE))
         return;
      c->mod[id] = ev->amount;
      update_target(c, id);
   }
}

static void pull_main_values(chorus_t *c) {
   if (atomic_exchange_explicit(&c->main_dirty, 0, memory_order_acquire)) {
      for (int i = 0; i < P_COUNT; ++i) {
         c->value[i] = u2d(atomic_load_explicit(&c->shared_value[i], memory_order_relaxed));
         update_target(c, i);
      }
   }
}

/* ------------------------------------------------------------------ */
/* Plugin callbacks                                                    */
/* ------------------------------------------------------------------ */

static bool cl_init(const clap_plugin_t *p) {
   chorus_t *c = (chorus_t *)p->plugin_data;
   c->host_params = (const clap_host_params_t *)c->host->get_extension(c->host, CLAP_EXT_PARAMS);
   return true;
}

static void free_lines(chorus_t *c) {
   free(c->line[0]);
   free(c->line[1]);
   free(c->ap_buf);
   free(c->sh_buf[0]);
   free(c->sh_buf[1]);
   c->line[0] = c->line[1] = NULL;
   c->ap_buf = NULL;
   c->sh_buf[0] = c->sh_buf[1] = NULL;
}

static void cl_destroy(const clap_plugin_t *p) {
   chorus_t *c = (chorus_t *)p->plugin_data;
   free_lines(c);
   free(c);
}

static bool cl_activate(const clap_plugin_t *p, double sample_rate, uint32_t min_frames,
                        uint32_t max_frames) {
   (void)min_frames;
   (void)max_frames;
   chorus_t *c = (chorus_t *)p->plugin_data;
   if (!(sample_rate >= 1000.0 && sample_rate <= 1536000.0))
      return false;
   c->sr = sample_rate;
   uint32_t need = (uint32_t)ceil(MAX_DELAY_MS * 0.001 * sample_rate) + 8;
   uint32_t size = 1;
   while (size < need)
      size <<= 1;
   free_lines(c);
   c->line[0] = (float *)calloc(size, sizeof(float));
   c->line[1] = (float *)calloc(size, sizeof(float));
   if (!c->line[0] || !c->line[1]) {
      free_lines(c);
      return false;
   }
   c->line_size = size;
   c->line_mask = size - 1;

   /* Lush allpasses */
   uint32_t tot = 0;
   for (int ch = 0; ch < 2; ++ch)
      for (int i = 0; i < N_ALLPASS; ++i) {
         uint32_t len = (uint32_t)lrint(k_ap_ms[ch][i] * 0.001 * sample_rate);
         if (len < 1)
            len = 1;
         c->ap_len[ch][i] = len;
         c->ap_off[ch][i] = tot;
         tot += len;
      }
   c->ap_buf = (float *)calloc(tot, sizeof(float));
   /* Feedback pitch shifter */
   uint32_t sneed = (uint32_t)ceil(1.5 * SHIFT_WIN_MS * 0.001 * sample_rate) + 8, ssize = 1;
   while (ssize < sneed)
      ssize <<= 1;
   c->sh_buf[0] = (float *)calloc(ssize, sizeof(float));
   c->sh_buf[1] = (float *)calloc(ssize, sizeof(float));
   if (!c->ap_buf || !c->sh_buf[0] || !c->sh_buf[1]) {
      free_lines(c);
      return false;
   }
   c->sh_mask = ssize - 1;
   c->sh_win = SHIFT_WIN_MS * 0.001 * sample_rate;
   c->sh_xlen = (uint32_t)(0.010 * sample_rate) + 1;
   {
      double g = tan(M_PI * EMPH_HZ / sample_rate);
      c->emph_G = g / (1.0 + g);
   }
   c->env_coef = 1.0 - exp(-1.0 / (COMP_TAU_MS * 0.001 * sample_rate));
   c->drv_dcr = 1.0 - CL_TWO_PI * 10.0 / sample_rate;
   c->lush_in_step = (float)(1.0 / (0.005 * sample_rate));

   double coef = 1.0 - exp(-1.0 / (SMOOTH_MS * 0.001 * sample_rate));
   for (int i = 0; i < P_COUNT; ++i)
      c->sm[i].coef = coef;
   c->sm_dry.coef = c->sm_wet.coef = c->sm_gain.coef = coef;
   c->xfade_len = (uint32_t)lrint(XFADE_MS * 0.001 * sample_rate);
   c->dc_r = 1.0 - CL_TWO_PI * 20.0 / sample_rate;
   c->rnd_coef = 1.0 - exp(-CL_TWO_PI * 8.0 / sample_rate);

   for (int i = 0; i < P_COUNT; ++i)
      c->value[i] = u2d(atomic_load_explicit(&c->shared_value[i], memory_order_relaxed));
   atomic_store_explicit(&c->main_dirty, 0, memory_order_relaxed);
   memset(c->mod, 0, sizeof c->mod);
   reset_dsp(c);
   c->active = 1;
   return true;
}

static void cl_deactivate(const clap_plugin_t *p) {
   chorus_t *c = (chorus_t *)p->plugin_data;
   c->active = 0;
   free_lines(c);
}

static bool cl_start_processing(const clap_plugin_t *p) {
   (void)p;
   return true;
}
static void cl_stop_processing(const clap_plugin_t *p) { (void)p; }

static void cl_reset(const clap_plugin_t *p) {
   chorus_t *c = (chorus_t *)p->plugin_data;
   memset(c->mod, 0, sizeof c->mod);
   reset_dsp(c);
}

/* Current voicing engine (plus the outgoing one during a voicing crossfade).
 * Writes the wet pair and stores the feedback source for the next sample. */
static inline void run_engines(chorus_t *c, const frame_params_t *fp, float *outl, float *outr) {
   float wl, wrr, fl, fr;
   engine_tick(c, c->voicing_cur, fp, &wl, &wrr, &fl, &fr);
   if (c->xfade_pos < c->xfade_len) {
      float ol, orr, ofl, ofr;
      engine_tick(c, c->voicing_prev, fp, &ol, &orr, &ofl, &ofr);
      double t = smoothstep((double)(c->xfade_pos + 1) / (double)c->xfade_len);
      float gn = (float)sin(t * (M_PI / 2.0)), go = (float)cos(t * (M_PI / 2.0));
      wl = wl * gn + ol * go;
      wrr = wrr * gn + orr * go;
      fl = fl * gn + ofl * go;
      fr = fr * gn + ofr * go;
      if (++c->xfade_pos >= c->xfade_len && c->voicing_pending != c->voicing_cur) {
         int nv = c->voicing_pending;
         c->voicing_prev = c->voicing_cur;
         c->voicing_cur = nv;
         c->xfade_pos = 0;
         engine_restart(&c->eng[nv]);
      }
   }
   c->fb_l = fl;
   c->fb_r = fr;
   *outl = wl;
   *outr = wrr;
}

static clap_process_status cl_process(const clap_plugin_t *p, const clap_process_t *proc) {
   chorus_t *c = (chorus_t *)p->plugin_data;
#ifdef CL_HAVE_SSE
   unsigned int saved_csr = _mm_getcsr();
   _mm_setcsr(saved_csr | 0x8040u); /* FTZ | DAZ */
#endif
   pull_main_values(c);

   const uint32_t frames = proc->frames_count;
   const uint32_t nev = proc->in_events ? proc->in_events->size(proc->in_events) : 0;
   uint32_t ei = 0;

   const float *inL = NULL, *inR = NULL;
   float *outL = NULL, *outR = NULL;
   if (proc->audio_inputs_count > 0 && proc->audio_inputs[0].data32) {
      const clap_audio_buffer_t *b = &proc->audio_inputs[0];
      if (b->channel_count > 0) {
         inL = b->data32[0];
         inR = b->channel_count > 1 ? b->data32[1] : b->data32[0];
      }
   }
   if (proc->audio_outputs_count > 0 && proc->audio_outputs[0].data32) {
      const clap_audio_buffer_t *b = &proc->audio_outputs[0];
      if (b->channel_count > 0) {
         outL = b->data32[0];
         outR = b->channel_count > 1 ? b->data32[1] : NULL;
      }
   }

   for (uint32_t n = 0; n < frames; ++n) {
      while (ei < nev) {
         const clap_event_header_t *h = proc->in_events->get(proc->in_events, ei);
         if (h->time > n)
            break;
         handle_event(c, h);
         ++ei;
      }

      float xl = inL ? inL[n] : 0.0f;
      float xr = inR ? inR[n] : 0.0f;
      if (!isfinite(xl))
         xl = 0.0f;
      if (!isfinite(xr))
         xr = 0.0f;

      frame_params_t fp;
      fp.rate = smooth_tick(&c->sm[P_RATE]);
      fp.depth = smooth_tick(&c->sm[P_DEPTH]);
      fp.tone = smooth_tick(&c->sm[P_TONE]);
      fp.width = smooth_tick(&c->sm[P_WIDTH]);
      fp.time = smooth_tick(&c->sm[P_TIME]);
      double fb = smooth_tick(&c->sm[P_FEEDBACK]) / 100.0;
      double dry = smooth_tick(&c->sm_dry);
      double wet = smooth_tick(&c->sm_wet);
      double gain = smooth_tick(&c->sm_gain);

      if (!c->v11_active) {
         /* Every v1.1 stage is off and settled: the v1.0 signal path, unchanged. */
         double fsrc[2] = {c->fb_l, c->fb_r};
         float wr_in[2] = {xl, xr};
         for (int ch = 0; ch < 2; ++ch) {
            double y = fsrc[ch] - c->dc_x[ch] + c->dc_r * c->dc_y[ch];
            c->dc_x[ch] = fsrc[ch];
            c->dc_y[ch] = y;
            c->line[ch][c->w] = wr_in[ch] + (float)(fb * fast_tanh(y));
         }
         float wl, wrr;
         run_engines(c, &fp, &wl, &wrr);
         if (outL)
            outL[n] = (float)((dry * xl + wet * wl) * gain);
         if (outR)
            outR[n] = (float)((dry * xr + wet * wrr) * gain);
         c->w = (c->w + 1) & c->line_mask;
         ++c->sample_count;
         continue;
      }

      const double drive = smooth_tick(&c->sm[P_DRIVE]) / 100.0;
      const double comp = smooth_tick(&c->sm[P_COMPANDER]) / 100.0;
      const double emph = smooth_tick(&c->sm[P_EMPHASIS]) / 100.0;
      const double lowcut = smooth_tick(&c->sm[P_LOWCUT]);
      const double monob = smooth_tick(&c->sm[P_MONOBASS]);
      const double lush = smooth_tick(&c->sm[P_LUSH]) / 100.0;
      const double shift = smooth_tick(&c->sm[P_SHIFT]);

      /* Coefficients for the new stages, on the same global 16-sample grid. */
      if ((c->sample_count % COEF_INTERVAL) == 0) {
         if (drive > 0.0) {
            c->drv_g = 1.0 + 24.0 * drive * drive;         /* up to +28 dB into the shaper */
            c->drv_c = 0.3 * drive;                        /* asymmetry -> even harmonics */
            c->drv_t = fast_tanh(c->drv_c); /* same function as the shaper: silence stays 0 */
            double slope = c->drv_g * (1.0 - c->drv_t * c->drv_t);
            c->drv_norm = pow(c->drv_g, 0.35) / slope;     /* gentle makeup as it clips */
         }
         for (int ch = 0; ch < 2 && (comp > 0.0 || c->sm[P_COMPANDER].target > 0.0); ++ch) {
            double a = comp;
            double ec = c->env_c[ch] > COMP_FLOOR ? c->env_c[ch] : COMP_FLOOR;
            double ee = c->env_e[ch] > COMP_FLOOR ? c->env_e[ch] : COMP_FLOOR;
            /* compressor: ratio 1/(1-a/2); expander restores it: exponent a/(2-a) */
            double tc = a > 0.0 ? pow(COMP_REF / ec, 0.5 * a) : 1.0;
            double te = a > 0.0 ? pow(ee / COMP_REF, a / (2.0 - a)) : 1.0;
            if (tc > 31.6)
               tc = 31.6;
            c->gc_step[ch] = (tc - c->gc[ch]) / COEF_INTERVAL;
            c->ge_step[ch] = (te - c->ge[ch]) / COEF_INTERVAL;
         }
         if (lowcut > OFF_HZ)
            c->lowcut_g = (float)tan(M_PI * (lowcut < 0.45 * c->sr ? lowcut : 0.45 * c->sr) / c->sr);
         if (monob > OFF_HZ)
            c->monob_g = (float)tan(M_PI * (monob < 0.45 * c->sr ? monob : 0.45 * c->sr) / c->sr);
         if (shift != c->sh_st) {
            c->sh_st = shift;
            c->sh_slope = 1.0 - pow(2.0, shift / 12.0); /* delay change per sample (<0: up) */
         }
      }

      /* Input drive (dry and wet). */
      if (drive > 0.0) {
         float in2[2] = {xl, xr};
         const double dmix = drive < 0.2 ? drive / 0.2 : 1.0; /* fade the shaper in */
         for (int ch = 0; ch < 2; ++ch) {
            double sh = (fast_tanh(c->drv_g * in2[ch] + c->drv_c) - c->drv_t) * c->drv_norm;
            sh = in2[ch] + dmix * (sh - in2[ch]);
            double y = sh - c->drv_dcx[ch] + c->drv_dcr * c->drv_dcy[ch];
            c->drv_dcx[ch] = sh;
            c->drv_dcy[ch] = y;
            in2[ch] = (float)y;
         }
         xl = in2[0];
         xr = in2[1];
      } else if (c->drv_dcx[0] != 0.0 || c->drv_dcx[1] != 0.0) {
         c->drv_dcx[0] = c->drv_dcy[0] = c->drv_dcx[1] = c->drv_dcy[1] = 0.0;
      }

      /* Feedback: DC-blocked source, optionally pitch shifted, soft-limited. */
      double fsrc[2] = {c->fb_l, c->fb_r};
      double fy[2];
      const int sh_on = shift != 0.0 || c->sm[P_SHIFT].target != 0.0;
      if (sh_on && !c->sh_warm) {
         /* start recording from silence with a short fade-in (no onset step) */
         memset(c->sh_buf[0], 0, sizeof(float) * (c->sh_mask + 1));
         memset(c->sh_buf[1], 0, sizeof(float) * (c->sh_mask + 1));
         c->sh_in = 0.0f;
         c->sh_d[0] = c->sh_d[1] = 0.5 * c->sh_win;
         c->sh_act = 0;
         c->sh_xf = c->sh_xlen;
      }
      c->sh_warm = sh_on;
      if (sh_on && c->sh_in < 1.0f) {
         c->sh_in += c->lush_in_step;
         if (c->sh_in > 1.0f)
            c->sh_in = 1.0f;
      }
      for (int ch = 0; ch < 2; ++ch) {
         double y = fsrc[ch] - c->dc_x[ch] + c->dc_r * c->dc_y[ch];
         c->dc_x[ch] = fsrc[ch];
         c->dc_y[ch] = y;
         fy[ch] = y;
         if (sh_on)
            c->sh_buf[ch][c->sh_w] = (float)y * c->sh_in;
      }
      double sw = fabs(shift) * 2.0;
      if (sw > 0.0) {
         /* Two taps moving at a constant slope (exact ratio); at the window edge the
          * idle tap jumps to a correlation-matched point and takes over (10 ms fade),
          * so the splice never cancels (same method as the Detune voicing). */
         if (sw > 1.0)
            sw = 1.0;
         const int a = c->sh_act, b = a ^ 1;
         const double lo_edge = 2.0, hi_edge = c->sh_win;
         float sh[2];
         if (c->sh_xf < c->sh_xlen) {
            double t = smoothstep((double)(c->sh_xf + 1) / (double)c->sh_xlen);
            for (int ch = 0; ch < 2; ++ch)
               sh[ch] = (float)(1.0 - t) * ring_tap(c->sh_buf[ch], c->sh_mask, c->sh_w, c->sh_d[b]) +
                        (float)t * ring_tap(c->sh_buf[ch], c->sh_mask, c->sh_w, c->sh_d[a]);
            ++c->sh_xf;
         } else {
            for (int ch = 0; ch < 2; ++ch)
               sh[ch] = ring_tap(c->sh_buf[ch], c->sh_mask, c->sh_w, c->sh_d[a]);
            const double travel = fabs(c->sh_slope) * (double)c->sh_xlen;
            const int search = (int)(0.5 * (hi_edge - lo_edge));
            if (c->sh_slope < 0.0 && c->sh_d[a] - travel <= lo_edge + 1.0) {
               int hi = (int)(hi_edge - c->sh_d[a]), lo = hi - search;
               c->sh_d[b] = find_splice_ring(c->sh_buf[0], c->sh_buf[1], c->sh_mask, c->sh_w,
                                             c->sh_d[a], lo, hi, 1);
               c->sh_act = b;
               c->sh_xf = 0;
            } else if (c->sh_slope > 0.0 && c->sh_d[a] + travel >= hi_edge) {
               int hi = (int)(c->sh_d[a] - lo_edge - travel - 1.0), lo = hi - search;
               if (lo < 1)
                  lo = 1;
               c->sh_d[b] = find_splice_ring(c->sh_buf[0], c->sh_buf[1], c->sh_mask, c->sh_w,
                                             c->sh_d[a], lo, hi, -1);
               c->sh_act = b;
               c->sh_xf = 0;
            }
         }
         for (int ch = 0; ch < 2; ++ch)
            fy[ch] += sw * ((double)sh[ch] - fy[ch]);
         c->sh_d[0] += c->sh_slope;
         c->sh_d[1] += c->sh_slope;
         /* keep both taps inside the ring whatever the shift does */
         for (int t = 0; t < 2; ++t) {
            if (c->sh_d[t] < 2.0)
               c->sh_d[t] = 2.0;
            if (c->sh_d[t] > (double)c->sh_mask - 4.0)
               c->sh_d[t] = (double)c->sh_mask - 4.0;
         }
      }
      if (sh_on)
         c->sh_w = (c->sh_w + 1) & c->sh_mask;

      /* Wet input chain: pre-emphasis -> compressor -> BBD clip, then into the line. */
      float wr_in[2] = {xl, xr};
      const double ek = 5.0 * emph; /* shelf boost: 1 + ek (+15.6 dB at Emphasis 100 %) */
      /* Stage state only runs while the stage is engaged or about to be (its target is
       * non-zero); it starts from the current signal so switching on is seamless. */
      const int emph_on = emph > 0.0 || c->sm[P_EMPHASIS].target > 0.0;
      const int comp_on = comp > 0.0 || c->sm[P_COMPANDER].target > 0.0;
      if (emph_on && !c->emph_warm) {
         c->pre_s[0] = xl;
         c->pre_s[1] = xr;
         c->de_s[0] = c->de_s[1] = 0.0; /* set from the wet below on this sample */
      }
      if (comp_on && !c->comp_warm) {
         c->env_c[0] = c->env_c[1] = c->env_e[0] = c->env_e[1] = COMP_REF;
         c->gc[0] = c->gc[1] = c->ge[0] = c->ge[1] = 1.0;
         c->gc_step[0] = c->gc_step[1] = c->ge_step[0] = c->ge_step[1] = 0.0;
      }
      for (int ch = 0; ch < 2; ++ch) {
         double x = wr_in[ch];
         if (emph_on) {
            double v = (x - c->pre_s[ch]) * c->emph_G;
            double lp = v + c->pre_s[ch];
            c->pre_s[ch] = lp + v;
            x = x + ek * (x - lp);
         }
         if (comp_on) {
            c->env_c[ch] += (fabs(x) - c->env_c[ch]) * c->env_coef;
            x *= c->gc[ch];
         }
         if (emph > 0.0) {
            /* clip strength grows from zero, so a tiny setting is near-transparent */
            float sat = (float)(2.5 * emph);
            x = (double)(fast_tanhf((float)x * sat) / sat);
         }
         wr_in[ch] = (float)x;
         c->line[ch][c->w] = wr_in[ch] + (float)(fb * fast_tanh(fy[ch]));
      }

      float wl, wrr;
      run_engines(c, &fp, &wl, &wrr);

      /* Wet output chain: expander -> de-emphasis -> Low Cut -> Lush. */
      float wo[2] = {wl, wrr};
      for (int ch = 0; ch < 2; ++ch) {
         double y = wo[ch];
         if (comp_on) {
            c->env_e[ch] += (fabs(y) - c->env_e[ch]) * c->env_coef;
            y *= c->ge[ch];
            c->gc[ch] += c->gc_step[ch];
            c->ge[ch] += c->ge_step[ch];
         }
         if (emph_on) {
            /* exact inverse of the pre-emphasis shelf (zero-delay solve) */
            double G = c->emph_G;
            if (!c->emph_warm)
               c->de_s[ch] = y;
            y = (y + ek * (1.0 - G) * c->de_s[ch]) / (1.0 + ek - ek * G);
            double v = (y - c->de_s[ch]) * G;
            c->de_s[ch] = 2.0 * v + c->de_s[ch];
         }
         if (lowcut > OFF_HZ) {
            if (!c->lowcut_warm)
               memset(&c->lowcut[ch], 0, sizeof c->lowcut[ch]);
            y = svf_hp(&c->lowcut[ch], (float)y, c->lowcut_g, 1.41421356f);
         }
         wo[ch] = (float)y;
      }
      c->emph_warm = emph_on;
      c->comp_warm = comp_on;
      c->lowcut_warm = lowcut > OFF_HZ;
      if (lush > 0.0 && !c->lush_warm) {
         /* start the diffuser from silence, not from audio left from its last use */
         uint32_t tot = 0;
         for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < N_ALLPASS; ++i) {
               tot += c->ap_len[ch][i];
               c->ap_idx[ch][i] = 0;
            }
         memset(c->ap_buf, 0, sizeof(float) * tot);
         c->lush_in = 0.0f;
      }
      c->lush_warm = lush > 0.0;
      if (lush > 0.0) {
         const float g = (float)(0.5 + 0.2 * lush);
         /* fade the diffuser input in, so starting its delay lines on a playing
          * signal doesn't produce an onset step at each stage's delay */
         float fin = c->lush_in;
         if (fin < 1.0f) {
            fin += c->lush_in_step;
            if (fin > 1.0f)
               fin = 1.0f;
            c->lush_in = fin;
         }
         for (int ch = 0; ch < 2; ++ch) {
            float x = wo[ch] * fin;
            for (int i = 0; i < N_ALLPASS; ++i) {
               float *b = c->ap_buf + c->ap_off[ch][i];
               uint32_t k = c->ap_idx[ch][i];
               float dv = b[k];
               float yv = dv - g * x;
               b[k] = x + g * yv;
               if (++k >= c->ap_len[ch][i])
                  k = 0;
               c->ap_idx[ch][i] = k;
               x = yv;
            }
            wo[ch] += (float)lush * (x - wo[ch]);
         }
      }
      wl = wo[0];
      wrr = wo[1];

      double ol_ = (dry * xl + wet * wl) * gain;
      double or_ = (dry * xr + wet * wrr) * gain;
      if (monob > OFF_HZ) {
         /* Mid untouched; side high-passed (Linkwitz-Riley 4th order), so the lows
          * below the crossover end up mono and mono input passes unchanged. */
         if (!c->monob_warm)
            memset(c->monob, 0, sizeof c->monob);
         double mid = 0.5 * (ol_ + or_);
         float side = (float)(0.5 * (ol_ - or_));
         side = svf_hp(&c->monob[0], side, c->monob_g, 1.41421356f);
         side = svf_hp(&c->monob[1], side, c->monob_g, 1.41421356f);
         ol_ = mid + side;
         or_ = mid - side;
      }
      c->monob_warm = monob > OFF_HZ;
      if (outL)
         outL[n] = (float)ol_;
      if (outR)
         outR[n] = (float)or_;
      if (!(c->emph_warm | c->comp_warm | c->lowcut_warm | c->monob_warm | c->lush_warm | c->sh_warm) &&
          drive == 0.0 && c->drv_dcx[0] == 0.0 && c->drv_dcx[1] == 0.0 && v11_settled_off(c))
         c->v11_active = 0; /* back to the v1.0 path from the next sample */

      c->w = (c->w + 1) & c->line_mask;
      ++c->sample_count;
   }
   /* Events stamped past the end of the block still apply. */
   while (ei < nev) {
      handle_event(c, proc->in_events->get(proc->in_events, ei));
      ++ei;
   }

#ifdef CL_HAVE_SSE
   _mm_setcsr(saved_csr);
#endif
   return CLAP_PROCESS_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Extension: audio ports                                              */
/* ------------------------------------------------------------------ */

static uint32_t ap_count(const clap_plugin_t *p, bool is_input) {
   (void)p;
   (void)is_input;
   return 1;
}

static bool ap_get(const clap_plugin_t *p, uint32_t index, bool is_input,
                   clap_audio_port_info_t *info) {
   (void)p;
   if (index != 0)
      return false;
   memset(info, 0, sizeof *info);
   info->id = is_input ? 0 : 1;
   snprintf(info->name, sizeof info->name, "%s", is_input ? "Main In" : "Main Out");
   info->flags = CLAP_AUDIO_PORT_IS_MAIN;
   info->channel_count = 2;
   info->port_type = CLAP_PORT_STEREO;
   info->in_place_pair = is_input ? 1 : 0;
   return true;
}

static const clap_plugin_audio_ports_t s_audio_ports = {ap_count, ap_get};

/* ------------------------------------------------------------------ */
/* Extension: params                                                   */
/* ------------------------------------------------------------------ */

static uint32_t pr_count(const clap_plugin_t *p) {
   (void)p;
   return P_COUNT;
}

static bool pr_get_info(const clap_plugin_t *p, uint32_t index, clap_param_info_t *info) {
   (void)p;
   if (index >= P_COUNT)
      return false;
   const param_desc_t *d = &k_params[k_order[index]];
   memset(info, 0, sizeof *info);
   info->id = d->id;
   info->flags = d->flags;
   info->cookie = NULL;
   snprintf(info->name, sizeof info->name, "%s", d->name);
   snprintf(info->module, sizeof info->module, "%s", d->module);
   info->min_value = d->min;
   info->max_value = d->max;
   info->default_value = d->def;
   return true;
}

static bool pr_get_value(const clap_plugin_t *p, clap_id id, double *out) {
   chorus_t *c = (chorus_t *)p->plugin_data;
   if (id >= P_COUNT)
      return false;
   *out = u2d(atomic_load_explicit(&c->shared_value[id], memory_order_relaxed));
   return true;
}

static bool pr_value_to_text(const clap_plugin_t *p, clap_id id, double v, char *buf,
                             uint32_t size) {
   (void)p;
   if (id >= P_COUNT || size == 0 || !isfinite(v))
      return false;
   switch (id) {
   case P_VOICING: {
      long i = lrint(clampd(v, 0.0, V_COUNT - 1));
      snprintf(buf, size, "%s", k_voicing_names[i]);
      break;
   }
   case P_RATE:
      snprintf(buf, size, "%.3f Hz", v);
      break;
   case P_LEVEL:
      snprintf(buf, size, "%.2f dB", v);
      break;
   case P_LOWCUT:
   case P_MONOBASS:
      if (v <= OFF_HZ)
         snprintf(buf, size, "Off");
      else
         snprintf(buf, size, "%.1f Hz", v);
      break;
   case P_SHIFT:
      snprintf(buf, size, "%+.2f st", v);
      break;
   default:
      snprintf(buf, size, "%.2f %%", v);
      break;
   }
   return true;
}

static int ieq(const char *a, const char *b) {
   while (*a && *b) {
      if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
         return 0;
      ++a;
      ++b;
   }
   return *a == 0 && *b == 0;
}

static bool pr_text_to_value(const clap_plugin_t *p, clap_id id, const char *text, double *out) {
   (void)p;
   if (id >= P_COUNT || !text)
      return false;
   while (isspace((unsigned char)*text))
      ++text;
   if (id == P_VOICING) {
      for (int i = 0; i < V_COUNT; ++i)
         if (ieq(text, k_voicing_names[i])) {
            *out = (double)i;
            return true;
         }
   }
   if ((id == P_LOWCUT || id == P_MONOBASS) && ieq(text, "Off")) {
      *out = 20.0;
      return true;
   }
   char *end = NULL;
   double v = strtod(text, &end);
   if (end == text || !isfinite(v))
      return false;
   while (isspace((unsigned char)*end))
      ++end;
   /* Allow only the expected unit suffix (or none). */
   if (*end) {
      const char *unit = (id == P_RATE || id == P_LOWCUT || id == P_MONOBASS) ? "Hz"
                         : id == P_LEVEL                                      ? "dB"
                         : id == P_SHIFT                                      ? "st"
                         : id == P_VOICING                                    ? ""
                                                                              : "%";
      if (!ieq(end, unit))
         return false;
   }
   const param_desc_t *d = &k_params[id];
   if (v < d->min || v > d->max)
      return false;
   if (d->flags & CLAP_PARAM_IS_STEPPED)
      v = (double)lrint(v);
   *out = v;
   return true;
}

static void pr_flush(const clap_plugin_t *p, const clap_input_events_t *in,
                     const clap_output_events_t *out) {
   (void)out;
   chorus_t *c = (chorus_t *)p->plugin_data;
   pull_main_values(c);
   uint32_t n = in ? in->size(in) : 0;
   for (uint32_t i = 0; i < n; ++i)
      handle_event(c, in->get(in, i));
}

static const clap_plugin_params_t s_params = {pr_count, pr_get_info, pr_get_value,
                                              pr_value_to_text, pr_text_to_value, pr_flush};

/* ------------------------------------------------------------------ */
/* Extension: tail                                                     */
/* ------------------------------------------------------------------ */

static uint32_t tl_get(const clap_plugin_t *p) {
   chorus_t *c = (chorus_t *)p->plugin_data;
   double sr = c->sr > 0.0 ? c->sr : 48000.0;
   double fb = fabs(u2d(atomic_load_explicit(&c->shared_value[P_FEEDBACK], memory_order_relaxed))) / 100.0;
   double time = u2d(atomic_load_explicit(&c->shared_value[P_TIME], memory_order_relaxed)) / 100.0;
   int vo = (int)lrint(clampd(u2d(atomic_load_explicit(&c->shared_value[P_VOICING], memory_order_relaxed)), 0.0, V_COUNT - 1));
   /* Longest feedback loop of the selected voicing (upper bound, Depth = 100 %). */
   double loop_s = (k_shape[vo].base_ms + k_shape[vo].span_ms) * time * 0.001;
   double shift = u2d(atomic_load_explicit(&c->shared_value[P_SHIFT], memory_order_relaxed));
   double lush = u2d(atomic_load_explicit(&c->shared_value[P_LUSH], memory_order_relaxed));
   if (shift != 0.0)
      loop_s += (SHIFT_WIN_MS + 1.0) * 0.001; /* shifter adds up to one window per pass */
   double passes = 1.0;
   if (fb > 0.001)
      passes += 3.0 / -log10(fb > 0.9 ? 0.9 : fb); /* loop gain <= fb, to -60 dB */
   double tail = (loop_s * passes + 2.0 * XFADE_MS * 0.001) * sr;
   if (lush > 0.0)
      tail += 0.25 * sr; /* allpass ring-out */
   return (uint32_t)ceil(tail);
}

static const clap_plugin_tail_t s_tail = {tl_get};

/* ------------------------------------------------------------------ */
/* Extension: state                                                    */
/*   "RCLB" | u32 version | u32 count | count x u64 (double bits) | u32 fnv1a */
/*   all little-endian. v2 = 16 params (current); v1 = 9 params (Chorus Lab  */
/*   1.0), loaded with the v1.1 controls at their defaults (off).        */
/* ------------------------------------------------------------------ */

#define STATE_MAGIC 0x424C4352u /* "RCLB" little-endian */
#define STATE_VERSION 2u
#define STATE_V1_COUNT 9
#define STATE_BYTES (4 + 4 + 4 + 8 * P_COUNT + 4)

static uint32_t fnv1a(const uint8_t *d, size_t n) {
   uint32_t h = 2166136261u;
   for (size_t i = 0; i < n; ++i) {
      h ^= d[i];
      h *= 16777619u;
   }
   return h;
}

static void put_u32(uint8_t *b, uint32_t v) {
   for (int i = 0; i < 4; ++i)
      b[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get_u32(const uint8_t *b) {
   uint32_t v = 0;
   for (int i = 0; i < 4; ++i)
      v |= (uint32_t)b[i] << (8 * i);
   return v;
}

static bool st_save(const clap_plugin_t *p, const clap_ostream_t *s) {
   chorus_t *c = (chorus_t *)p->plugin_data;
   uint8_t buf[STATE_BYTES];
   put_u32(buf, STATE_MAGIC);
   put_u32(buf + 4, STATE_VERSION);
   put_u32(buf + 8, P_COUNT);
   for (int i = 0; i < P_COUNT; ++i) {
      uint64_t u = atomic_load_explicit(&c->shared_value[i], memory_order_relaxed);
      for (int k = 0; k < 8; ++k)
         buf[12 + 8 * i + k] = (uint8_t)(u >> (8 * k));
   }
   put_u32(buf + STATE_BYTES - 4, fnv1a(buf, STATE_BYTES - 4));
   size_t done = 0;
   while (done < sizeof buf) {
      int64_t r = s->write(s, buf + done, sizeof buf - done);
      if (r <= 0)
         return false;
      done += (size_t)r;
   }
   return true;
}

static bool read_exact(const clap_istream_t *s, uint8_t *buf, size_t n) {
   size_t done = 0;
   while (done < n) {
      int64_t r = s->read(s, buf + done, n - done);
      if (r <= 0)
         return false; /* truncated or error */
      done += (size_t)r;
   }
   return true;
}

static bool st_load(const clap_plugin_t *p, const clap_istream_t *s) {
   chorus_t *c = (chorus_t *)p->plugin_data;
   uint8_t buf[STATE_BYTES];
   if (!read_exact(s, buf, 12))
      return false;
   uint32_t version = get_u32(buf + 4), count = get_u32(buf + 8);
   if (get_u32(buf) != STATE_MAGIC)
      return false;
   if (!((version == STATE_VERSION && count == P_COUNT) ||
         (version == 1u && count == STATE_V1_COUNT)))
      return false;
   size_t total = 12 + 8 * (size_t)count + 4;
   if (!read_exact(s, buf + 12, total - 12))
      return false;
   if (get_u32(buf + total - 4) != fnv1a(buf, total - 4))
      return false;
   double vals[P_COUNT];
   for (int i = 0; i < P_COUNT; ++i)
      vals[i] = k_params[i].def; /* params missing from older versions */
   for (uint32_t i = 0; i < count; ++i) {
      uint64_t u = 0;
      for (int k = 0; k < 8; ++k)
         u |= (uint64_t)buf[12 + 8 * i + k] << (8 * k);
      double v = u2d(u);
      if (!isfinite(v) || v < k_params[i].min || v > k_params[i].max)
         return false;
      if ((k_params[i].flags & CLAP_PARAM_IS_STEPPED) && v != floor(v))
         return false;
      vals[i] = v;
   }
   for (int i = 0; i < P_COUNT; ++i)
      atomic_store_explicit(&c->shared_value[i], d2u(vals[i]), memory_order_relaxed);
   atomic_store_explicit(&c->main_dirty, 1, memory_order_release);
   if (c->host_params)
      c->host_params->rescan(c->host, CLAP_PARAM_RESCAN_VALUES);
   return true;
}

static const clap_plugin_state_t s_state = {st_save, st_load};

/* ------------------------------------------------------------------ */
/* Plugin boilerplate                                                  */
/* ------------------------------------------------------------------ */

static const void *cl_get_extension(const clap_plugin_t *p, const char *id) {
   (void)p;
   if (!strcmp(id, CLAP_EXT_AUDIO_PORTS))
      return &s_audio_ports;
   if (!strcmp(id, CLAP_EXT_PARAMS))
      return &s_params;
   if (!strcmp(id, CLAP_EXT_STATE))
      return &s_state;
   if (!strcmp(id, CLAP_EXT_TAIL))
      return &s_tail;
   return NULL;
}

static void cl_on_main_thread(const clap_plugin_t *p) { (void)p; }

static const char *const s_features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                         CLAP_PLUGIN_FEATURE_CHORUS,
                                         CLAP_PLUGIN_FEATURE_STEREO, NULL};

static const clap_plugin_descriptor_t s_desc = {
   .clap_version = CLAP_VERSION_INIT,
   .id = "com.veepedaldude.rig01.chorus-lab",
   .name = "Chorus Lab",
   .vendor = "KC",
   .url = "https://github.com/VeePedalDude/DIY-multi-effect-",
   .manual_url = "",
   .support_url = "",
   .version = "1.1.0",
   .description = "Stereo chorus with eight selectable voicings",
   .features = s_features,
};

static const clap_plugin_t *create_plugin(const clap_host_t *host) {
   chorus_t *c = (chorus_t *)calloc(1, sizeof *c);
   if (!c)
      return NULL;
   c->host = host;
   c->plugin.desc = &s_desc;
   c->plugin.plugin_data = c;
   c->plugin.init = cl_init;
   c->plugin.destroy = cl_destroy;
   c->plugin.activate = cl_activate;
   c->plugin.deactivate = cl_deactivate;
   c->plugin.start_processing = cl_start_processing;
   c->plugin.stop_processing = cl_stop_processing;
   c->plugin.reset = cl_reset;
   c->plugin.process = cl_process;
   c->plugin.get_extension = cl_get_extension;
   c->plugin.on_main_thread = cl_on_main_thread;
   for (int i = 0; i < P_COUNT; ++i) {
      c->value[i] = k_params[i].def;
      atomic_init(&c->shared_value[i], d2u(k_params[i].def));
   }
   atomic_init(&c->main_dirty, 0);
   c->sr = 48000.0;
   return &c->plugin;
}

static uint32_t f_count(const clap_plugin_factory_t *f) {
   (void)f;
   return 1;
}
static const clap_plugin_descriptor_t *f_desc(const clap_plugin_factory_t *f, uint32_t i) {
   (void)f;
   return i == 0 ? &s_desc : NULL;
}
static const clap_plugin_t *f_create(const clap_plugin_factory_t *f, const clap_host_t *host,
                                     const char *id) {
   (void)f;
   if (!host || !id || !clap_version_is_compatible(host->clap_version) || strcmp(id, s_desc.id))
      return NULL;
   return create_plugin(host);
}

static const clap_plugin_factory_t s_factory = {f_count, f_desc, f_create};

static bool e_init(const char *path) {
   (void)path;
   return true;
}
static void e_deinit(void) {}
static const void *e_factory(const char *id) {
   return !strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &s_factory : NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
   .clap_version = CLAP_VERSION_INIT,
   .init = e_init,
   .deinit = e_deinit,
   .get_factory = e_factory,
};
