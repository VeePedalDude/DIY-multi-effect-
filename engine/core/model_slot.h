// engine/core/model_slot.h - holds a NeuralAmpModelerCore model (a NAM capture, or an IR as a
// nam::Linear) for a module, and swaps models without clicks or audio-thread allocation.
//
//   control thread:  build the model (parse, allocate, Reset, prewarm), then stage(model)
//   audio thread:    begin(n) once per block -> weights; run current() (and next() while a swap
//                    is under way) and mix them by those weights
//
// A swap: the new model first runs silently on the live input for its warm-up (a capture's
// receptive field, so it doesn't start from an empty history), then crossfades in over 10 ms. The old model is handed back to the control thread (retired) and freed there on the next
// stage() or collect(), so the audio thread never allocates or frees. In the browser both
// "threads" are the AudioWorklet, which is also fine.
#pragma once
#include <atomic>
#include <memory>
#include "NAM/dsp.h"

namespace rig {

struct LoadedModel {
  std::unique_ptr<nam::DSP> dsp;   // nullptr = the module's built-in sound
  std::unique_ptr<nam::DSP> dsp2;  // optional second mono model (right channel of a stereo IR)
  float gain = 1.f;                // level correction (loudness normalisation for captures)
  double rate = 0;                 // rate the model was made at (0 = unknown or built-in)
  int frames = 0;                  // IR length after resampling (IR slots)
  int warm = 0;                    // samples to run silently on live input before crossfading in
};

// Weight of current() across a block (next() gets 1 - weight).
struct Fade {
  float from, to;
  float at(int i, int n) const { return from + (to - from) * (float)(i + 1) / (float)n; }
  Fade other() const { return {1.f - from, 1.f - to}; }
};

class ModelSlot {
 public:
  static constexpr int NRETIRED = 4;
  ModelSlot() = default;
  ModelSlot(const ModelSlot&) = delete;
  ModelSlot& operator=(const ModelSlot&) = delete;
  ~ModelSlot() {
    delete cur_; delete next_; delete staged_.exchange(nullptr);
    for (auto& r : retired_) delete r.exchange(nullptr);
  }

  // control thread
  void stage(std::unique_ptr<LoadedModel> m) {
    collect();
    delete staged_.exchange(m.release(), std::memory_order_acq_rel);   // an unswapped earlier load is dropped
  }
  // Install a model immediately. Only while audio is stopped (module init).
  void replaceNow(std::unique_ptr<LoadedModel> m) {
    delete cur_; delete next_; next_ = nullptr; cur_ = m.release(); xf_ = 0.f;
    delete staged_.exchange(nullptr); collect();
  }
  void collect() { for (auto& r : retired_) delete r.exchange(nullptr, std::memory_order_acq_rel); }

  void setFade(int samples) { step_ = 1.f / (float)(samples > 1 ? samples : 1); }

  // audio thread: call once per block of n samples, before current() / next()
  Fade begin(int n) {
    if (next_ && xf_ >= 1.f && retire(cur_)) { cur_ = next_; next_ = nullptr; }   // previous crossfade done
    if (!next_) {
      next_ = staged_.exchange(nullptr, std::memory_order_acq_rel);
      if (!next_) return {1.f, 1.f};
      xf_ = 0.f; warmLeft_ = next_->warm;
    }
    if (warmLeft_ > 0) { warmLeft_ -= n; return {1.f, 1.f}; }
    const float from = 1.f - xf_;
    xf_ = xf_ + step_ * n < 1.f ? xf_ + step_ * n : 1.f;
    return {from, 1.f - xf_};
  }
  const LoadedModel* next() const { return next_; }   // non-null only during a crossfade
  const LoadedModel* current() const { return cur_; }
  nam::DSP* dsp() const { return cur_ ? cur_->dsp.get() : nullptr; }

 private:
  bool retire(LoadedModel* old) {
    if (!old) return true;
    for (auto& r : retired_) { LoadedModel* empty = nullptr; if (r.compare_exchange_strong(empty, old)) return true; }
    return false;
  }
  LoadedModel* next_ = nullptr;                      // fading in (audio thread)
  float xf_ = 0.f, step_ = 1.f / 480.f;
  int warmLeft_ = 0;
  LoadedModel* cur_ = nullptr;                       // owned by the audio thread
  std::atomic<LoadedModel*> staged_{nullptr};
  std::atomic<LoadedModel*> retired_[NRETIRED] = {};
};

} // namespace rig
