# Linux host (next step)

`rig-host` will be a small native program for the iMac that:

1. runs the same `engine/` code as a JACK client at 64–128 samples (the 5–10 ms latency target),
2. serves `dist/rig-test.html` and accepts the same text commands over a WebSocket
   (see `docs/PROTOCOL.md`), and
3. starts at boot with JACK and a kiosk browser pointed at `localhost`, and
4. keeps the library as a real folder (`~/RIG Library/ir`, `nam`, `presets`) and loads files with
   `asset_load <inst> <kind>/<file>` (see `docs/LIBRARY.md`).

It links `engine/api/rig_api.cpp`, so every module and source added to the engine works
here with no extra code. Planned files: `main.cpp` (JACK client + WebSocket), `systemd/`
units, and `setup.sh` for a fresh Debian install.
