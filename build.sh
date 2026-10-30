#!/bin/sh
# build.sh - build and test everything. Stops at the first failure.
#   native tests -> WebAssembly engine -> UI unit tests -> parity check -> dist/rig-test.html
# The vendored libraries in third_party/ compile once into build/obj/ and are reused until a file
# there changes (first run: a few minutes; after that, seconds).
set -e
cd "$(dirname "$0")"
mkdir -p build/obj/native build/obj/wasm dist
CXX="${CXX:-g++}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
NAM_SRC="$(ls third_party/NeuralAmpModelerCore/NAM/*.cpp third_party/NeuralAmpModelerCore/NAM/wavenet/*.cpp)"
INC="-DNDEBUG -DNAM_SAMPLE_FLOAT -include engine/core/json_assert.h -isystem third_party/NeuralAmpModelerCore -isystem third_party/NeuralAmpModelerCore/NAM -isystem third_party/eigen -isystem third_party/nlohmann -isystem third_party/q -isystem third_party/q/infra"
export ZIG_GLOBAL_CACHE_DIR="$PWD/build/zig-cache" ZIG_LOCAL_CACHE_DIR="$PWD/build/zig-cache"
ZIGCXX="python3 -m ziglang c++"
WASM="-target wasm32-wasi -fwasm-exceptions -mexception-handling"

# stale <out-dir> <command...> : compile each listed source (stdin) whose object is missing or older
stale() {
  dir=$1; shift
  while read -r src; do
    obj="$dir/$(echo "$src" | tr '/' '_' | sed 's/\.[a-zA-Z]*$//').o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ build.sh -nt "$obj" ]; then echo "$src $obj"; fi
  done
}
compile() { # compile <out-dir> <flags...> < sources
  dir=$1; shift
  stale "$dir" | xargs -r -P "$JOBS" -n 2 sh -c 'echo "   cc $0"; '"$*"' -c "$0" -o "$1"'
}

echo "== registries (engine/modules and engine/sources category folders -> registry.h)"
python3 tests/tools/gen_registry.test.py
python3 tools/gen_registry.py
echo "== third-party objects (cached in build/obj)"
echo "$NAM_SRC" | compile build/obj/native "$CXX -O2 -std=c++20 -w $INC"
# WebAssembly: NAM core minus its file readers (no file system in the browser), plus the C++
# exception runtime pieces zig's WASI libc++ leaves out (see tools/wasm/README.md).
ZL="$(python3 -c 'import ziglang, os; print(os.path.join(os.path.dirname(ziglang.__file__), "lib"))')"
ABI="-D_LIBCXXABI_BUILDING_LIBRARY -DLIBCXXABI_USE_LLVM_UNWINDER -D_LIBCPP_BUILDING_LIBRARY -DHAS_THREAD_LOCAL -D_LIBCXXABI_HAS_NO_THREADS -I$ZL/libcxxabi/include -I$ZL/libunwind/include -I$ZL/libcxx/src"
(echo "$NAM_SRC" | grep -v -e '/nam_file.cpp' -e '/wav.cpp'; echo tools/wasm/nam_no_files.cpp) \
  | compile build/obj/wasm "$ZIGCXX $WASM -std=c++20 -O3 -msimd128 -w $INC -include tools/wasm/nam_compat.h"
mkdir -p build/obj/wasm-rt
printf '%s\n' "$ZL/libcxxabi/src/cxa_exception.cpp" "$ZL/libcxxabi/src/cxa_personality.cpp" \
  | compile build/obj/wasm-rt "$ZIGCXX $WASM -O2 -w $ABI"
echo "$ZL/libunwind/src/Unwind-wasm.c" | compile build/obj/wasm-rt "python3 -m ziglang cc $WASM -O2 -w -D_LIBUNWIND_HAS_NO_THREADS -D_LIBUNWIND_HIDE_SYMBOLS -I$ZL/libunwind/include"
echo tools/wasm/cpp_exception.S | compile build/obj/wasm-rt "python3 -m ziglang cc -target wasm32-wasi -mexception-handling"
echo tools/wasm/wasi_none.c | compile build/obj/wasm-rt "python3 -m ziglang cc -target wasm32-wasi -O2 -Wall -Wextra -Werror"

STRICT="-std=c++20 -Wall -Wextra -Werror $INC"
echo "== native tests"
$CXX -O2 $STRICT -o build/tests tests/native/*.cpp build/obj/native/*.o
./build/tests
echo "== WebAssembly engine"
# exception runtime first, so it wins over zig's no-exceptions stubs
$ZIGCXX $WASM -mexec-model=reactor -O3 -msimd128 -g0 -s $STRICT -include tools/wasm/nam_compat.h \
  -o build/rig.wasm build/obj/wasm-rt/*.o engine/api/rig_api.cpp build/obj/wasm/*.o
echo "== UI unit tests (library, preset format, skins)"
node tests/ui/library.test.mjs
node tests/ui/preset.test.mjs
node tests/ui/skin.test.mjs
echo "== native vs WebAssembly parity"
$CXX -O2 $STRICT -o build/dump tests/parity/dump.cpp build/obj/native/*.o
./build/dump
node tests/parity/parity.mjs
echo "== web page"
python3 tools/build_web.py
node --check build/app.js
node --check ui/worklet/engine-worklet.js
echo "== done"
