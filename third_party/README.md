# third_party

Vendored libraries, copied unchanged so a fresh clone builds offline. Don't edit files here: the
few WebAssembly differences are shims in `tools/wasm/`, and our own checks live in `engine/core/nam.h`.

| Folder | What | Version | License |
|---|---|---|---|
| `NeuralAmpModelerCore/` | [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) by Steven Atkinson: plays every `.nam` architecture (WaveNet A1 and A2, LSTM, ConvNet, Linear, Sequential, SlimmableContainer), and its `nam::Linear` is a zero-latency partitioned FFT convolver we use for IRs | 0.6.0, commit `0b3d3c9` (2026-09-18); `NAM/` folder and `LICENSE` only | MIT (`NeuralAmpModelerCore/LICENSE`) |
| `eigen/` | [Eigen](https://eigen.tuxfamily.org) linear algebra, used by NAM core: `Eigen/` and `unsupported/Eigen/FFT` only | 3.4.0 (Debian/Ubuntu `libeigen3-dev` 3.4.0-4) | MPL-2.0 (`eigen/COPYING.MPL2`, `eigen/COPYRIGHT`) |
| `nlohmann/` | [nlohmann/json](https://github.com/nlohmann/json), single header, used by NAM core to read `.nam` files | 3.12.0 (as bundled by NAM core) | MIT (SPDX header in `json.hpp`) |

## About the Eigen version

NAM core 0.6.0 pins Eigen 5.0.1 as a git submodule from gitlab.com. Eigen 3.4.0 was used here
instead. NAM core's own test suite (`tools/run_tests.cpp`, every test) passes against it, and our
tests check that the amp's output is sample-identical to NAM core run directly. Moving to Eigen 5
later is a drop-in swap of `eigen/`. It may run faster in WebAssembly; measure before and after.

## Updating NAM core

1. Copy the new `NAM/` folder and `LICENSE` over `NeuralAmpModelerCore/`.
2. `./build.sh` (the cached objects in `build/obj/` rebuild automatically) and the browser tests.
3. Check `tools/wasm/nam_compat.h` and `nam_no_files.cpp` still cover what the WebAssembly build
   needs, and that `engine/core/nam.h`'s LSTM weight check still matches `NAM/lstm.cpp`.

## Known issue in NAM core (worth reporting upstream)

NAM core trusts each file's weight count. Except for `Linear`, the loaders read the weights list
without a bounds check (AddressSanitizer confirms reads past the end with too few weights). A
cut-off download is invalid JSON and is refused before that point, and `engine/core/nam.h` checks
LSTM counts (where NAM core only has a debug `assert`). A well-formed file with too few weights
for another architecture could still read past the list.

A second one: NAM core reads `version`, `architecture` and `config` from a const JSON object without
checking they exist. In release builds that is undefined behaviour in nlohmann/json: usually an
exception, occasionally a crash (1 run in 25 of our test suite, on a test file with no `config`).
`engine/core/nam.h` (`checkShape`) checks every model in a file has them first, and
`engine/core/json_assert.h` (force-included by build.sh) turns nlohmann's precondition checks into
exceptions. After the fix: 0 crashes in 300 runs of that test.
