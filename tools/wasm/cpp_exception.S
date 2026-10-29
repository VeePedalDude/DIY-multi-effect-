# The exception tag C++ throws with on WebAssembly. Emscripten ships this in its runtime; the
# WASI toolchain zig bundles doesn't, so we provide it (see tools/wasm/README.md).
.globl __cpp_exception
.tagtype __cpp_exception i32
__cpp_exception:
