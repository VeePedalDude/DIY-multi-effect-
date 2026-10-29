// heap_count.cpp - counts heap calls made while a piece of code runs (glibc builds only), so tests can
// prove the audio path never allocates. Replaces malloc and friends for the whole test program.
#include "framework.h"
#include <atomic>

#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)   // AddressSanitizer brings its own malloc
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t, size_t);
extern "C" void* __libc_realloc(void*, size_t);
extern "C" void* __libc_memalign(size_t, size_t);
extern "C" void __libc_free(void*);
static std::atomic<long> g_heapCalls{0};
static std::atomic<bool> g_counting{false};
static inline void note() { if (g_counting.load(std::memory_order_relaxed)) g_heapCalls++; }
extern "C" void* malloc(size_t n) noexcept { note(); return __libc_malloc(n); }
extern "C" void* calloc(size_t a, size_t b) noexcept { note(); return __libc_calloc(a, b); }
extern "C" void* realloc(void* p, size_t n) noexcept { note(); return __libc_realloc(p, n); }
extern "C" void free(void* p) noexcept { if (p) note(); __libc_free(p); }
extern "C" void* aligned_alloc(size_t a, size_t n) noexcept { note(); return __libc_memalign(a, n); }
extern "C" void* memalign(size_t a, size_t n) noexcept { note(); return __libc_memalign(a, n); }
extern "C" int posix_memalign(void** p, size_t a, size_t n) noexcept { note(); *p = __libc_memalign(a, n); return *p ? 0 : 12; }
namespace rigtest {
bool heapCounting() { return true; }
long heapCallsDuring(const std::function<void()>& f) { g_heapCalls = 0; g_counting = true; f(); g_counting = false; return g_heapCalls.load(); }
}
#else
namespace rigtest {
bool heapCounting() { return false; }
long heapCallsDuring(const std::function<void()>& f) { f(); return 0; }
}
#endif
