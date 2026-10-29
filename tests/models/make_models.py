#!/usr/bin/env python3
"""Writes the three test captures NAM core doesn't ship examples of: ConvNet, Linear and Sequential.
Weights are seeded random numbers: the files only have to load and play, not sound like an amp.
Run from anywhere: python3 tests/models/make_models.py"""
import json, os, random

here = os.path.dirname(os.path.abspath(__file__))
rnd = random.Random(1)
w = lambda n, s=0.3: [round(rnd.uniform(-s, s), 6) for _ in range(n)]

def convnet(channels, dilations):
    # per block: kernel-2 conv with bias (batchnorm off): 2*in*ch + ch; head: ch weights + 1 bias
    n = 0; cin = 1
    for _ in dilations: n += 2 * cin * channels + channels; cin = channels
    n += channels + 1
    return {"version": "0.5.4", "architecture": "ConvNet", "sample_rate": 48000,
            "metadata": {"name": "Test ConvNet", "loudness": -24.0},
            "config": {"channels": channels, "dilations": dilations, "batchnorm": False, "activation": "Tanh"},
            "weights": w(n, 0.5)}

def linear(taps, version="0.5.4"):
    ir = [0.0] * taps; ir[0] = 0.6; ir[3] = -0.2; ir[taps // 2] = 0.1
    return {"version": version, "architecture": "Linear", "sample_rate": 48000,
            "config": {"receptive_field": taps, "bias": False}, "weights": ir}

def sequential():
    a = convnet(4, [1, 2, 4, 8]); b = linear(32, "0.7.0")
    for m in (a, b): m.pop("sample_rate"); m["version"] = "0.7.0"
    return {"version": "0.7.0", "architecture": "Sequential", "sample_rate": 48000, "metadata": {},
            "config": {"models": [a, b]}, "weights": []}

for name, model in [("convnet.nam", convnet(8, [1, 2, 4, 8, 16, 32, 64, 128, 256])),
                    ("linear.nam", linear(256)), ("sequential.nam", sequential())]:
    with open(os.path.join(here, name), "w") as f: json.dump(model, f, separators=(",", ":"))
    print("wrote", name)
