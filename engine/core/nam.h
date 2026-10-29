// engine/core/nam.h - the one place the engine talks to NeuralAmpModelerCore
// (third_party/NeuralAmpModelerCore, MIT, https://github.com/sdatkinson/NeuralAmpModelerCore).
//
// NAM core plays every .nam architecture (WaveNet A1 and A2, LSTM, ConvNet, Linear, Sequential,
// SlimmableContainer) and has a zero-latency partitioned FFT convolver (nam::Linear) that we use
// for impulse responses of any length. Loading allocates and may throw, so it happens here on
// the control thread; errors come back as text for the UI instead of crashing the engine.
#pragma once
#ifndef NAM_SAMPLE_FLOAT
#error "Build with -DNAM_SAMPLE_FLOAT (the engine runs NAM in float). See build.sh."
#endif
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "NAM/activations.h"
#include "NAM/get_dsp.h"
#include "NAM/linear.h"
#include "model_slot.h"

namespace rig {

constexpr float CAPTURE_LOUDNESS_DB = -18.f;   // captures with loudness metadata are levelled to this (as the NAM plugin does)

// The NAM plugin's default: a fast tanh approximation (A1 WaveNets spend most of their time in tanh).
inline void namInit() {
  static bool done = false;
  if (!done) { nam::activations::Activation::enable_fast_tanh(); done = true; }
}

inline std::string khz(double hz) { char b[32]; snprintf(b, sizeof b, "%g kHz", std::round(hz / 100.0) / 10.0); return b; }

// NAM core trusts each file's weight count: most loaders read the weights list without a bounds
// check, so a well-formed file with too few weights reads past it. A cut-off download is invalid
// JSON and is rejected before that; LSTM is the one loader where NAM meant to check (a debug-only
// assert), so we check it here, including LSTMs inside Sequential and Slimmable containers.
inline void checkLstmWeights(const nlohmann::json& j) {
  if (j.is_array()) { for (const auto& v : j) checkLstmWeights(v); return; }
  if (!j.is_object()) return;
  if (j.value("architecture", std::string()) == "LSTM" && j.contains("config") && j.contains("weights")) {
    const auto& c = j["config"];
    const long L = c.value("num_layers", 0), in = c.value("input_size", 0), H = c.value("hidden_size", 0), out = c.value("out_channels", 1);
    if (L < 0 || in < 0 || H < 0 || out < 1 || L > 64 || H > 4096 || in > 4096) throw std::runtime_error("LSTM config out of range");
    long need = out * H + out;
    for (long l = 0; l < L; l++) need += 4 * H * ((l ? H : in) + H) + 4 * H + 2 * H;
    const long have = j["weights"].is_array() ? (long)j["weights"].size() : -1;
    if (have != need) throw std::runtime_error("LSTM has " + std::to_string(have) + " weights, its config needs " + std::to_string(need) + " (damaged file?)");
  }
  for (const auto& [k, v] : j.items()) if (k != "weights") checkLstmWeights(v);
}

// A .nam file's JSON -> a playable model. Returns nullptr and sets `note` to the reason on failure.
inline std::unique_ptr<LoadedModel> loadCapture(const unsigned char* json, int bytes, float sr, int maxBlock, std::string& note) {
  namInit();
  try {
    nlohmann::json j = nlohmann::json::parse(json, json + bytes);
    checkLstmWeights(j);
    nam::DspLoadOptions opt; opt.prewarm = false;
    auto lm = std::make_unique<LoadedModel>();
    lm->dsp = nam::get_dsp(j, opt);
    nam::DSP& d = *lm->dsp;
    if (d.NumInputChannels() < 1 || d.NumInputChannels() > 2 || d.NumOutputChannels() < 1 || d.NumOutputChannels() > 2) {
      note = "this capture has " + std::to_string(d.NumInputChannels()) + " in / " + std::to_string(d.NumOutputChannels()) + " out channels; the amp takes 1 or 2";
      return nullptr;
    }
    d.ResetAndPrewarm(sr, maxBlock);
    const bool rateKnown = d.GetExpectedSampleRate() > 0;
    lm->rate = rateKnown ? d.GetExpectedSampleRate() : 48000.0;      // older files don't say; NAM captures are 48 kHz
    lm->warm = std::min(d.GetPrewarmSamples(), (int)(0.15f * sr));   // LSTMs report 0.5 s but settle sooner
    lm->gain = d.HasLoudness() ? powf(10.f, (CAPTURE_LOUDNESS_DB - (float)d.GetLoudness()) / 20.f) : 1.f;
    std::string arch = j.value("architecture", std::string("model"));
    note = arch;
    note += " · " + khz(lm->rate) + (rateKnown ? "" : " (assumed)");
    if (std::fabs(lm->rate - sr) > 1) note += " capture running at " + khz(sr) + " (tone shifts slightly)";
    note += d.HasLoudness() ? " · levelled" : " · no loudness data, not levelled";
    return lm;
  } catch (const nlohmann::json::parse_error&) {
    note = "not valid JSON: a damaged download, or not a .nam file";
  } catch (const nlohmann::json::exception& e) {
    note = std::string("a setting the capture needs is missing or the wrong type (") + e.what() + ")";
  } catch (const std::exception& e) {
    note = e.what();
  } catch (...) {
    note = "unknown error";
  }
  return nullptr;
}

// Impulse responses travel as a small header plus planar float32 samples:
//   int32 magic 'RIR1' · int32 channels (1 or 2) · int32 frames · float32 sample rate · ch0[frames] · ch1[frames]
// The browser decodes the .wav (any format the browser reads) at the engine's rate; the Linux host
// can pass the file's own rate and nam::Linear resamples.
constexpr int32_t IR_MAGIC = 0x31524952;      // "RIR1"
constexpr float IR_MAX_SECONDS = 20.f;
constexpr int IR_HEADER = 16;
constexpr int irMaxBytes() { return IR_HEADER + 2 * (int)(IR_MAX_SECONDS * 192000.f) * 4; }

// Energy-normalise so IRs of any length swap at similar loudness (`level` = RMS gain target used by
// the original 2048-tap cab, measured at the engine rate).
inline std::unique_ptr<LoadedModel> makeIr(const float* planar, int ch, int frames, float rate, float sr, int maxBlock,
                                           float level, std::string& note) {
  try {
    double e = 0;
    for (int i = 0; i < ch * frames; i++) e += (double)planar[i] * planar[i];
    e = e / ch * (rate / sr);                          // per channel, as if sampled at the engine rate
    if (!(e > 1e-12) || !std::isfinite(e)) { note = "the IR is silent or has invalid samples"; return nullptr; }
    // Stereo runs as two mono convolvers, the right one's schedule offset from the left's so their
    // big FFT blocks never land in the same audio block (nam::Linear 1-in/2-out would do both at once).
    auto lm = std::make_unique<LoadedModel>();
    for (int c = 0; c < ch; c++) {
      std::vector<float> w(planar + (size_t)c * frames, planar + (size_t)(c + 1) * frames);
      // FFT above 256 taps: NAM's own table picks direct convolution up to 1024 taps, which is fine
      // natively but ~3x slower in WebAssembly, where Eigen has no SIMD (measured on the built-in cab).
      auto lin = std::make_unique<nam::Linear>(1, 1, frames, false, w, rate,
                                               frames > 256 ? nam::LinearImplementation::FFT : nam::LinearImplementation::Auto);
      lin->Reset(sr, maxBlock);                        // resamples when rate != sr
      if (c == 1) {
        const int skew = nam::linear::select_fft_plan((int)std::ceil(frames * (double)sr / rate)).max_partition_size / 2 + 96;
        std::vector<float> z(maxBlock, 0.f), o(maxBlock);
        float* ip[1] = {z.data()}; float* op[1] = {o.data()};
        for (int done = 0; done < skew;) { int k = std::min(maxBlock, skew - done); lin->process(ip, op, k); done += k; }
      }
      (c ? lm->dsp2 : lm->dsp) = std::move(lin);
    }
    lm->rate = rate;
    lm->gain = (float)(level / std::sqrt(e));
    lm->frames = (int)std::ceil(frames * (double)sr / rate);
    char b[96];
    snprintf(b, sizeof b, "%s · %.2f s%s", ch == 2 ? "stereo" : "mono", frames / rate, std::fabs(rate - sr) > 1 ? (" · resampled from " + khz(rate)).c_str() : "");
    note = b;
    return lm;
  } catch (const std::exception& ex) {
    note = ex.what();
  } catch (...) {
    note = "unknown error";
  }
  return nullptr;
}

inline std::unique_ptr<LoadedModel> parseIr(const unsigned char* buf, int bytes, float sr, int maxBlock, float level, std::string& note) {
  int32_t h[3]; float rate;
  if (bytes < IR_HEADER) { note = "IR data too short"; return nullptr; }
  memcpy(h, buf, 12); memcpy(&rate, buf + 12, 4);
  const int ch = h[1], frames = h[2];
  if (h[0] != IR_MAGIC) { note = "not IR data (missing RIR1 header)"; return nullptr; }
  if (ch < 1 || ch > 2) { note = "IRs can have 1 or 2 channels"; return nullptr; }
  if (!(rate >= 8000.f && rate <= 384000.f)) { note = "IR sample rate out of range"; return nullptr; }
  if (frames < 1 || frames > IR_MAX_SECONDS * rate) { note = "IR longer than 20 s"; return nullptr; }
  if ((int64_t)bytes != IR_HEADER + (int64_t)ch * frames * 4) { note = "IR size doesn't match its header"; return nullptr; }
  std::vector<float> s((size_t)ch * frames);
  memcpy(s.data(), buf + IR_HEADER, s.size() * 4);
  return makeIr(s.data(), ch, frames, rate, sr, maxBlock, level, note);
}

} // namespace rig
