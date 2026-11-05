# Chorus Lab judge: test signals (READ-ONLY once locked)
# Copyright (C) 2026 KC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Deterministic generators (fixed seeds). Realistic guitar material plus hard cases.
import numpy as np
from scipy import signal


def pluck(f0, dur, sr, decay=0.996, bright=0.5, seed=0):
    """Karplus-Strong plucked string."""
    rng = np.random.default_rng(seed)
    n = int(dur * sr)
    L = int(round(sr / f0))
    buf = signal.lfilter([bright, 1 - bright], [1], rng.uniform(-1, 1, L))
    out = np.zeros(n)
    for i in range(n):
        v = buf[i % L]
        out[i] = v
        buf[i % L] = decay * 0.5 * (v + buf[(i + 1) % L])
    return out


def stereo(x):
    return np.stack([x, x], 1).astype(np.float32)


def guitar_phrase(sr, seed=1):
    """Single notes, a chord, palm mutes, a high lead with vibrato, then decay into silence."""
    parts = []
    for k, f in enumerate([110.0, 146.8, 196.0, 246.9]):
        parts.append(pluck(f, 0.35, sr, seed=seed + k) * 0.5)
    chord = sum(pluck(f, 1.6, sr, decay=0.998, seed=seed + 10 + i)
                for i, f in enumerate([82.4, 123.5, 164.8, 207.7, 246.9, 329.6])) * 0.22
    parts.append(chord)
    for k in range(6):
        pm = pluck(82.4, 0.14, sr, decay=0.97, bright=0.9, seed=seed + 20 + k) * 0.6
        parts.append(signal.lfilter(*signal.butter(2, 900 / (sr / 2)), pm))
    n = int(1.2 * sr)
    t = np.arange(n) / sr
    f = 880 * (1 + 0.006 * np.sin(2 * np.pi * 5.5 * t))
    lead = 0.4 * np.sin(2 * np.pi * np.cumsum(f) / sr) * np.exp(-t * 1.2)
    lead += 0.12 * np.sin(4 * np.pi * np.cumsum(f) / sr) * np.exp(-t * 1.8)
    parts.append(lead)
    parts.append(np.zeros(int(1.5 * sr)))
    return stereo(np.concatenate(parts))


def sustained_chord(sr, dur=4.0):
    ch = sum(pluck(f, dur, sr, decay=0.9995, seed=50 + i)
             for i, f in enumerate([110, 164.8, 220, 277.2])) * 0.2
    return stereo(ch)


def sine(f, dur, sr, amp=0.5):
    t = np.arange(int(dur * sr)) / sr
    return stereo(amp * np.sin(2 * np.pi * f * t))


def square_fullscale(sr, dur=2.0, f=110.0):
    t = np.arange(int(dur * sr)) / sr
    s = np.sign(np.sin(2 * np.pi * f * t))
    s[s == 0] = 1
    return stereo(s)


def dc_offset(sr, dur=2.0):
    return (guitar_phrase(sr)[: int(dur * sr)] + 0.5).astype(np.float32)


def denormals(sr, dur=1.0):
    d = np.full((int(dur * sr), 2), 1e-39, np.float32)
    d[::2] *= -1
    return d


def silence(sr, dur=1.0):
    return np.zeros((int(dur * sr), 2), np.float32)
