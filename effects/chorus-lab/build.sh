#!/usr/bin/env sh
# Chorus Lab build script
# Copyright (C) 2026 KC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds bin/linux-x86_64/chorus-lab.clap and bin/windows-x86_64/chorus-lab.clap.
# Fetches the CLAP SDK headers (MIT) into ./clap if they are not present.
set -e
cd "$(dirname "$0")"
[ -d clap ] || git clone --depth 1 https://github.com/free-audio/clap.git
FLAGS="-std=c11 -O2 -Wall -Wextra -Wpedantic -Iclap/include"
mkdir -p bin/linux-x86_64 bin/windows-x86_64
gcc $FLAGS -fPIC -shared src/chorus-lab.c -o bin/linux-x86_64/chorus-lab.clap -lm
echo "built bin/linux-x86_64/chorus-lab.clap"
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
   x86_64-w64-mingw32-gcc $FLAGS -shared -static-libgcc src/chorus-lab.c \
      -o bin/windows-x86_64/chorus-lab.clap -lm
   echo "built bin/windows-x86_64/chorus-lab.clap"
else
   echo "x86_64-w64-mingw32-gcc not found; skipped Windows build"
fi
# Full judge (gate + fidelity + CPU objective): ./build.sh test
if [ "$1" = "test" ]; then
   ./eval/eval.sh
fi
