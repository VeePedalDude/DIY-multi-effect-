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
| `--in N\|NAME`, `--out N\|NAME` | the devices (a number from `--list`, or part of the name) |
| `--frames N` | buffer size: 64 is the default; try 32 for less latency, 128 if it crackles |
| `--periods N` | buffers in the queue (default 2) |
| `--rate N` | sample rate (default 48000, what NAM captures are made at) |
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
- **Threads:** the audio thread is the only one that touches the engine. The network thread queues
  commands and files (`spsc.h`, lock-free); the audio thread runs them between blocks, as the browser's
  worklet does, and queues replies and meters back. Running the engine's add/remove calls on a second
  thread instead is what the engine was designed for, but ThreadSanitizer finds races in it
  (`tests/api/protocol.cpp` built with `-fsanitize=thread`), so that waits for the engine to be fixed.
  Until then, adding a module or loading a capture runs between two audio blocks: a capture takes
  about 0.1 s to load, so expect a short gap in the sound when you load one, as in the browser.

## Linux (the iMac)

The same program builds on Linux (`./build.sh` makes `build/rig-host`) and uses ALSA, PulseAudio or
JACK (`--backend jack`). The boot-to-effects setup (systemd unit, kiosk browser) is the next step.

## Building

`./build.sh` builds both: `build/rig-host` (native, used by the tests) and `dist/rig-host.exe`
(cross-compiled with zig, the same compiler as the WebAssembly build). `tests/browser/host.cjs`
runs the host on a null audio device with a simulated loopback and drives it from the page;
`RIG_HOST_BIN="wine dist/rig-host.exe"` runs the same test against the Windows build under Wine.
