# Chorus Lab judge, v1.1 (READ-ONLY once locked)
# v1.1: adds the Drive/Compander/Emphasis/Low Cut/Mono Bass/Lush/Shift params to the
# gate (extremes, modulation, buffer-size), plus one measured check per feature.
# The fidelity refs in ref/ are unchanged: with the v1.1 controls at their defaults
# the output must still null against the v1.0 renders.
# Copyright (C) 2026 KC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: python3 gate.py HOST PLUGIN WORKDIR [--make-refs]
# Prints CHECK lines, report lines, then GATE=PASS|FAIL and OBJECTIVE=<number>.
import hashlib, json, os, platform, subprocess, sys
import numpy as np
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import signals as S  # noqa: E402

HOST, PLUG, WORK = sys.argv[1], sys.argv[2], sys.argv[3]
MAKE_REFS = "--make-refs" in sys.argv
QUICK = "--gate-only" in sys.argv          # used for the sanitizer pass (no refs/objective)
REF = os.path.join(HERE, "ref")
os.makedirs(WORK, exist_ok=True)

VOICINGS = ["Classic", "Dimension", "Ensemble", "Tri-Chorus", "Vibrato", "Detune", "Lo-Fi", "Random"]
BLOCKS = ["32", "37", "64", "128", "256", "333", "512"]
FIDELITY_LIMIT_DB = -60.0      # residual vs locked baseline render, per render
PEAK_BOUND = 16.0              # hard cases: output peak must stay below this (+24 dBFS)
OBJ_SR, OBJ_SECONDS, OBJ_BLOCK, OBJ_REPS = 48000, 20.0, "64", 11
OBJ_PARAMS = "5=30"            # feedback 30 % so the feedback path is exercised; other params default

fails = []
ALL_ON = "5=50,9=40,10=70,11=60,12=150,13=120,14=60,15=7"


def check(cond, name, detail=""):
    print(f"CHECK {'PASS' if cond else 'FAIL'} {name}{(' : ' + detail) if detail else ''}")
    if not cond:
        fails.append(name)


def p(**kw):
    ids = dict(voicing=0, rate=1, depth=2, mix=3, tone=4, fb=5, width=6, time=7, level=8,
               drive=9, comp=10, emph=11, lowcut=12, mono=13, lush=14, shift=15)
    return ",".join(f"{ids[k]}={v}" for k, v in kw.items())


def run(x, sr=48000, block="128", events=None, params=""):
    fin = os.path.join(WORK, "in.f32")
    fout = os.path.join(WORK, "out.f32")
    np.ascontiguousarray(x, dtype=np.float32).tofile(fin)
    evf = "-"
    if events:
        evf = os.path.join(WORK, "ev.txt")
        with open(evf, "w") as f:
            for t, k, i, v in sorted(events, key=lambda e: e[0]):
                f.write(f"{t} {k} {i} {v!r}\n")
    r = subprocess.run([HOST, PLUG, "render", str(sr), fin, fout, block, evf, params],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"host failed ({r.returncode}): {r.stderr[-2000:]}")
    y = np.fromfile(fout, dtype=np.float32).reshape(-1, 2)
    vals = {int(k): float(v) for k, v in (kv.split("=") for kv in r.stdout.strip().split(","))}
    tail = int([l for l in r.stderr.splitlines() if l.startswith("tail=")][0].split("=")[1])
    return y, vals, tail


def db(x):
    return 20 * np.log10(np.maximum(x, 1e-15))


def rms(x):
    return float(np.sqrt(np.mean(np.square(x, dtype=np.float64))))


# ------------------------------------------------------------------ fidelity renders
def fidelity_cases():
    """(name, input, sr, block, events, params) rendered by the locked baseline into ref/."""
    cases = []
    g48 = S.guitar_phrase(48000)[: 2 * 48000]
    for v, n in enumerate(VOICINGS):
        cases.append((f"g48_{v}", g48, 48000, "128", None,
                      p(voicing=v, rate=1.3, depth=70, mix=60, tone=60, fb=30, width=80, time=110)))
    g44 = S.guitar_phrase(44100)[: 44100]
    for v, n in enumerate(VOICINGS):
        cases.append((f"g44_{v}", g44, 44100, "128", None, p(voicing=v, fb=30)))
    rng = np.random.default_rng(21)
    ev = [(t, "M", i, float(rng.uniform(-60, 60))) for t in range(0, 48000, 11) for i in (1, 2, 3, 4, 5, 6, 7, 8)]
    cases.append(("mod48", g48[:48000], 48000, "128", ev, p(voicing=2, depth=40, fb=20)))
    ch = S.sustained_chord(48000, 1.6)
    order = [1, 4, 6, 5, 0, 2, 7, 3]
    sw = [(int((k + 1) * 0.18 * 48000), "V", 0, float(v)) for k, v in enumerate(order)]
    cases.append(("switch48", ch, 48000, "128", sw, p(depth=60, fb=25)))
    return cases


def make_refs():
    os.makedirs(REF, exist_ok=True)
    man = {}
    for name, x, sr, block, ev, prm in fidelity_cases():
        y, _, _ = run(x, sr, block, ev, prm)
        y.astype("<f4").tofile(os.path.join(REF, name + ".f32"))
        man[name] = dict(sr=sr, frames=len(y), params=prm, sha256=hashlib.sha256(y.tobytes()).hexdigest())
    src = os.path.join(os.path.dirname(HERE), "src", "chorus-lab.c")
    man["_baseline_source_sha256"] = hashlib.sha256(open(src, "rb").read()).hexdigest()
    json.dump(man, open(os.path.join(REF, "manifest.json"), "w"), indent=1)
    print(f"wrote {len(man) - 1} reference renders to {REF}")


# ------------------------------------------------------------------ gate checks
def gate_block_invariance():
    x = S.guitar_phrase(48000)[: 3 * 48000]
    rng = np.random.default_rng(4)
    ev = [(int(t), "M", int(rng.integers(1, 9)), float(rng.uniform(-50, 50)))
          for t in sorted(rng.integers(0, len(x), 300))]
    for t in range(0, len(x), 9000):
        ev.append((t, "V", 0, float(rng.integers(0, 8))))
        ev.append((t + 3, "V", 2, float(rng.uniform(0, 100))))
    ev += [(int(t), "M", int(rng.integers(9, 16)), float(rng.uniform(-30, 30)))
           for t in sorted(rng.integers(0, len(x), 200))]
    for label, prm in (("v1.1 stages off", p(fb=40)), ("all v1.1 stages on", ALL_ON)):
        ref = None
        bad = []
        for b in BLOCKS:
            y, _, _ = run(x, 48000, b, ev, prm)
            if ref is None:
                ref = y
            elif not np.array_equal(y.view(np.uint32), ref.view(np.uint32)):
                bad.append(b)
        check(not bad, f"bit-identical across buffer sizes {','.join(BLOCKS)} ({label})",
              f"{len(ev)} events" + (f"; mismatched: {bad}" if bad else ""))


def gate_finite_bounded_silence():
    sr = 48000
    hard = {"square": S.square_fullscale(sr), "dc": S.dc_offset(sr), "guitar": S.guitar_phrase(sr)[: 2 * sr]}
    extremes = [p(rate=10, depth=100, mix=100, tone=100, fb=90, width=100, time=200, level=12),
                p(rate=0.05, depth=0, mix=0, tone=0, fb=-90, width=0, time=50, level=-24),
                p(rate=10, depth=100, mix=50, tone=0, fb=-90, width=100, time=50, level=0),
                p(rate=10, depth=100, mix=100, tone=100, fb=90, width=100, time=200, drive=100, comp=100,
                  emph=100, lowcut=800, mono=400, lush=100, shift=12, level=12),
                p(rate=0.05, depth=0, mix=50, tone=0, fb=-90, width=0, time=50, drive=100, comp=100,
                  emph=100, lowcut=20, mono=20, lush=100, shift=-12, level=0)]
    nonfinite, worst, dc_grew = [], 0.0, []
    for v in range(8):
        for hn, x in hard.items():
            for e in extremes:
                prm = f"0={v}," + e
                y, _, _ = run(x, sr, "128", params=prm)
                if not np.all(np.isfinite(y)):
                    nonfinite.append((v, hn, e))
                    continue
                worst = max(worst, float(np.max(np.abs(y))))
                if hn == "dc":
                    lvl = 10 ** (float([kv for kv in e.split(",") if kv.startswith("8=")][0][2:]) / 20)
                    m_in, m_out = float(np.mean(x[-sr // 2:])), float(np.mean(y[-sr // 2:]))
                    if abs(m_out) > abs(m_in) * lvl * 2.0 + 0.05:
                        dc_grew.append((v, e))
    check(not nonfinite, "all outputs finite (hard cases x extreme params x 8 voicings)", f"{len(nonfinite)} bad")
    check(worst < PEAK_BOUND, "hard cases bounded", f"worst peak {worst:.2f} (limit {PEAK_BOUND})")
    check(not dc_grew, "DC does not build up through feedback", f"{len(dc_grew)} bad")
    sil_bad, sub_bad = [], []
    for v in range(8):
        for e in extremes:
            y, _, _ = run(S.silence(sr), sr, "128", params=f"0={v}," + e)
            if not np.all(y == 0):
                sil_bad.append((v, e))
            y, _, _ = run(S.denormals(sr), sr, "128", params=f"0={v}," + e)
            if np.any((y != 0) & (np.abs(y) < 1.1754944e-38)) or not np.all(np.isfinite(y)):
                sub_bad.append((v, e))
    check(not sil_bad, "silence in gives exact silence out", f"{len(sil_bad)} bad")
    check(not sub_bad, "denormal input gives no subnormal output", f"{len(sub_bad)} bad")


def gate_mod_knobs():
    x = S.guitar_phrase(48000)[: 2 * 48000]
    knobs = p(voicing=2, rate=1.5, depth=40, mix=60, tone=55, fb=10, width=80, time=120, level=-3,
              drive=30, comp=50, emph=40, lowcut=100, mono=90, lush=50, shift=5)
    rng = np.random.default_rng(9)
    ev = [(t, "M", i, float(rng.uniform(-200, 200))) for t in range(0, len(x), 7) for i in range(1, 16)]
    y, vals, _ = run(x, 48000, "97", ev, knobs)
    want = {int(k): float(v) for k, v in (kv.split("=") for kv in knobs.split(","))}
    ok = all(abs(vals[k] - want[k]) < 1e-12 for k in want) and np.all(np.isfinite(y))
    check(ok, "modulation leaves knob values unchanged", f"{len(ev)} PARAM_MOD events")


def gate_state():
    r = subprocess.run([HOST, PLUG, "state"], capture_output=True, text=True)
    out = r.stdout.strip()
    check(r.returncode == 0 and "STATE PASS" in out and "17 bytes" in out,
          "state round-trip (17-byte reads) and garbage rejected", out.replace("\n", " | "))


# ------------------------------------------------------------------ v1.1 feature checks
def tone_amp(y, f, sr):
    seg = y.astype(np.float64)
    t = np.arange(len(seg)) / sr
    return 2 * np.hypot(np.mean(seg * np.sin(2 * np.pi * f * t)), np.mean(seg * np.cos(2 * np.pi * f * t)))


def thd(y, f0, sr):
    s_ = y.astype(np.float64)
    Sp = np.abs(np.fft.rfft(s_ * signal.windows.blackmanharris(len(s_)))) ** 2
    fr = np.fft.rfftfreq(len(s_), 1 / sr)
    fund = np.abs(fr - f0) < 20
    band = (fr > 20) & (fr < 20000)
    return float(np.sqrt(Sp[band & ~fund].sum() / Sp[fund].sum())), Sp, fr


def gate_features():
    sr = 48000
    g = S.guitar_phrase(sr)[: 4 * sr]
    # Drive: loudness held within +/-3 dB, harmonics and an even (2nd) harmonic appear
    base = rms(run(g, sr, params=p(mix=0))[0])
    lv = []
    for d in (50, 100):
        lv.append(db(rms(run(g, sr, params=p(mix=0, drive=d))[0])) - db(base))
    sn = S.sine(1000, 1.0, sr, 0.25)
    y0 = run(sn, sr, params=p(mix=0))[0][sr // 4:, 0]
    y1 = run(sn, sr, params=p(mix=0, drive=100))[0][sr // 4:, 0]
    t0, _, _ = thd(y0, 1000, sr)
    t1, Sp, fr = thd(y1, 1000, sr)
    h2 = db(np.sqrt(Sp[np.abs(fr - 2000) < 20].sum() / Sp[np.abs(fr - 1000) < 20].sum()))
    print(f"FEATURE drive: guitar level change 50%/100% {lv[0]:+.2f}/{lv[1]:+.2f} dB; "
          f"THD+N 1 kHz -12 dBFS off {100*t0:.4f}% -> 100% {100*t1:.2f}%; 2nd harmonic {h2:.1f} dB")
    check(all(abs(l) < 3 for l in lv) and t1 > 0.01 and h2 > -50, "Drive: level held +/-3 dB, adds harmonics incl. even")

    # Compander: complementary on steady tones (level unchanged after settling); breathing on decays
    rows = []
    ok = True
    for amp in (0.3, 0.03, 0.003):
        x = S.sine(500, 1.0, sr, amp)
        a0 = tone_amp(run(x, sr, params=p(voicing=1, depth=0, mix=100, tone=100))[0][sr // 2:, 0], 500, sr)
        a1 = tone_amp(run(x, sr, params=p(voicing=1, depth=0, mix=100, tone=100, comp=100))[0][sr // 2:, 0], 500, sr)
        d_ = db(a1) - db(a0)
        rows.append(f"{db(amp):.0f}dBFS {d_:+.2f}dB")
        ok &= abs(d_) < 1.0
    xg = S.guitar_phrase(sr)[: 2 * sr]
    e0 = run(xg, sr, params=p(mix=100, depth=50))[0][:, 0]
    e1 = run(xg, sr, params=p(mix=100, depth=50, comp=100))[0][:, 0]
    env = lambda v: np.sqrt(signal.lfilter(np.ones(480) / 480, 1, v.astype(float) ** 2))
    dev = db(env(e1)[sr // 10:] + 1e-9) - db(env(e0)[sr // 10:] + 1e-9)
    act = env(e0)[sr // 10:] > 1e-3
    print(f"FEATURE compander: steady-tone wet level vs off: {', '.join(rows)}; "
          f"guitar envelope deviation (breathing) p5/p95 {np.percentile(dev[act], 5):+.1f}/{np.percentile(dev[act], 95):+.1f} dB")
    check(ok, "Compander: complementary on steady tones (wet level within 1 dB of off)")

    # Emphasis: transparent for small signals, adds high-frequency grit at high level
    small, big = [], []
    for f in (200, 1000, 4000, 10000):
        xs = S.sine(f, 0.5, sr, 0.003)
        a0 = tone_amp(run(xs, sr, params=p(voicing=1, depth=0, mix=100, tone=100))[0][sr // 4:, 0], f, sr)
        a1 = tone_amp(run(xs, sr, params=p(voicing=1, depth=0, mix=100, tone=100, emph=100))[0][sr // 4:, 0], f, sr)
        small.append(db(a1) - db(a0))
    xb = S.sine(3000, 1.0, sr, 0.1)  # -20 dBFS: playing level
    tb0, _, _ = thd(run(xb, sr, params=p(voicing=1, depth=0, mix=100, tone=100))[0][sr // 4:, 0], 3000, sr)
    tb1, _, _ = thd(run(xb, sr, params=p(voicing=1, depth=0, mix=100, tone=100, emph=100))[0][sr // 4:, 0], 3000, sr)
    xl_ = S.sine(200, 1.0, sr, 0.1)
    tl1, _, _ = thd(run(xl_, sr, params=p(voicing=1, depth=0, mix=100, tone=100, emph=100))[0][sr // 4:, 0], 200, sr)
    print(f"FEATURE emphasis: -50 dBFS response vs off at 200/1k/4k/10k Hz: " +
          "/".join(f"{v:+.2f}" for v in small) + f" dB; THD+N -20 dBFS 3 kHz off {100*tb0:.3f}% -> {100*tb1:.2f}%, "
          f"200 Hz {100*tl1:.2f}%")
    check(max(abs(v) for v in small) < 0.5 and tb1 > 10 * tl1 and tb1 > 0.01,
          "Emphasis: flat for small signals, clips highs well before lows")

    # Low Cut: wet -3 dB point
    fr_ = np.array([50, 80, 100, 130, 160, 180, 200, 220, 250, 300, 400, 600, 1000])
    mags = []
    for f in fr_:
        xs = S.sine(f, 0.4, sr, 0.25)
        y = run(xs, sr, params=p(voicing=1, depth=0, mix=100, tone=100, lowcut=200))[0][sr // 5:, 0]
        mags.append(db(tone_amp(y, f, sr) / 0.25))
    mags = np.array(mags) - mags[-1]
    i = np.where(mags > -3)[0][0]
    f3 = fr_[i - 1] + (fr_[i] - fr_[i - 1]) * (-3 - mags[i - 1]) / (mags[i] - mags[i - 1])
    print(f"FEATURE low cut 200 Hz: wet -3 dB at {f3:.0f} Hz, 50 Hz {mags[0]:.1f} dB, 100 Hz {mags[2]:.1f} dB")
    check(170 < f3 < 230, "Low Cut: -3 dB within 15 % of the setting")

    # Mono Bass: a pure side (L = -R) tone is removed below the crossover, kept above;
    # mono input passes untouched
    def side_gain(f):
        t = np.arange(sr) / sr
        sg = 0.25 * np.sin(2 * np.pi * f * t)
        x = np.stack([sg, -sg], 1).astype(np.float32)
        y = run(x, sr, params=p(mix=0, mono=150))[0][sr // 2:]
        return db(tone_amp(0.5 * (y[:, 0] - y[:, 1]), f, sr) / 0.25)
    g60, g1k = side_gain(60), side_gain(1000)
    xg = S.guitar_phrase(sr)[: 3 * sr]
    ym = run(xg, sr, params=p(mix=0, mono=150))[0]
    null = db(rms(ym.astype(np.float64) - xg)) - db(rms(xg))
    print(f"FEATURE mono bass 150 Hz: side (L-R) gain 60 Hz {g60:.1f} dB, 1 kHz {g1k:+.2f} dB; "
          f"mono input null {null:.1f} dB")
    check(g60 < -20 and abs(g1k) < 0.5 and null < -100, "Mono Bass: lows summed to mono, highs and mono input unchanged")

    # Lush: impulse response spread (time to 90 % energy) and L/R decorrelation of the wet
    imp = np.zeros((sr // 2, 2), np.float32)
    imp[100] = 1.0
    def spread(prm):
        y = run(imp, sr, params=prm)[0]
        e = np.cumsum(y[:, 0].astype(float) ** 2)
        return (np.argmax(e >= 0.9 * e[-1]) - np.argmax(e > 1e-6 * e[-1])) / sr * 1000, \
               float(np.corrcoef(y[:, 0], y[:, 1])[0, 1])
    sp0, c0 = spread(p(voicing=1, depth=0, mix=100, tone=100, width=0))
    sp1, c1 = spread(p(voicing=1, depth=0, mix=100, tone=100, width=0, lush=100))
    print(f"FEATURE lush: wet impulse 90 % energy spread {sp0:.1f} -> {sp1:.1f} ms; L/R correlation {c0:+.2f} -> {c1:+.2f}")
    check(sp1 > 15 and sp1 > 10 * max(sp0, 0.1) and c1 < 0.5, "Lush: diffuses (spread > 15 ms, L/R decorrelated)")

    # Shift: octave appears in the feedback cascade
    xs = S.sine(220, 1.5, sr, 0.25)
    res = []
    for st, f in ((12, 440), (-12, 110), (7, 220 * 2 ** (7 / 12))):
        y0 = run(xs, sr, params=p(voicing=1, depth=0, mix=100, tone=100, fb=70))[0][sr // 2:, 0]
        y1 = run(xs, sr, params=p(voicing=1, depth=0, mix=100, tone=100, fb=70, shift=st))[0][sr // 2:, 0]
        r0 = db(tone_amp(y0, f, sr)) - db(tone_amp(y0, 220, sr))
        r1 = db(tone_amp(y1, f, sr)) - db(tone_amp(y1, 220, sr))
        res.append((st, r0, r1))
    print("FEATURE shift: shifted partial re 220 Hz, fb 70 %: " +
          ", ".join(f"{st:+d} st {r0:.0f} -> {r1:.0f} dB" for st, r0, r1 in res))
    check(all(r1 > -25 and r1 - r0 > 30 for _, r0, r1 in res), "Shift: pitch-shifted partials build in the feedback loop")


def gate_engage_clicks():
    """Each v1.1 control switched on, then off, mid-chord: no click at either edge."""
    sr = 48000
    x = S.sustained_chord(sr, 2.0)
    hp = signal.butter(4, 6000 / (sr / 2), "high", output="sos")
    w = int(0.03 * sr)
    worst, who = -99.0, ""
    for pid, on in ((9, 80), (10, 100), (11, 100), (12, 400), (13, 300), (14, 100), (15, 12)):
        ev = [(int(0.6 * sr), "V", pid, float(on)), (int(1.3 * sr), "V", pid, float(k_off(pid)))]
        y = run(x, sr, "128", ev, p(voicing=2, mix=50, fb=40))[0]
        h = signal.sosfilt(hp, y[:, 0].astype(float))
        for t, _, _, _ in ev:
            before = np.max(np.abs(h[t - w - 400:t - 400])) + 1e-9
            during = np.max(np.abs(h[t:t + w])) + 1e-9
            after = np.max(np.abs(h[t + int(0.08 * sr):t + int(0.08 * sr) + w])) + 1e-9
            ex = db(during) - max(db(before), db(after))
            if ex > worst:
                worst, who = ex, f"param {pid}"
    print(f"FEATURE engage/disengage of each v1.1 control mid-chord: worst >6 kHz excess {worst:+.1f} dB ({who})")
    check(worst < 6, "v1.1 controls switch on/off without clicks")


def k_off(pid):
    return 20.0 if pid in (12, 13) else 0.0


def gate_fidelity():
    man = json.load(open(os.path.join(REF, "manifest.json")))
    worst, worst_name, rows = -999.0, "", []
    for name, x, sr, block, ev, prm in fidelity_cases():
        y, _, _ = run(x, sr, block, ev, prm)
        ref = np.fromfile(os.path.join(REF, name + ".f32"), dtype="<f4").reshape(-1, 2)
        if ref.shape != y.shape:
            rows.append((name, 999.0))
            worst, worst_name = 999.0, name
            continue
        res = db(rms(y.astype(np.float64) - ref)) - db(rms(ref))
        rows.append((name, res))
        if res > worst:
            worst, worst_name = res, name
    print("FIDELITY " + " ".join(f"{n}={r:.1f}" for n, r in rows))
    check(worst <= FIDELITY_LIMIT_DB, f"fidelity vs locked baseline renders <= {FIDELITY_LIMIT_DB:.0f} dB residual",
          f"worst {worst:.1f} dB ({worst_name})")
    return worst


# ------------------------------------------------------------------ report-only measurements
def report_extras():
    sr = 48000
    x = S.sine(1000, 2.0, sr, 0.5)
    thd = []
    for v, n in enumerate(VOICINGS):
        if n == "Detune":
            continue
        y, _, _ = run(x, sr, params=p(voicing=v, depth=0, mix=100, tone=100, width=0))
        s = y[sr // 2:, 0].astype(np.float64)
        Sp = np.abs(np.fft.rfft(s * signal.windows.blackmanharris(len(s)))) ** 2
        fr = np.fft.rfftfreq(len(s), 1 / sr)
        fund = (fr > 980) & (fr < 1020)
        band = (fr > 20) & (fr < 20000)
        thd.append(f"{n}={100 * np.sqrt(Sp[band & ~fund].sum() / Sp[fund].sum()):.4f}%")
    print("REPORT THD+N(1kHz,-6dBFS,wet,depth0) " + " ".join(thd))
    freqs = np.array([200, 1000, 2000, 4000, 6000, 8000, 10000, 12000, 14000, 16000, 18000, 20000])
    seg = int(0.12 * sr)
    t = np.arange(seg) / sr
    xs = S.stereo(np.concatenate([0.25 * np.sin(2 * np.pi * f * t) for f in freqs]))
    fr_rows = []
    for v, n in enumerate(VOICINGS):
        if n == "Detune":
            continue
        y, _, _ = run(xs, sr, params=p(voicing=v, depth=0, mix=100, tone=70, width=0))
        mags = []
        for k, f in enumerate(freqs):
            sgm = y[k * seg + seg // 3:(k + 1) * seg, 0].astype(np.float64)
            tt = np.arange(len(sgm)) / sr
            mags.append(db(2 * np.hypot(np.mean(sgm * np.sin(2 * np.pi * f * tt)),
                                        np.mean(sgm * np.cos(2 * np.pi * f * tt))) / 0.25))
        mags = np.array(mags) - mags[0]
        below = np.where(mags < -3)[0]
        if len(below):
            i = below[0]
            f3 = freqs[i - 1] + (freqs[i] - freqs[i - 1]) * (-3 - mags[i - 1]) / (mags[i] - mags[i - 1])
        else:
            f3 = freqs[-1]
        fr_rows.append(f"{n}={int(f3)}Hz")
    print("REPORT wet -3dB point at Tone 70% " + " ".join(fr_rows))
    imp = np.zeros((4800, 2), np.float32)
    imp[100] = 1.0
    y, _, _ = run(imp, sr, params=p(mix=0))
    print(f"REPORT dry latency {int(np.argmax(np.abs(y[:, 0]))) - 100} samples")


def bench(params, reps=OBJ_REPS):
    cmd = [HOST, PLUG, "bench", str(OBJ_SR), str(OBJ_SECONDS), OBJ_BLOCK, str(reps), params]
    if os.path.exists("/usr/bin/taskset") and (os.cpu_count() or 1) > 1:
        cmd = ["taskset", "-c", "1"] + cmd
    r = subprocess.run(cmd, capture_output=True, text=True)
    return [float(l) for l in r.stdout.split()]


def objective():
    meds = []
    for v in range(8):
        runs = bench(f"0={v}," + OBJ_PARAMS)
        meds.append(float(np.median(runs)))
    print("REPORT CPU %/core (median of 11, 48 kHz, 64-frame blocks, fb 30 %) " +
          " ".join(f"{n}={m:.3f}" for n, m in zip(VOICINGS, meds)))
    print(f"REPORT CPU worst voicing {max(meds):.3f} %")
    on = [float(np.median(bench(f"0={v}," + ALL_ON, 5))) for v in range(8)]
    print("REPORT CPU %/core with all v1.1 stages on (median of 5) " +
          " ".join(f"{n}={m:.3f}" for n, m in zip(VOICINGS, on)) + f" mean={np.mean(on):.3f}")
    cpu = platform.processor() or ""
    try:
        cpu = [l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo") if l.startswith("model name")][0]
    except Exception:
        pass
    print(f"REPORT machine {cpu} / {os.cpu_count()} cores / {platform.system()} {platform.release()}")
    return float(np.mean(meds))


if MAKE_REFS:
    make_refs()
    sys.exit(0)

gate_state()
gate_mod_knobs()
gate_block_invariance()
gate_finite_bounded_silence()
gate_features()
gate_engage_clicks()
if QUICK:
    print("GATE=PASS" if not fails else "GATE=FAIL")
    sys.exit(0 if not fails else 1)
gate_fidelity()
report_extras()
obj = objective()
print("GATE=PASS" if not fails else "GATE=FAIL")
print(f"OBJECTIVE={obj:.4f}")
sys.exit(0 if not fails else 1)
