#!/usr/bin/env bash
# Chorus Lab judge (READ-ONLY once locked)
# Copyright (C) 2026 KC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One command: builds the effect, runs every gate check, prints report lines,
# then exactly:
#   GATE=PASS|FAIL
#   OBJECTIVE=<mean CPU % of one core across the 8 voicings, lower is better>
#
# Needs: gcc, x86_64-w64-mingw32-gcc, python3 + numpy + scipy, git.
# clap-validator: $CLAP_VALIDATOR, or `clap-validator` on PATH, or it is built
# from source into ../.tools (needs cargo). CLAP SDK headers: ../clap (fetched if missing).
set -u
EVAL="$(cd "$(dirname "$0")" && pwd)"
FX="$(dirname "$EVAL")"
B="$FX/build"
LOG="$B/eval.log"
mkdir -p "$B/linux" "$B/win" "$B/work"
: > "$LOG"
FAIL=0
pass() { echo "CHECK PASS $*"; }
fail() { echo "CHECK FAIL $*"; FAIL=1; }

[ -d "$FX/clap" ] || git clone -q --depth 1 https://github.com/free-audio/clap.git "$FX/clap" >>"$LOG" 2>&1
VAL="${CLAP_VALIDATOR:-$(command -v clap-validator || true)}"
if [ -z "$VAL" ]; then
   if [ ! -x "$FX/.tools/clap-validator/target/release/clap-validator" ]; then
      git clone -q --depth 1 https://github.com/free-audio/clap-validator.git "$FX/.tools/clap-validator" >>"$LOG" 2>&1
      (cd "$FX/.tools/clap-validator" && cargo build --release -q) >>"$LOG" 2>&1
   fi
   VAL="$FX/.tools/clap-validator/target/release/clap-validator"
fi

SRC="$FX/src/chorus-lab.c"
STRICT="-std=c11 -O2 -Wall -Wextra -Wpedantic -I$FX/clap/include"

# 1. Builds with zero warnings
W=$(gcc $STRICT -fPIC -shared "$SRC" -o "$B/linux/chorus-lab.clap" -lm 2>&1); RC=$?
echo "$W" >>"$LOG"
if [ $RC -eq 0 ] && [ -z "$W" ]; then pass "linux gcc build, zero warnings"; else fail "linux gcc build : $(echo "$W" | head -3)"; fi
W=$(x86_64-w64-mingw32-gcc $STRICT -shared -static-libgcc "$SRC" -o "$B/win/chorus-lab.clap" -lm 2>&1); RC=$?
echo "$W" >>"$LOG"
if [ $RC -eq 0 ] && [ -z "$W" ]; then pass "windows mingw build, zero warnings"; else fail "windows mingw build : $(echo "$W" | head -3)"; fi

HOSTSRC="$EVAL/host.c"
gcc -std=c11 -O2 -I"$FX/clap/include" "$HOSTSRC" -o "$B/host" -ldl -lpthread -lm >>"$LOG" 2>&1 || fail "build judge host"
SAN="-std=c11 -O1 -g -fno-omit-frame-pointer -I$FX/clap/include"
gcc $SAN -fsanitize=address,undefined -fno-sanitize-recover=all -fPIC -shared "$SRC" -o "$B/linux/chorus-lab-asan.clap" -lm >>"$LOG" 2>&1 &&
gcc $SAN -fsanitize=address,undefined -fno-sanitize-recover=all "$HOSTSRC" -o "$B/host-asan" -ldl -lpthread -lm >>"$LOG" 2>&1 || fail "build ASan/UBSan binaries"
gcc $SAN -fsanitize=thread -fPIC -shared "$SRC" -o "$B/linux/chorus-lab-tsan.clap" -lm >>"$LOG" 2>&1 &&
gcc $SAN -fsanitize=thread "$HOSTSRC" -o "$B/host-tsan" -ldl -lpthread -lm >>"$LOG" 2>&1 || fail "build TSan binaries"

# 2. clap-validator
V=$("$VAL" validate "$B/linux/chorus-lab.clap" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | tail -1)
echo "$V" >>"$LOG"
if echo "$V" | grep -qE " 0 failed, 0 warnings"; then pass "clap-validator : $V"; else fail "clap-validator : $V"; fi

# 3. AddressSanitizer + UBSan
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
SOUT=$( { "$B/host-asan" "$B/linux/chorus-lab-asan.clap" state && "$B/host-asan" "$B/linux/chorus-lab-asan.clap" threads &&
          python3 "$EVAL/gate.py" "$B/host-asan" "$B/linux/chorus-lab-asan.clap" "$B/work/asan" --gate-only; } 2>&1 ); RC=$?
echo "$SOUT" >>"$LOG"
if [ $RC -eq 0 ] && ! echo "$SOUT" | grep -qiE "AddressSanitizer|LeakSanitizer|runtime error"; then
   pass "ASan+UBSan (state, threads, gate signals) : no errors or leaks"
else fail "ASan+UBSan : $(echo "$SOUT" | grep -iE "Sanitizer|runtime error|FAIL" | head -3)"; fi
unset ASAN_OPTIONS UBSAN_OPTIONS

# 4. ThreadSanitizer (3 runs)
TOUT=""; RC=0
for i in 1 2 3; do T=$(TSAN_OPTIONS=halt_on_error=1 "$B/host-tsan" "$B/linux/chorus-lab-tsan.clap" threads 2>&1) || RC=1; TOUT="$TOUT$T"; done
echo "$TOUT" >>"$LOG"
if [ $RC -eq 0 ] && ! echo "$TOUT" | grep -q "ThreadSanitizer"; then pass "ThreadSanitizer x3 : no races"; else fail "ThreadSanitizer : $(echo "$TOUT" | grep -m2 ThreadSanitizer)"; fi

# 5. Signal gate, fidelity, report, objective
GOUT=$(python3 "$EVAL/gate.py" "$B/host" "$B/linux/chorus-lab.clap" "$B/work/rel" 2>&1)
echo "$GOUT" >>"$LOG"
echo "$GOUT" | grep -E "^(CHECK|FIDELITY|REPORT)"
echo "$GOUT" | grep -q "^GATE=PASS" || FAIL=1
OBJ=$(echo "$GOUT" | sed -n 's/^OBJECTIVE=//p')
[ -n "$OBJ" ] || { OBJ=nan; FAIL=1; echo "CHECK FAIL gate.py did not finish : $(echo "$GOUT" | tail -3)"; }

if [ $FAIL -eq 0 ]; then echo "GATE=PASS"; else echo "GATE=FAIL"; fi
echo "OBJECTIVE=$OBJ"
