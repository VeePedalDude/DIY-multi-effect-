# Linux boot-to-effects (next step)

The native host is **`hosts/rig-host`**: one program for Windows and Linux (ALSA, PulseAudio or JACK)
that runs the engine on the audio thread, serves the page, and keeps the library in `Documents/RIG01`
([README](../rig-host/README.md)).

What's left here is turning a Linux machine (the iMac) into a pedal that boots straight to the rig:
`systemd/` units that start JACK, `rig-host` and a kiosk browser pointed at `localhost`, and a
`setup.sh` for a fresh Debian install.
