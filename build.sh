#!/bin/sh
# build.sh - native tests + WebAssembly engine + single-file web rig
set -e
cd "$(dirname "$0")"
mkdir -p build dist
g++ -O2 -std=c++17 -Wall -Wextra -Werror -o test/harness test/harness.cpp
g++ -O2 -std=c++17 -Wall -Wextra -o test/dump test/dump.cpp
export ZIG_GLOBAL_CACHE_DIR="$(mktemp -d)" ZIG_LOCAL_CACHE_DIR="$(mktemp -d)"   # zig cache can serve stale builds
python3 -m ziglang c++ -target wasm32-wasi -mexec-model=reactor -O3 -msimd128 \
  -fno-exceptions -fno-rtti -g0 -s -Wall -Wextra -Werror -o build/rig.wasm dsp/rig_api.cpp
python3 web/build_web.py
./test/harness
./test/dump && node test/parity.mjs
