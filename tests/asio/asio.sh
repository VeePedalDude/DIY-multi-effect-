#!/usr/bin/env bash
# tests/asio/asio.sh - rig-host's ASIO path, end to end, under Wine: register the stand-in driver
# (build/rigtest_asio.dll, built by build.sh from tests/asio/rigtest_asio.cpp), then run
# dist/rig-host.exe on it. Its outputs are cabled back to its inputs 437 samples later, so the
# round-trip test must measure exactly 437 in every sample format a driver may use.
# Needs wine (64-bit prefix); WINEPREFIX as you like. Then, for the page as well:
#   RIG_HOST_BIN="wine dist/rig-host.exe" RIG_HOST_AUDIO="--asio rig" node tests/browser/host.cjs
set -euo pipefail
cd "$(dirname "$0")/../.."
export WINEDEBUG=-all
CLSID='{5A1F0D3E-6C2B-4E8A-9B71-2F4D8C3A9E10}'
DLL="$(winepath -w build/rigtest_asio.dll)"
wine reg add 'HKLM\Software\ASIO\RIG Test ASIO' /v CLSID /t REG_SZ /d "$CLSID" /f /reg:64 >/dev/null
wine reg add 'HKLM\Software\ASIO\RIG Test ASIO' /v Description /t REG_SZ /d 'RIG Test ASIO' /f /reg:64 >/dev/null
wine reg add "HKCR\\CLSID\\$CLSID\\InprocServer32" /ve /t REG_SZ /d "$DLL" /f /reg:64 >/dev/null
wine reg add "HKCR\\CLSID\\$CLSID\\InprocServer32" /v ThreadingModel /t REG_SZ /d Apartment /f /reg:64 >/dev/null

fail=0
wine dist/rig-host.exe --list 2>&1 | tr -d '\r' | grep -q 'RIG Test ASIO' || { echo "  [FAIL] --list doesn't show the ASIO driver"; fail=1; }
[ $fail = 0 ] && echo "  [PASS] --list shows the ASIO driver"
for fmt in int32 int24 int16 float; do
  out=$(RIGTEST_ASIO_FORMAT=$fmt timeout 60 wine dist/rig-host.exe --asio "rig test" --latency-test 2>&1 | tr -d '\r')
  if grep -q '"samples":437' <<<"$out" && grep -q 'audio:   ASIO' <<<"$out"; then
    echo "  [PASS] $fmt samples: round trip 437 samples through ASIO ($(grep -o 'audio:.*' <<<"$out" | head -1))"
  else echo "  [FAIL] $fmt samples:"; echo "$out" | sed 's/^/        /'; fail=1; fi
done
out=$(RIGTEST_ASIO_FORMAT=int32 timeout 60 wine dist/rig-host.exe --asio "rig test" --frames 64 --latency-test 2>&1 | tr -d '\r')
if grep -q '"samples":437' <<<"$out" && grep -q '64 frames' <<<"$out"; then echo "  [PASS] --frames 64 asks the driver for 64"; else echo "  [FAIL] --frames 64:"; echo "$out" | sed 's/^/        /'; fail=1; fi
out=$(timeout 60 wine dist/rig-host.exe --backend wasapi --latency-test 2>&1 | tr -d '\r' || true)
if grep -q 'audio:   ASIO' <<<"$out"; then echo "  [FAIL] --backend wasapi still opened ASIO"; fail=1; else echo "  [PASS] --backend wasapi leaves ASIO alone"; fi
out=$(RIGTEST_ASIO_SILENT=1 timeout 60 wine dist/rig-host.exe --asio "rig test" --latency-test 2>&1 | tr -d '\r' || true)
if grep -q "isn't passing audio, so using Windows' own audio path" <<<"$out"; then echo "  [PASS] a driver that never calls back: the host says so and moves to WASAPI"; else echo "  [FAIL] silent driver:"; echo "$out" | sed 's/^/        /'; fail=1; fi
# The same driver registered a second time as "FL Studio ASIO" (a wrapper: it sorts first, and must be passed over)
wine reg add 'HKLM\Software\ASIO\FL Studio ASIO' /v CLSID /t REG_SZ /d "$CLSID" /f /reg:64 >/dev/null
out=$(timeout 60 wine dist/rig-host.exe --latency-test 2>&1 | tr -d '\r' || true)
wine reg delete 'HKLM\Software\ASIO\FL Studio ASIO' /f /reg:64 >/dev/null 2>&1 || true
if grep -q 'audio:   ASIO' <<<"$out" && grep -q 'input:   RIG Test ASIO' <<<"$out" && grep -q '"samples":437' <<<"$out"; then
  echo "  [PASS] by default: a driver that doesn't match the interface's name is used (like a first-gen Scarlett's), FL Studio ASIO passed over"
else echo "  [FAIL] default choice:"; echo "$out" | sed 's/^/        /'; fail=1; fi
exit $fail
