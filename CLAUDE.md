# RIG/01 multi-effect

Read README.md first, especially "For AI assistants working on this repo".

- All DSP lives in `dsp/rig.h`; `web/` is the UI and browser engine bridge; `dist/rig-test.html` is the built, committed test bench.
- Every change runs through `./build.sh` (native harness must report zero failures, compiled with -Wall -Wextra -Werror) and `./test/dump && node test/parity.mjs` for native/WASM agreement.
- Browser tests: `THREE_JS=<path to three.min.js r128> PLAYWRIGHT_CORE=<path> node test/browser.cjs` and `test/blocked.cjs`.
- Report measured numbers before and after a DSP change; keep tests and add one for each new module or source.
- Rebuild `dist/rig-test.html` and commit it with every change so it can be downloaded straight from GitHub.
