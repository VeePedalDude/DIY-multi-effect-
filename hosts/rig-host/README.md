# rig-host: RIG/01 on your audio interface

The browser test bench runs the engine inside the browser's audio system, which adds 10 ms on a good
day and 20–40 ms on Windows. `rig-host` runs the same engine straight on the audio interface and
serves the same page to control it, so the browser only draws: it's no longer in the audio path.

## Windows: quick start

1. Download **`dist/rig-host.exe`** from the repository (one file, the page is built in).
2. Plug in the Scarlett 2i2, set it to 48 kHz in Focusrite Control, and close other programs that use it.
3. Run `rig-host.exe`. It opens the page in your browser (`http://127.0.0.1:8000/`). Press **Start**.

Windows SmartScreen may warn that the program is unrecognised (it isn't signed): **More info → Run anyway**.
The page loads three.js from the internet the first time, as the test bench does.

From a command prompt, to choose the interface and buffer size:

```
rig-host.exe --list
rig-host.exe --in scarlett --out scarlett --frames 64
```

| Option | What it does |
|---|---|
| `--list` | lists inputs and outputs, with numbers |
| `--in N\|NAME`, `--out N\|NAME` | the devices (a number from `--list`, or part of the name). Choose one and the other follows it onto the same interface; choose neither and the input follows the default output |
| `--frames N` | a fixed buffer size. Without it the host starts at 64 and doubles the buffer (up to 512) whenever the audio drops out |
| `--periods N` | buffers in the queue (default 2) |
| `--rate N` | sample rate (default: the interface's own, so nothing is resampled; set the interface to 48 kHz, what NAM captures are made at) |
| `--shared` | WASAPI shared mode instead of exclusive (higher latency; lets other programs play too) |
| `--riff` | start with the built-in test riff instead of the input |
| `--latency-test` | measure the round trip and exit (see below) |
| `--port N`, `--listen ADDR` | where the page is served (`--listen 0.0.0.0` to use a phone or tablet on the same network) |
| `--no-browser` | don't open the page |

### Exclusive mode

By default the host opens the interface in **WASAPI exclusive mode**: Windows' own low-latency path,
where the host talks to the driver directly and Windows' mixer stays out of the way. While it runs,
other programs can't play through that interface. If exclusive mode is refused (another program has
it, or Windows doesn't allow it), the host says so and uses shared mode. To allow it: Sound settings →
More sound settings → the 2i2 → Properties → Advanced → tick both "Exclusive Mode" boxes.

Focusrite's own ASIO driver can go a little lower still. ASIO needs Steinberg's SDK, which can't be
shipped in this repository under its license; it can be added as an optional build later.

### If it crackles, distorts or howls

The console (and the settings drawer) says what the host opened and anything that looks wrong:

- **Two devices.** If the input and output are on different hardware (the Scarlett in, the laptop's
  speakers out, or the laptop's microphone in), their clocks drift against each other, which crackles,
  and a microphone near the speakers feeds back through an amp sim and howls. The host now keeps both
  on one interface by default and warns if they still differ. To pick: `--list`, then `--in scarlett`.
- **Dropouts.** The audio thread counts every callback that comes later than the whole output buffer
  lasts: the interface ran dry, which you hear as a click or glitch. With three or more in two seconds
  the host reopens the device with twice the buffer (64 → 128 → 256 → 512 frames), prints
  `dropouts at 64 frames: buffer raised to 128`, and the drawer shows it. Once it's clean, that
  size can be fixed with `--frames`.
- **Resampling.** The host runs at the interface's own rate, so Windows doesn't resample anything in
  between; if that isn't 48 kHz it says so (NAM captures are made at 48 kHz).

## Measuring the round trip

Numbers on a datasheet aren't the latency you play through: the converters and the USB driver add
their own. To measure the real thing, connect **Output 1 to Input 1** with a short cable, turn the
input gain down low, and press **Measure round trip** in the settings drawer (or run
`rig-host.exe --latency-test`). The host plays five clicks and times each one coming back; the result
(median, in ms and samples) shows in the drawer and the top bar. Unplug the cable before you play.

## How it works

```
browser page ──WebSocket──▶ rig-host ──▶ engine (engine/api/rig_api.cpp) ──▶ WASAPI / ALSA / JACK ──▶ 2i2
```

- **Audio:** [miniaudio](../../third_party/README.md) opens the devices: WASAPI on Windows (with the
  "Pro Audio" thread priority), ALSA, PulseAudio or JACK on Linux. Denormals are flushed to zero in the
  audio thread.
- **Page:** `dist/rig-test.html`, built into the program by `tools/embed_page.py`, served with
  `window.RIG_HOST` set. `ui/js/31-host.js` then connects to `/ws` instead of starting browser audio;
  the rest of the page works unchanged.
- **Commands:** the same text protocol as the browser ([PROTOCOL.md](../../docs/PROTOCOL.md)), parsed
  by the engine itself (`rig_exec`), so the two can't drift apart. Two more for the host: `host_input
  riff|interface` and `latency_test`.
- **Threads** (the way the NAM plugin and ToobAmp load models):
  - *audio* runs the engine, and between blocks the queued commands (`spsc.h`, lock-free), as the
    browser's worklet does. The engine's add/remove calls stay here: ThreadSanitizer finds races in them
    beside `process()` on another thread (`tests/api/protocol.cpp` built with `-fsanitize=thread`).
  - *loader* builds NAM captures and IRs. Parsing and building a model takes 10–100 ms, far longer than
    an audio block, so it never happens on the audio thread: the loader builds it into the module's
    `ModelSlot` (`engine/core/model_slot.h`) and the audio thread only picks up the finished model (a
    short warm-up on your playing, then a 10 ms crossfade). While a file loads, commands that add,
    remove or move modules wait their turn, so the module can't go away underneath it; knobs and
    patches carry on. ThreadSanitizer finds no races on this path, or in the whole host driven by
    `tests/browser/host.cjs`.
  - *network* is the page's WebSocket; it queues work for the other two and sends their answers back.
- **Loading a capture while you play:** before, the audio thread spent 24–32 ms building it, a dropout
  at any buffer size. Now the audio thread's busiest block while captures load takes under 1 ms of its
  own CPU on the test machine (the loader does the work beside it); the meters report the slowest
  callback (`worstMs`, and `worstCpuMs` on Linux) so you can watch for spikes on your machine.

## Linux (the iMac)

The same program builds on Linux (`./build.sh` makes `build/rig-host`) and uses ALSA, PulseAudio or
JACK (`--backend jack`). The boot-to-effects setup (systemd unit, kiosk browser) is the next step.

## Building

`./build.sh` builds both: `build/rig-host` (native, used by the tests) and `dist/rig-host.exe`
(cross-compiled with zig, the same compiler as the WebAssembly build). `tests/browser/host.cjs`
runs the host on a null audio device with a simulated loopback and drives it from the page;
`RIG_HOST_BIN="wine dist/rig-host.exe"` runs the same test against the Windows build under Wine.
