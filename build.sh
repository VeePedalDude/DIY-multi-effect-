#!/bin/sh
# build.sh - build and test everything. Stops at the first failure.
#   native tests -> WebAssembly engine -> parity check -> dist/rig-test.html
set -e
cd "$(dirname "$0")"
mkdir -p build dist
CXX="${CXX:-g++}"
echo "== native tests"
$CXX -O2 -std=c++17 -Wall -Wextra -Werror -o build/tests tests/native/*.cpp
./build/tests
echo "== WebAssembly engine"
export ZIG_GLOBAL_CACHE_DIR="$(mktemp -d)" ZIG_LOCAL_CACHE_DIR="$(mktemp -d)"   # zig cache can serve stale builds
python3 -m ziglang c++ -target wasm32-wasi -mexec-model=reactor -O3 -msimd128 \
  -fno-exceptions -fno-rtti -g0 -s -Wall -Wextra -Werror -o build/rig.wasm engine/api/rig_api.cpp
echo "== native vs WebAssembly parity"
$CXX -O2 -std=c++17 -Wall -Wextra -Werror -o build/dump tests/parity/dump.cpp
./build/dump
node tests/parity/parity.mjs
echo "== web page"
python3 tools/build_web.py
node --check build/app.js
node --check ui/worklet/engine-worklet.js
echo "== done"
