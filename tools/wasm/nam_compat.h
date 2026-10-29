// tools/wasm/nam_compat.h - force-included when compiling NeuralAmpModelerCore for WebAssembly.
// The browser engine runs on one thread (the AudioWorklet), and zig's single-threaded WASI C++
// library leaves out std::mutex and the atomic shared_ptr helpers. NAM only uses them to guard
// against other threads, so on one thread they can be plain no-ops / plain loads and stores.
#pragma once
#ifdef __wasm__
#include <memory>
#include <mutex>
#include <atomic>
namespace std {
#if !defined(_LIBCPP_HAS_THREADS) || !_LIBCPP_HAS_THREADS
struct mutex {
  constexpr mutex() noexcept = default;
  mutex(const mutex&) = delete;
  mutex& operator=(const mutex&) = delete;
  void lock() {}
  bool try_lock() { return true; }
  void unlock() {}
};
template <class T> shared_ptr<T> atomic_load_explicit(const shared_ptr<T>* p, memory_order) { return *p; }
template <class T> void atomic_store_explicit(shared_ptr<T>* p, shared_ptr<T> v, memory_order) { *p = std::move(v); }
template <class T> shared_ptr<T> atomic_exchange_explicit(shared_ptr<T>* p, shared_ptr<T> v, memory_order) { p->swap(v); return v; }
#endif
}  // namespace std
#endif
