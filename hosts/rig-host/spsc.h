// hosts/rig-host/spsc.h - a lock-free single-producer / single-consumer ring of fixed slots, for
// passing commands to the audio thread and replies and meters back without locks or allocation.
// The producer claims a slot, fills it in place, publishes; the consumer reads the front, releases.
#pragma once
#include <atomic>

template <class T, unsigned N> struct Spsc {
  static_assert((N & (N - 1)) == 0, "N must be a power of two");
  T slot[N];
  std::atomic<unsigned> head{0}, tail{0};                     // head: next to write (producer), tail: next to read (consumer)
  T* claim() { const unsigned h = head.load(std::memory_order_relaxed); return h - tail.load(std::memory_order_acquire) < N ? &slot[h & (N - 1)] : nullptr; }
  void publish() { head.store(head.load(std::memory_order_relaxed) + 1, std::memory_order_release); }
  T* front() { const unsigned t = tail.load(std::memory_order_relaxed); return t != head.load(std::memory_order_acquire) ? &slot[t & (N - 1)] : nullptr; }
  void release() { tail.store(tail.load(std::memory_order_relaxed) + 1, std::memory_order_release); }
};
