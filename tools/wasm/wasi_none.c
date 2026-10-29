// tools/wasm/wasi_none.c - the engine needs no operating system. The C/C++ runtime links in a few
// WASI calls (printing an error before abort, reading the environment); defining them here means
// build/rig.wasm imports nothing, so the browser, Node and tests instantiate it with {}.
// Arguments are wasm32 addresses into the engine's own memory.
#include <stdint.h>
#define WASI(name) __imported_wasi_snapshot_preview1_##name
enum { ERRNO_BADF = 8 };
typedef struct { uint32_t buf, len; } iovec32;
int32_t WASI(fd_write)(int32_t fd, int32_t iovs, int32_t n, int32_t written) {
  (void)fd; uint32_t total = 0; const iovec32* v = (const iovec32*)(uintptr_t)iovs;
  for (int32_t i = 0; i < n; i++) total += v[i].len;
  *(uint32_t*)(uintptr_t)written = total; return 0;               // output is discarded
}
int32_t WASI(fd_read)(int32_t fd, int32_t iovs, int32_t n, int32_t got) { (void)fd; (void)iovs; (void)n; *(uint32_t*)(uintptr_t)got = 0; return ERRNO_BADF; }
int32_t WASI(fd_close)(int32_t fd) { (void)fd; return ERRNO_BADF; }
int32_t WASI(fd_seek)(int32_t fd, int64_t off, int32_t whence, int32_t pos) { (void)fd; (void)off; (void)whence; (void)pos; return ERRNO_BADF; }
int32_t WASI(fd_fdstat_get)(int32_t fd, int32_t st) { (void)fd; (void)st; return ERRNO_BADF; }
int32_t WASI(environ_sizes_get)(int32_t count, int32_t size) { *(uint32_t*)(uintptr_t)count = 0; *(uint32_t*)(uintptr_t)size = 0; return 0; }
int32_t WASI(environ_get)(int32_t env, int32_t buf) { (void)env; (void)buf; return 0; }
_Noreturn void WASI(proc_exit)(int32_t code) { (void)code; __builtin_trap(); }
// zig's own libc close() calls the fd_close import directly; this close() replaces it.
int close(int fd) { (void)fd; return -1; }
