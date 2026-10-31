// hosts/rig-host/asio_host.h - ASIO on Windows: the audio interface's own driver (Focusrite's, RME's,
// ...), the low-latency path every DAW uses there. Included by main.cpp on Windows only.
//
// The interface definitions are Steinberg's ASIO SDK 2.3.4 (third_party/asiosdk, GPLv3 under its dual
// license). Only the headers are used: the drivers are COM objects listed in the registry under
// HKLM\SOFTWARE\ASIO\<name> (value CLSID), created with CoCreateInstance, and called through IASIO.
// That's all the SDK's host glue (asio.cpp, asiodrivers.cpp, asiolist.cpp) does too.
//
// ASIO calls bufferSwitch(index) from the driver's own thread twice per round: inputs are filled and
// outputs wanted in half `index` of each channel's double buffer, in the driver's sample format.
// The host converts to interleaved floats, runs the same runBlock as the miniaudio path, converts back
// and calls outputReady() when the driver supports it (one buffer less of latency).
#include <objbase.h>
#include <unknwn.h>
#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"

struct AsioDriverInfo { std::string name; CLSID clsid; };
static std::vector<AsioDriverInfo> asioDrivers() {
  std::vector<AsioDriverInfo> v; HKEY root;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &root) != ERROR_SUCCESS) return v;
  for (DWORD i = 0;; i++) {
    char key[256]; DWORD n = sizeof key;
    if (RegEnumKeyExA(root, i, key, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
    HKEY k; if (RegOpenKeyExA(root, key, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
    char cls[128] = {0}, desc[256] = {0}; DWORD cn = sizeof cls - 1, dn = sizeof desc - 1, type = 0;
    if (RegQueryValueExA(k, "CLSID", nullptr, &type, (BYTE*)cls, &cn) == ERROR_SUCCESS && type == REG_SZ) {
      wchar_t w[128]; MultiByteToWideChar(CP_ACP, 0, cls, -1, w, 128); CLSID id;
      if (CLSIDFromString(w, &id) == NOERROR) {
        const bool hasDesc = RegQueryValueExA(k, "Description", nullptr, &type, (BYTE*)desc, &dn) == ERROR_SUCCESS && type == REG_SZ && desc[0];
        v.push_back({hasDesc ? desc : key, id});
      }
    }
    RegCloseKey(k);
  }
  RegCloseKey(root);
  return v;
}
// Drivers that wrap Windows' own audio (WASAPI/WDM/MME) rather than talking to an interface: no better
// than the WASAPI path, so they're used only when asked for by name.
static bool asioGeneric(const std::string& name) {
  const std::string n = lower(name);
  for (const char* g : {"asio4all", "flexasio", "generic low latency", "realtek", "wineasio", "koordasio", "asio2wasapi"}) if (n.find(g) != std::string::npos) return true;
  return false;
}

// The open driver. ASIO's callbacks carry no user pointer, so this is the one global.
struct AsioState {
  IASIO* drv = nullptr; Host* h = nullptr;
  ASIOBufferInfo bi[4]{}; ASIOSampleType type[4]{}; int nIn = 0, nOut = 0; long frames = 0; bool outReady = false;
  std::vector<float> in, out;                                // interleaved, sized at open: nothing allocates in the callback
  std::atomic<int> reset{0};                                 // the driver asked to be reopened (its buffer size or rate changed)
  ASIOCallbacks cb{};
};
static AsioState g_asio;

static bool asioTypeOk(ASIOSampleType t) {
  return t == ASIOSTInt16LSB || t == ASIOSTInt24LSB || t == ASIOSTInt32LSB || t == ASIOSTFloat32LSB || t == ASIOSTFloat64LSB ||
         t == ASIOSTInt32LSB16 || t == ASIOSTInt32LSB18 || t == ASIOSTInt32LSB20 || t == ASIOSTInt32LSB24;
}
static const char* asioTypeName(ASIOSampleType t) {
  switch (t) {
    case ASIOSTInt16LSB: return "16-bit"; case ASIOSTInt24LSB: return "24-bit"; case ASIOSTInt32LSB: return "32-bit";
    case ASIOSTFloat32LSB: return "float"; case ASIOSTFloat64LSB: return "double"; case ASIOSTInt32LSB16: return "16 in 32-bit";
    case ASIOSTInt32LSB18: return "18 in 32-bit"; case ASIOSTInt32LSB20: return "20 in 32-bit"; case ASIOSTInt32LSB24: return "24 in 32-bit";
    default: return "unsupported";
  }
}
// One channel of the driver's buffer to/from float, `stride` apart in the interleaved buffer.
static void asioRead(const void* src, ASIOSampleType t, float* dst, int stride, long n) {
  switch (t) {
    case ASIOSTInt16LSB: { const int16_t* s = (const int16_t*)src; for (long i = 0; i < n; i++) dst[i * stride] = s[i] * (1.f / 32768.f); break; }
    case ASIOSTInt24LSB: { const uint8_t* s = (const uint8_t*)src; for (long i = 0; i < n; i++) { const int32_t v = (int32_t)((uint32_t)s[3 * i] << 8 | (uint32_t)s[3 * i + 1] << 16 | (uint32_t)s[3 * i + 2] << 24); dst[i * stride] = (float)(v * (1.0 / 2147483648.0)); } break; }
    case ASIOSTInt32LSB: { const int32_t* s = (const int32_t*)src; for (long i = 0; i < n; i++) dst[i * stride] = (float)(s[i] * (1.0 / 2147483648.0)); break; }
    case ASIOSTFloat32LSB: { const float* s = (const float*)src; for (long i = 0; i < n; i++) dst[i * stride] = s[i]; break; }
    case ASIOSTFloat64LSB: { const double* s = (const double*)src; for (long i = 0; i < n; i++) dst[i * stride] = (float)s[i]; break; }
    case ASIOSTInt32LSB16: case ASIOSTInt32LSB18: case ASIOSTInt32LSB20: case ASIOSTInt32LSB24: {
      const int bits = t == ASIOSTInt32LSB16 ? 16 : t == ASIOSTInt32LSB18 ? 18 : t == ASIOSTInt32LSB20 ? 20 : 24;
      const double k = 1.0 / (double)(1 << (bits - 1)); const int32_t* s = (const int32_t*)src;
      for (long i = 0; i < n; i++) dst[i * stride] = (float)(s[i] * k); break;
    }
    default: for (long i = 0; i < n; i++) dst[i * stride] = 0.f;
  }
}
static inline double clip1(float x) { return x > 1.f ? 1.0 : x < -1.f ? -1.0 : (double)x; }
static void asioWrite(const float* src, int stride, ASIOSampleType t, void* dst, long n) {
  switch (t) {
    case ASIOSTInt16LSB: { int16_t* d = (int16_t*)dst; for (long i = 0; i < n; i++) d[i] = (int16_t)lrint(clip1(src[i * stride]) * 32767.0); break; }
    case ASIOSTInt24LSB: { uint8_t* d = (uint8_t*)dst; for (long i = 0; i < n; i++) { const int32_t v = (int32_t)lrint(clip1(src[i * stride]) * 8388607.0); d[3 * i] = (uint8_t)v; d[3 * i + 1] = (uint8_t)(v >> 8); d[3 * i + 2] = (uint8_t)(v >> 16); } break; }
    case ASIOSTInt32LSB: { int32_t* d = (int32_t*)dst; for (long i = 0; i < n; i++) d[i] = (int32_t)llrint(clip1(src[i * stride]) * 2147483647.0); break; }
    case ASIOSTFloat32LSB: { float* d = (float*)dst; for (long i = 0; i < n; i++) d[i] = src[i * stride]; break; }
    case ASIOSTFloat64LSB: { double* d = (double*)dst; for (long i = 0; i < n; i++) d[i] = src[i * stride]; break; }
    case ASIOSTInt32LSB16: case ASIOSTInt32LSB18: case ASIOSTInt32LSB20: case ASIOSTInt32LSB24: {
      const int bits = t == ASIOSTInt32LSB16 ? 16 : t == ASIOSTInt32LSB18 ? 18 : t == ASIOSTInt32LSB20 ? 20 : 24;
      const double k = (double)((1 << (bits - 1)) - 1); int32_t* d = (int32_t*)dst;
      for (long i = 0; i < n; i++) d[i] = (int32_t)lrint(clip1(src[i * stride]) * k); break;
    }
    default: memset(dst, 0, (size_t)n * 4);
  }
}

static void asioBufferSwitch(long index, ASIOBool) {
  AsioState& s = g_asio; const long n = s.frames;
  for (int c = 0; c < s.nIn; c++) asioRead(s.bi[c].buffers[index], s.type[c], s.in.data() + c, s.nIn, n);
  runBlock(*s.h, s.nIn ? s.in.data() : nullptr, s.out.data(), (ma_uint32)n);
  for (int c = 0; c < s.nOut; c++) asioWrite(s.out.data() + c, s.nOut, s.type[s.nIn + c], s.bi[s.nIn + c].buffers[index], n);
  if (s.outReady) s.drv->outputReady();
}
static ASIOTime* asioBufferSwitchTimeInfo(ASIOTime*, long index, ASIOBool direct) { asioBufferSwitch(index, direct); return nullptr; }
static void asioRateChanged(ASIOSampleRate) { g_asio.reset = 1; }
static long asioMessage(long selector, long value, void*, double*) {
  switch (selector) {
    case kAsioSelectorSupported: return value == kAsioResetRequest || value == kAsioEngineVersion || value == kAsioResyncRequest || value == kAsioLatenciesChanged ? 1 : 0;
    case kAsioResetRequest: g_asio.reset = 1; return 1;        // e.g. the buffer size changed in the driver's control panel
    case kAsioResyncRequest: return 1;
    case kAsioLatenciesChanged: return 1;
    case kAsioEngineVersion: return 2;
    default: return 0;
  }
}

static void asioClose(Audio& a) {
  AsioState& s = g_asio;
  if (!s.drv) return;
  if (a.running) s.drv->stop();
  s.drv->disposeBuffers(); s.drv->Release(); s.drv = nullptr; a.open = a.running = false;
}
// Open the driver `a.asioName` names: `frames` per buffer (0: the size set in the driver's own control
// panel), `rate` (0: the driver's current one).
static bool asioOpen(Host& h, Audio& a, int frames, int rate) {
  AsioState& s = g_asio; s.h = &h; s.reset = 0;
  if (FAILED(CoCreateInstance(a.asioClsid, nullptr, CLSCTX_INPROC_SERVER, a.asioClsid, (void**)&s.drv)) || !s.drv) {
    fprintf(stderr, "ASIO: couldn't load the %s driver\n", a.asioName.c_str()); s.drv = nullptr; return false;
  }
  HWND wnd = GetConsoleWindow(); if (!wnd) wnd = GetDesktopWindow();
  char err[128] = {0};
  if (!s.drv->init(wnd)) { s.drv->getErrorMessage(err); fprintf(stderr, "ASIO: %s won't start (%s). Is another program using it?\n", a.asioName.c_str(), err); s.drv->Release(); s.drv = nullptr; return false; }
  auto fail = [&](const char* what) { fprintf(stderr, "ASIO: %s: %s\n", a.asioName.c_str(), what); s.drv->disposeBuffers(); s.drv->Release(); s.drv = nullptr; return false; };
  long ni = 0, no = 0; s.drv->getChannels(&ni, &no);
  if (no < 1) return fail("no outputs");
  if (rate > 0 && s.drv->canSampleRate((ASIOSampleRate)rate) == ASE_OK) s.drv->setSampleRate((ASIOSampleRate)rate);
  ASIOSampleRate sr = 0; if (s.drv->getSampleRate(&sr) != ASE_OK || sr < 8000) return fail("no sample rate");
  long mn = 0, mx = 0, pref = 0, gran = 0; s.drv->getBufferSize(&mn, &mx, &pref, &gran);
  long n = pref;
  if (frames > 0) {                                           // the nearest size the driver allows
    if (gran == -1) { n = mn; while (n < frames && n * 2 <= mx) n *= 2; }
    else if (gran > 0) { n = std::clamp<long>((long)frames, mn, mx); n = mn + (n - mn + gran - 1) / gran * gran; if (n > mx) n = mx; }
  }
  if (n < 1 || n > 8192) return fail("unusable buffer size");
  s.nIn = (int)std::min<long>(2, ni); s.nOut = (int)std::min<long>(2, no);
  for (int c = 0; c < s.nIn + s.nOut; c++) { s.bi[c] = ASIOBufferInfo{}; s.bi[c].isInput = c < s.nIn ? ASIOTrue : ASIOFalse; s.bi[c].channelNum = c < s.nIn ? c : c - s.nIn; }
  s.cb.bufferSwitch = asioBufferSwitch; s.cb.bufferSwitchTimeInfo = asioBufferSwitchTimeInfo; s.cb.sampleRateDidChange = asioRateChanged; s.cb.asioMessage = asioMessage;
  if (s.drv->createBuffers(s.bi, s.nIn + s.nOut, n, &s.cb) != ASE_OK) return fail("couldn't make the buffers");
  std::string inNames, outName;
  for (int c = 0; c < s.nIn + s.nOut; c++) {
    ASIOChannelInfo ci{}; ci.channel = s.bi[c].channelNum; ci.isInput = s.bi[c].isInput;
    if (s.drv->getChannelInfo(&ci) != ASE_OK) return fail("no channel info");
    s.type[c] = ci.type;
    if (!asioTypeOk(ci.type)) return fail("a sample format this host doesn't read yet");
    if (c < s.nIn) inNames += (c ? " / " : "") + std::string(ci.name);
  }
  s.frames = n; s.in.assign((size_t)n * std::max(1, s.nIn), 0.f); s.out.assign((size_t)n * s.nOut, 0.f);
  s.outReady = s.drv->outputReady() == ASE_OK;
  long li = 0, lo = 0; s.drv->getLatencies(&li, &lo);
  h.inChannels = std::max(1, s.nIn); h.outChannels = s.nOut; h.sr = (int)sr;
  h.bufS = 2.0 * n / sr; h.settle = 20;
  a.backend = "ASIO"; a.exclusive = false; a.frames = (int)n; a.periods = 2; a.capFrames = (int)n; a.capPeriods = 1;
  a.maxFrames = (int)mx; a.judge = true;
  a.nominalMs = li + lo > 0 ? 1000.0 * (li + lo) / sr : 1000.0 * 3 * n / sr;
  a.inName = a.asioName + (inNames.empty() ? "" : ": " + inNames) + " (" + asioTypeName(s.type[0]) + ")";
  a.outName = a.asioName;
  a.note.clear(); a.open = true;
  if ((int)sr != 48000) a.note = "the interface runs at " + std::to_string((int)sr) + " Hz; NAM captures are made at 48000 Hz (set it in the interface's control panel)";
  return true;
}
static bool asioStart(Audio& a) {
  if (!g_asio.drv || g_asio.drv->start() != ASE_OK) return false;
  a.running = true; return true;
}
