/*
 * Chorus Lab test host
 * Copyright (C) 2026 KC
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Judge host for the Chorus Lab eval (READ-ONLY once locked).
 * Minimal offline CLAP host for testing one effect.
 *   host PLUGIN render SR IN.f32 OUT.f32 BLOCKSPEC [EVENTS.txt] [PARAMS]
 *       IN/OUT: interleaved stereo float32. BLOCKSPEC: "N" fixed or "rN:SEED" random 1..N.
 *       EVENTS: lines "sample V|M id value" (V = knob value, M = modulation offset).
 *       PARAMS: "id=val,id=val" applied with flush before activation.
 *   host PLUGIN state      (state save/load/garbage tests)
 *   host PLUGIN threads    (main thread hammers params/state while audio runs; for TSan)
 *   host PLUGIN bench SR SECONDS PARAMS   (CPU time of process())
 */
#define _POSIX_C_SOURCE 200809L
#include <clap/clap.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_rescans = 0;
static void h_rescan(const clap_host_t *h, clap_param_rescan_flags f) {
   (void)h;
   (void)f;
   ++g_rescans;
}
static void h_clear(const clap_host_t *h, clap_id id, clap_param_clear_flags f) {
   (void)h;
   (void)id;
   (void)f;
}
static void h_req_flush(const clap_host_t *h) { (void)h; }
static const clap_host_params_t s_hparams = {h_rescan, h_clear, h_req_flush};

static const void *h_get_ext(const clap_host_t *h, const char *id) {
   (void)h;
   if (!strcmp(id, CLAP_EXT_PARAMS))
      return &s_hparams;
   return NULL;
}
static void h_noop(const clap_host_t *h) { (void)h; }

static clap_host_t g_host = {CLAP_VERSION_INIT, NULL, "rig01-test", "KC", "", "1",
                             h_get_ext, h_noop, h_noop, h_noop};

/* ---- event lists ---- */
typedef union {
   clap_event_header_t h;
   clap_event_param_value_t v;
   clap_event_param_mod_t m;
} ev_t;

typedef struct {
   ev_t *ev;
   uint32_t n;
} evlist_t;
static uint32_t el_size(const clap_input_events_t *l) { return ((const evlist_t *)l->ctx)->n; }
static const clap_event_header_t *el_get(const clap_input_events_t *l, uint32_t i) {
   return &((const evlist_t *)l->ctx)->ev[i].h;
}
static bool ol_push(const clap_output_events_t *l, const clap_event_header_t *e) {
   (void)l;
   (void)e;
   return true;
}

static ev_t make_ev(uint32_t time, char kind, uint32_t id, double val) {
   ev_t e;
   memset(&e, 0, sizeof e);
   if (kind == 'V') {
      e.v.header.size = sizeof e.v;
      e.v.header.type = CLAP_EVENT_PARAM_VALUE;
      e.v.param_id = id;
      e.v.value = val;
      e.v.note_id = -1;
      e.v.port_index = e.v.channel = e.v.key = -1;
   } else {
      e.m.header.size = sizeof e.m;
      e.m.header.type = CLAP_EVENT_PARAM_MOD;
      e.m.param_id = id;
      e.m.amount = val;
      e.m.note_id = -1;
      e.m.port_index = e.m.channel = e.m.key = -1;
   }
   e.h.time = time;
   e.h.space_id = CLAP_CORE_EVENT_SPACE_ID;
   return e;
}

typedef struct {
   void *lib;
   const clap_plugin_entry_t *entry;
   const clap_plugin_t *p;
   const clap_plugin_params_t *params;
   const clap_plugin_state_t *state;
} inst_t;

static const clap_plugin_entry_t *g_entry;
static void *g_lib;

static int load_lib(const char *path) {
   g_lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   if (!g_lib) {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 0;
   }
   g_entry = (const clap_plugin_entry_t *)dlsym(g_lib, "clap_entry");
   if (!g_entry || !g_entry->init(path))
      return 0;
   return 1;
}

static int new_inst(inst_t *in) {
   const clap_plugin_factory_t *f =
      (const clap_plugin_factory_t *)g_entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
   const clap_plugin_descriptor_t *d = f->get_plugin_descriptor(f, 0);
   in->p = f->create_plugin(f, &g_host, d->id);
   if (!in->p || !in->p->init(in->p))
      return 0;
   in->params = (const clap_plugin_params_t *)in->p->get_extension(in->p, CLAP_EXT_PARAMS);
   in->state = (const clap_plugin_state_t *)in->p->get_extension(in->p, CLAP_EXT_STATE);
   return 1;
}

static void apply_params(inst_t *in, const char *spec) {
   if (!spec || !*spec)
      return;
   ev_t evs[64];
   uint32_t n = 0;
   char buf[1024];
   snprintf(buf, sizeof buf, "%s", spec);
   for (char *tok = strtok(buf, ","); tok && n < 64; tok = strtok(NULL, ",")) {
      unsigned id;
      double v;
      if (sscanf(tok, "%u=%lf", &id, &v) == 2)
         evs[n++] = make_ev(0, 'V', id, v);
   }
   evlist_t el = {evs, n};
   clap_input_events_t in_ev = {&el, el_size, el_get};
   clap_output_events_t out_ev = {NULL, ol_push};
   in->params->flush(in->p, &in_ev, &out_ev);
}

/* Render interleaved stereo through the plugin with a block-size schedule. */
static void render(inst_t *in, const float *x, float *y, size_t frames, const char *blockspec,
                   ev_t *events, uint32_t nevents) {
   uint32_t fixed = 0, rmax = 0;
   unsigned seed = 1;
   if (blockspec[0] == 'r')
      sscanf(blockspec + 1, "%u:%u", &rmax, &seed);
   else
      fixed = (uint32_t)atoi(blockspec);
   float *bl = malloc(sizeof(float) * 8192 * 4);
   float *ibuf[2] = {bl, bl + 8192}, *obuf[2] = {bl + 16384, bl + 24576};
   ev_t *blkev = malloc(sizeof(ev_t) * (nevents + 1));
   size_t pos = 0;
   uint32_t ei = 0;
   uint64_t rng = seed;
   while (pos < frames) {
      uint32_t n = fixed;
      if (rmax) {
         rng = rng * 6364136223846793005ull + 1442695040888963407ull;
         n = 1 + (uint32_t)((rng >> 33) % rmax);
      }
      if (n > 8192)
         n = 8192;
      if (pos + n > frames)
         n = (uint32_t)(frames - pos);
      for (uint32_t i = 0; i < n; ++i) {
         ibuf[0][i] = x[2 * (pos + i)];
         ibuf[1][i] = x[2 * (pos + i) + 1];
      }
      uint32_t ne = 0;
      while (ei < nevents && events[ei].h.time < pos + n) {
         blkev[ne] = events[ei];
         blkev[ne].h.time = (uint32_t)(events[ei].h.time - pos);
         ++ne;
         ++ei;
      }
      evlist_t el = {blkev, ne};
      clap_input_events_t in_ev = {&el, el_size, el_get};
      clap_output_events_t out_ev = {NULL, ol_push};
      clap_audio_buffer_t ib = {ibuf, NULL, 2, 0, 0}, ob = {obuf, NULL, 2, 0, 0};
      clap_process_t pr;
      memset(&pr, 0, sizeof pr);
      pr.steady_time = (int64_t)pos;
      pr.frames_count = n;
      pr.audio_inputs = &ib;
      pr.audio_outputs = &ob;
      pr.audio_inputs_count = 1;
      pr.audio_outputs_count = 1;
      pr.in_events = &in_ev;
      pr.out_events = &out_ev;
      in->p->process(in->p, &pr);
      for (uint32_t i = 0; i < n; ++i) {
         y[2 * (pos + i)] = obuf[0][i];
         y[2 * (pos + i) + 1] = obuf[1][i];
      }
      pos += n;
   }
   free(blkev);
   free(bl);
}

static float *read_f32(const char *path, size_t *frames) {
   FILE *f = fopen(path, "rb");
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   long sz = ftell(f);
   fseek(f, 0, SEEK_SET);
   float *d = malloc((size_t)sz);
   if (fread(d, 1, (size_t)sz, f) != (size_t)sz) {
      fclose(f);
      free(d);
      return NULL;
   }
   fclose(f);
   *frames = (size_t)sz / 8;
   return d;
}

static ev_t *read_events(const char *path, uint32_t *n) {
   *n = 0;
   if (!path || !strcmp(path, "-"))
      return NULL;
   FILE *f = fopen(path, "r");
   if (!f)
      return NULL;
   size_t cap = 1024;
   ev_t *e = malloc(sizeof(ev_t) * cap);
   unsigned long t;
   char k;
   unsigned id;
   double v;
   while (fscanf(f, "%lu %c %u %lf", &t, &k, &id, &v) == 4) {
      if (*n == cap) {
         cap *= 2;
         e = realloc(e, sizeof(ev_t) * cap);
      }
      e[(*n)++] = make_ev((uint32_t)t, k, id, v);
   }
   fclose(f);
   return e;
}

/* ---- streams ---- */
typedef struct {
   uint8_t *d;
   size_t n, cap, pos, chunk;
} mstream_t;

static int64_t ms_write(const clap_ostream_t *s, const void *b, uint64_t n) {
   mstream_t *m = s->ctx;
   if (m->chunk && n > m->chunk)
      n = m->chunk;
   if (m->n + n > m->cap) {
      m->cap = (m->n + n) * 2;
      m->d = realloc(m->d, m->cap);
   }
   memcpy(m->d + m->n, b, n);
   m->n += n;
   return (int64_t)n;
}
static int64_t ms_read(const clap_istream_t *s, void *b, uint64_t n) {
   mstream_t *m = s->ctx;
   if (m->chunk && n > m->chunk)
      n = m->chunk;
   if (n > m->n - m->pos)
      n = m->n - m->pos;
   memcpy(b, m->d + m->pos, n);
   m->pos += n;
   return (int64_t)n;
}

static int save(inst_t *in, mstream_t *m, size_t chunk) {
   memset(m, 0, sizeof *m);
   m->chunk = chunk;
   clap_ostream_t o = {m, ms_write};
   return in->state->save(in->p, &o);
}
static int load(inst_t *in, const uint8_t *d, size_t n, size_t chunk) {
   mstream_t m = {(uint8_t *)d, n, n, 0, chunk};
   clap_istream_t i = {&m, ms_read};
   return in->state->load(in->p, &i);
}

static int values_equal(inst_t *a, inst_t *b) {
   uint32_t n = a->params->count(a->p);
   for (uint32_t i = 0; i < n; ++i) {
      double va, vb;
      a->params->get_value(a->p, i, &va);
      b->params->get_value(b->p, i, &vb);
      if (va != vb)
         return 0;
   }
   return 1;
}

static uint32_t fnv(const uint8_t *d, size_t n) {
   uint32_t h = 2166136261u;
   for (size_t i = 0; i < n; ++i) {
      h ^= d[i];
      h *= 16777619u;
   }
   return h;
}

static int cmd_state(void) {
   int fails = 0;
   inst_t a, b;
   new_inst(&a);
   new_inst(&b);
   apply_params(&a, "0=5,1=3.21,2=77,3=64,4=40,5=-33,6=25,7=150,8=-6.5");
   mstream_t m;
   if (!save(&a, &m, 0)) {
      printf("FAIL save\n");
      return 1;
   }
   mstream_t m2;
   save(&a, &m2, 3);
   printf("state bytes: %zu; chunked-write identical: %s\n", m.n,
          (m.n == m2.n && !memcmp(m.d, m2.d, m.n)) ? "yes" : "NO");
   fails += !(m.n == m2.n && !memcmp(m.d, m2.d, m.n));
   /* deterministic: save twice */
   mstream_t m3;
   save(&a, &m3, 0);
   int det = m3.n == m.n && !memcmp(m3.d, m.d, m.n);
   printf("deterministic save: %s\n", det ? "yes" : "NO");
   fails += !det;
   static const size_t chunks[] = {1, 2, 3, 4, 5, 6, 7, 8, 17};
   for (size_t ci = 0; ci < sizeof chunks / sizeof chunks[0]; ++ci) {
      size_t ch = chunks[ci];
      inst_t c;
      new_inst(&c);
      int r0 = g_rescans;
      int ok = load(&c, m.d, m.n, ch) && values_equal(&a, &c) && g_rescans == r0 + 1;
      if (!ok) {
         printf("FAIL chunked load chunk=%zu\n", ch);
         ++fails;
      }
      c.p->destroy(c.p);
   }
   printf("chunked load 1..8 and 17 bytes per read + rescan(VALUES) called: %s\n", fails ? "FAIL" : "ok");
   /* Garbage: every truncation, every single-byte flip, random blobs */
   int accepted = 0, tried = 0;
   for (size_t len = 0; len < m.n; ++len) {
      ++tried;
      accepted += load(&b, m.d, len, 0);
   }
   for (size_t i = 0; i < m.n; ++i)
      for (int bit = 0; bit < 8; ++bit) {
         uint8_t *d = malloc(m.n);
         memcpy(d, m.d, m.n);
         d[i] ^= (uint8_t)(1u << bit);
         ++tried;
         accepted += load(&b, d, m.n, 0);
         free(d);
      }
   uint64_t rng = 99;
   for (int t = 0; t < 20000; ++t) {
      size_t len = 1 + (size_t)(t % 200);
      uint8_t *d = malloc(len);
      for (size_t i = 0; i < len; ++i) {
         rng = rng * 6364136223846793005ull + 1442695040888963407ull;
         d[i] = (uint8_t)(rng >> 56);
      }
      if (len >= 4 && (t & 1)) {
         d[0] = 'R';
         d[1] = 'C';
         d[2] = 'L';
         d[3] = 'B';
      }
      ++tried;
      accepted += load(&b, d, len, 0);
      free(d);
   }
   /* Valid checksum but bad contents: NaN, out-of-range, fractional enum, wrong version */
   struct {
      const char *name;
      int off;
      uint64_t val;
      int is_u32;
   } bad[] = {
      {"NaN rate", 12 + 8 * 1, 0x7ff8000000000000ull, 0},
      {"inf depth", 12 + 8 * 2, 0x7ff0000000000000ull, 0},
      {"mix 500", 12 + 8 * 3, 0x407f400000000000ull, 0},
      {"voicing 2.5", 12, 0x4004000000000000ull, 0},
      {"version 2", 4, 2, 1},
      {"count 8", 8, 8, 1},
   };
   for (size_t k = 0; k < sizeof bad / sizeof bad[0]; ++k) {
      uint8_t *d = malloc(m.n);
      memcpy(d, m.d, m.n);
      int bytes = bad[k].is_u32 ? 4 : 8;
      for (int i = 0; i < bytes; ++i)
         d[bad[k].off + i] = (uint8_t)(bad[k].val >> (8 * i));
      uint32_t h = fnv(d, m.n - 4);
      for (int i = 0; i < 4; ++i)
         d[m.n - 4 + i] = (uint8_t)(h >> (8 * i));
      ++tried;
      int acc = load(&b, d, m.n, 0);
      accepted += acc;
      if (acc)
         printf("FAIL accepted %s\n", bad[k].name);
      free(d);
   }
   /* b must still hold defaults */
   inst_t fresh;
   new_inst(&fresh);
   int untouched = values_equal(&b, &fresh);
   printf("garbage blobs tried: %d, accepted: %d; failed loads left values untouched: %s\n",
          tried, accepted, untouched ? "yes" : "NO");
   fails += accepted + !untouched;
   a.p->destroy(a.p);
   b.p->destroy(b.p);
   fresh.p->destroy(fresh.p);
   free(m.d);
   free(m2.d);
   free(m3.d);
   printf("STATE %s\n", fails ? "FAIL" : "PASS");
   return fails != 0;
}

/* ---- threads: main thread hammers params/state while audio thread processes ---- */
static atomic_int g_stop;
typedef struct {
   inst_t *in;
} thr_t;
static void *audio_thread(void *arg) {
   inst_t *in = ((thr_t *)arg)->in;
   size_t frames = 48000;
   float *x = calloc(frames * 2, sizeof(float));
   float *y = calloc(frames * 2, sizeof(float));
   for (size_t i = 0; i < frames; ++i)
      x[2 * i] = x[2 * i + 1] = (float)(0.5 * sin(i * 0.05));
   in->p->start_processing(in->p);
   ev_t ev[4];
   int it = 0;
   while (!atomic_load(&g_stop) && it < 40) {
      ev[0] = make_ev(10, 'M', 2, (it % 7) * 5.0);
      ev[1] = make_ev(20, 'V', 0, (double)(it % 8));
      render(in, x, y, frames, "r257:3", ev, 2);
      ++it;
   }
   in->p->stop_processing(in->p);
   free(x);
   free(y);
   return NULL;
}

static int cmd_threads(void) {
   inst_t a;
   new_inst(&a);
   a.p->activate(a.p, 48000, 1, 8192);
   thr_t t = {&a};
   pthread_t th;
   pthread_create(&th, NULL, audio_thread, &t);
   inst_t src;
   new_inst(&src);
   apply_params(&src, "0=3,2=90,5=50");
   mstream_t m;
   save(&src, &m, 0);
   for (int i = 0; i < 300; ++i) {
      double v;
      char txt[64];
      for (uint32_t id = 0; id < a.params->count(a.p); ++id) {
         a.params->get_value(a.p, id, &v);
         a.params->value_to_text(a.p, id, v, txt, sizeof txt);
      }
      mstream_t s;
      save(&a, &s, 0);
      free(s.d);
      load(&a, m.d, m.n, 0);
      const clap_plugin_tail_t *tl = a.p->get_extension(a.p, CLAP_EXT_TAIL);
      (void)tl->get(a.p);
   }
   atomic_store(&g_stop, 1);
   pthread_join(th, NULL);
   a.p->deactivate(a.p);
   a.p->destroy(a.p);
   src.p->destroy(src.p);
   free(m.d);
   printf("THREADS done\n");
   return 0;
}

static double now_s(void) {
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
   if (argc < 3 || !load_lib(argv[1]))
      return 2;
   const char *cmd = argv[2];
   if (!strcmp(cmd, "state"))
      return cmd_state();
   if (!strcmp(cmd, "threads"))
      return cmd_threads();
   if (!strcmp(cmd, "bench") && argc >= 8) {
      /* bench SR SECONDS BLOCK REPS PARAMS -> one CPU % (of one core) per rep */
      double sr = atof(argv[3]);
      double secs = atof(argv[4]);
      const char *block = argv[5];
      int reps = atoi(argv[6]);
      inst_t a;
      new_inst(&a);
      apply_params(&a, argv[7]);
      a.p->activate(a.p, sr, 1, 8192);
      a.p->start_processing(a.p);
      size_t frames = (size_t)(sr * secs);
      float *x = malloc(sizeof(float) * frames * 2), *y = malloc(sizeof(float) * frames * 2);
      uint64_t r = 5;
      double lp = 0.0;
      for (size_t i = 0; i < frames; ++i) {
         r = r * 6364136223846793005ull + 1442695040888963407ull;
         double nz = (double)(r >> 40) / 16777216.0 - 0.5;
         lp += 0.05 * (nz - lp);
         double env = 0.5 * fabs(sin((double)i * 3.14159265358979 / (sr * 0.7)));
         x[2 * i] = x[2 * i + 1] = (float)(lp * 4.0 * env);
      }
      render(&a, x, y, frames / 4, block, NULL, 0); /* warm-up */
      for (int rep = 0; rep < reps; ++rep) {
         double t0 = now_s();
         render(&a, x, y, frames, block, NULL, 0);
         double dt = now_s() - t0;
         printf("%.5f\n", 100.0 * dt / secs);
      }
      a.p->stop_processing(a.p);
      a.p->deactivate(a.p);
      a.p->destroy(a.p);
      free(x);
      free(y);
      return 0;
   }
   if (!strcmp(cmd, "render") && argc >= 7) {
      double sr = atof(argv[3]);
      size_t frames;
      float *x = read_f32(argv[4], &frames);
      if (!x)
         return 3;
      float *y = calloc(frames * 2, sizeof(float));
      uint32_t ne;
      ev_t *ev = read_events(argc > 7 ? argv[7] : NULL, &ne);
      inst_t a;
      new_inst(&a);
      apply_params(&a, argc > 8 ? argv[8] : NULL);
      if (!a.p->activate(a.p, sr, 1, 8192))
         return 4;
      a.p->start_processing(a.p);
      render(&a, x, y, frames, argv[6], ev, ne);
      a.p->stop_processing(a.p);
      {
         const clap_plugin_tail_t *tl = a.p->get_extension(a.p, CLAP_EXT_TAIL);
         fprintf(stderr, "tail=%u\n", tl->get(a.p));
      }
      /* After processing, knob values must equal the last V event (M never moves them). */
      FILE *vf = fopen(argv[5], "wb");
      fwrite(y, sizeof(float), frames * 2, vf);
      fclose(vf);
      for (uint32_t id = 0; id < a.params->count(a.p); ++id) {
         double v;
         a.params->get_value(a.p, id, &v);
         printf("%u=%.17g%s", id, v, id + 1 < a.params->count(a.p) ? "," : "\n");
      }
      a.p->deactivate(a.p);
      a.p->destroy(a.p);
      free(x);
      free(y);
      free(ev);
      g_entry->deinit();
      dlclose(g_lib);
      return 0;
   }
   fprintf(stderr, "bad command\n");
   return 2;
}
