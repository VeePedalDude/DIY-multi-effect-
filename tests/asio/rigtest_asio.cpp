// tests/asio/rigtest_asio.cpp - a stand-in ASIO driver for testing rig-host's ASIO path without an
// interface: a COM DLL (rigtest_asio.dll) that registers as "RIG Test ASIO" and behaves like a
// 2-in/2-out interface whose outputs are cabled straight back into its inputs, LOOP samples later.
// Built for Windows by build.sh; tests/asio/asio.sh registers it in Wine and runs rig-host on it.
//
// Like a real driver it runs its own thread, calls bufferSwitch(0/1) once per buffer at the sample
// rate's pace, and keeps samples in its own format. Environment (read when the driver starts):
//   RIGTEST_ASIO_LOOP    loopback delay in samples (default 437; at least the buffer size)
//   RIGTEST_ASIO_FORMAT  int32 (default, as Focusrite's driver) | int24 | int16 | float
//   RIGTEST_ASIO_RATE    sample rate (default 48000)
#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"

// {5A1F0D3E-6C2B-4E8A-9B71-2F4D8C3A9E10}
static const CLSID CLSID_RigTest = {0x5a1f0d3e, 0x6c2b, 0x4e8a, {0x9b, 0x71, 0x2f, 0x4d, 0x8c, 0x3a, 0x9e, 0x10}};
static std::atomic<long> g_objects{0};

static int envInt(const char* k, int d) { const char* v = getenv(k); return v && *v ? atoi(v) : d; }

class RigTestAsio : public IASIO {
  std::atomic<long> refs{1};
  ASIOCallbacks* cb = nullptr;
  ASIOSampleType type = ASIOSTInt32LSB; int bytes = 4;
  double rate = 48000; long frames = 0; int loop = 437;
  std::vector<unsigned char> buf[4][2];                      // in0 in1 out0 out1, two halves each
  std::vector<float> line[2]; long long pos = 0;             // the "cable": what was played, by sample time
  std::thread th; std::atomic<bool> run{false};
  char err[128] = "";

  float get(int c, int half, long i) const {
    const unsigned char* p = buf[c][half].data() + (size_t)i * bytes;
    switch (type) {
      case ASIOSTInt16LSB: { int16_t v; memcpy(&v, p, 2); return v / 32768.f; }
      case ASIOSTInt24LSB: { const int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24); return (float)(v / 2147483648.0); }
      case ASIOSTFloat32LSB: { float v; memcpy(&v, p, 4); return v; }
      default: { int32_t v; memcpy(&v, p, 4); return (float)(v / 2147483648.0); }
    }
  }
  void put(int c, int half, long i, float x) {
    unsigned char* p = buf[c][half].data() + (size_t)i * bytes; x = x > 1.f ? 1.f : x < -1.f ? -1.f : x;
    switch (type) {
      case ASIOSTInt16LSB: { const int16_t v = (int16_t)lrintf(x * 32767.f); memcpy(p, &v, 2); break; }
      case ASIOSTInt24LSB: { const int32_t v = (int32_t)lrint(x * 8388607.0); p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); break; }
      case ASIOSTFloat32LSB: memcpy(p, &x, 4); break;
      default: { const int32_t v = (int32_t)llrint(x * 2147483647.0); memcpy(p, &v, 4); }
    }
  }
  void thread() {
    LARGE_INTEGER f, t0, now; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    const double period = frames / rate; const size_t L = line[0].size();
    for (long long k = 0; run; k++) {
      const int half = (int)(k & 1);
      for (int c = 0; c < 2; c++)                               // inputs: what came out `loop` samples ago
        for (long i = 0; i < frames; i++) { const long long t = pos + i - loop; put(c, half, i, t >= 0 ? line[c][(size_t)(t % (long long)L)] : 0.f); }
      cb->bufferSwitch(half, ASIOTrue);
      for (int c = 0; c < 2; c++)                               // outputs into the cable
        for (long i = 0; i < frames; i++) line[c][(size_t)((pos + i) % (long long)L)] = get(2 + c, half, i);
      pos += frames;
      const double due = (k + 1) * period;                      // keep the sample rate's pace
      for (;;) {
        QueryPerformanceCounter(&now); const double el = (double)(now.QuadPart - t0.QuadPart) / f.QuadPart;
        if (el >= due || !run) break;
        if (due - el > 0.002) Sleep(1); else SwitchToThread();
      }
    }
  }

 public:
  RigTestAsio() { g_objects++; }
  virtual ~RigTestAsio() { stop(); g_objects--; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, CLSID_RigTest)) { *ppv = this; AddRef(); return S_OK; }
    *ppv = nullptr; return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)++refs; }
  ULONG STDMETHODCALLTYPE Release() override { const long r = --refs; if (!r) delete this; return (ULONG)r; }

  ASIOBool init(void*) override {
    const char* fmt = getenv("RIGTEST_ASIO_FORMAT"); const std::string f = fmt ? fmt : "int32";
    if (f == "int16") { type = ASIOSTInt16LSB; bytes = 2; } else if (f == "int24") { type = ASIOSTInt24LSB; bytes = 3; }
    else if (f == "float") { type = ASIOSTFloat32LSB; bytes = 4; } else { type = ASIOSTInt32LSB; bytes = 4; }
    rate = envInt("RIGTEST_ASIO_RATE", 48000); loop = envInt("RIGTEST_ASIO_LOOP", 437);
    return ASIOTrue;
  }
  void getDriverName(char* name) override { strcpy(name, "RIG Test ASIO"); }
  long getDriverVersion() override { return 1; }
  void getErrorMessage(char* s) override { strcpy(s, err); }
  ASIOError start() override {
    if (!cb || !frames) return ASE_NotPresent;
    if (run) return ASE_OK;
    run = true; th = std::thread([this] { thread(); }); return ASE_OK;
  }
  ASIOError stop() override { if (run) { run = false; if (th.joinable()) th.join(); } return ASE_OK; }
  ASIOError getChannels(long* ni, long* no) override { *ni = 2; *no = 2; return ASE_OK; }
  ASIOError getLatencies(long* li, long* lo) override { *li = frames; *lo = frames; return ASE_OK; }
  ASIOError getBufferSize(long* mn, long* mx, long* pref, long* gran) override { *mn = 64; *mx = 256; *pref = 256; *gran = -1; return ASE_OK; }
  ASIOError canSampleRate(ASIOSampleRate r) override { return r == 44100 || r == 48000 || r == 96000 ? ASE_OK : ASE_NoClock; }
  ASIOError getSampleRate(ASIOSampleRate* r) override { *r = rate; return ASE_OK; }
  ASIOError setSampleRate(ASIOSampleRate r) override { if (canSampleRate(r) != ASE_OK) return ASE_NoClock; rate = r; return ASE_OK; }
  ASIOError getClockSources(ASIOClockSource* c, long* n) override { memset(c, 0, sizeof *c); c->index = 0; c->isCurrentSource = ASIOTrue; strcpy(c->name, "Internal"); *n = 1; return ASE_OK; }
  ASIOError setClockSource(long) override { return ASE_OK; }
  ASIOError getSamplePosition(ASIOSamples* s, ASIOTimeStamp* t) override { s->hi = (unsigned long)(pos >> 32); s->lo = (unsigned long)pos; t->hi = t->lo = 0; return ASE_OK; }
  ASIOError getChannelInfo(ASIOChannelInfo* ci) override {
    if (ci->channel < 0 || ci->channel > 1) return ASE_InvalidParameter;
    ci->isActive = ASIOTrue; ci->channelGroup = 0; ci->type = type;
    snprintf(ci->name, sizeof ci->name, "%s %ld", ci->isInput ? "Loop In" : "Loop Out", ci->channel + 1); return ASE_OK;
  }
  ASIOError createBuffers(ASIOBufferInfo* bi, long n, long size, ASIOCallbacks* c) override {
    if (size < 64 || size > 256 || (size & (size - 1)) || loop < size) { strcpy(err, "buffer size"); return ASE_InvalidMode; }
    frames = size; cb = c;
    for (long k = 0; k < n; k++) {
      if (bi[k].channelNum < 0 || bi[k].channelNum > 1) return ASE_InvalidParameter;
      const int slot = (bi[k].isInput ? 0 : 2) + (int)bi[k].channelNum;
      for (int h = 0; h < 2; h++) { buf[slot][h].assign((size_t)size * bytes, 0); bi[k].buffers[h] = buf[slot][h].data(); }
    }
    for (int s = 0; s < 4; s++) for (int h = 0; h < 2; h++) if (buf[s][h].empty()) buf[s][h].assign((size_t)size * bytes, 0);
    for (auto& l : line) l.assign(65536, 0.f);
    pos = 0; return ASE_OK;
  }
  ASIOError disposeBuffers() override { stop(); cb = nullptr; frames = 0; return ASE_OK; }
  ASIOError controlPanel() override { return ASE_NotPresent; }
  ASIOError future(long, void*) override { return ASE_InvalidParameter; }
  ASIOError outputReady() override { return ASE_NotPresent; }
};

class Factory : public IClassFactory {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) { *ppv = this; return S_OK; }
    *ppv = nullptr; return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    if (outer) return CLASS_E_NOAGGREGATION;
    RigTestAsio* d = new RigTestAsio(); const HRESULT r = d->QueryInterface(riid, ppv); d->Release(); return r;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};
static Factory g_factory;

extern "C" HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (!IsEqualCLSID(clsid, CLSID_RigTest)) return CLASS_E_CLASSNOTAVAILABLE;
  return g_factory.QueryInterface(riid, ppv);
}
extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow() { return g_objects ? S_FALSE : S_OK; }
