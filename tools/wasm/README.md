# tools/wasm

What it takes to build NeuralAmpModelerCore (C++20, exceptions, Eigen) to WebAssembly with the
zig toolchain from `pip install ziglang`, without Emscripten. `build.sh` uses all of these.

| File | Why |
|---|---|
| `cpp_exception.S` | NAM core reports bad files by throwing. zig's WASI C++ runtime is built without exceptions, so `build.sh` compiles libc++abi's `cxa_exception.cpp` / `cxa_personality.cpp` and libunwind's `Unwind-wasm.c` (from zig's own sources) with `-fwasm-exceptions`, and this file supplies the `__cpp_exception` tag Emscripten normally provides. Link these objects first, so they win over zig's no-exception stubs. |
| `nam_compat.h` | Force-included when compiling NAM core. The browser engine runs on one thread, and zig's single-threaded WASI libc++ leaves out `std::mutex` and the atomic `shared_ptr` helpers, so they become plain no-ops / loads and stores. |
| `nam_no_files.cpp` | Replaces NAM's file readers (`nam_file.cpp`, `wav.cpp`): there is no file system in the browser; the page reads files and passes their contents. |
| `wasi_none.c` | Defines the few WASI calls the C/C++ runtime links in (an error print before abort, the environment, `close`), so `rig.wasm` has **no imports** and loads with `new WebAssembly.Instance(module, {})`. |

Browser support: WebAssembly exceptions and SIMD, i.e. Chrome/Edge 95+, Firefox 100+, Safari 15.2+
(SIMD: Safari 16.4+).
