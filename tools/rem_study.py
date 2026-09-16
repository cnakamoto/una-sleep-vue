#!/usr/bin/env python3
"""REM feasibility study for the HRV-free design (HEART_BEAT proved absent,
v0.8.1 K-probe verdict 0,0 — ARCHITECTURE.md §9).

Compares two candidate HR-variance feature families for separating
REM-plausible stretches (still body, jagged HR, second half of night):

  F1 across-epoch: stdev of 30-s HR means over a trailing window —
                   computable from every existing night file, no new
                   on-watch data needed.
  F2 within-epoch: stdev of the raw 1 Hz HR samples inside one epoch —
                   needs the probe's prb_*.csv H lines (2 nights only).

Usage: rem_study.py <slp.bin> [prb.csv] [out.png]   (.tools-venv python)
"""
import math
import struct
import sys
from datetime import datetime, timedelta, timezone

import matplotlib
matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

from plot_night import load, restage, LOCAL_TZ, STAGE_COLOR, STAGE_Y, style

EPOCH = 30
F1_WINDOW = 10          # trailing epochs (5 min)
QUIET_WINDOW = 20       # trailing epochs whose movement must be ~nil
QUIET_MAX = 1
REM_LATENCY_EPOCHS = 120  # no REM in the first 60 min
MERGE_GAP = 4           # bridge non-candidate runs up to this many epochs
MIN_EPISODE = 6         # keep candidate runs of >= 3 min (after merging)
LEVEL_PCTL = 50         # candidate HR must exceed this percentile of
                        # the night's sleep-epoch HR (night-relative anchor —
                        # the first-90-min baseline misfired on 09-07/09/13)


def load_prb(path):
    """prb CSV -> {epoch_s: [bpm, ...]} for valid H samples."""
    samples = {}
    with open(path) as f:
        for line in f:
            parts = line.strip().split(",")
            if len(parts) == 4 and parts[0] == "H":
                bpm10 = int(parts[2])
                if bpm10 > 0:
                    samples.setdefault(int(parts[1]), []).append(bpm10 / 10.0)
    return samples


def features(epochs, prb, bed):
    """Per-epoch (f1, f2, quiet) feature vectors."""
    hrs = [e["hr"] for e in epochs]
    f1, f2, quiet = [], [], []
    for i, e in enumerate(epochs):
        lo = max(0, i - F1_WINDOW + 1)
        win = [h for h in hrs[lo:i + 1] if h > 0]
        if len(win) >= 5 and e["hr"] > 0:
            m = sum(win) / len(win)
            f1.append(math.sqrt(sum((h - m) ** 2 for h in win) / len(win)))
        else:
            f1.append(0.0)
        if prb is not None:
            sec = []
            for s in range(EPOCH):
                sec += prb.get(bed + i * EPOCH + s, [])
            if len(sec) >= 15:
                m = sum(sec) / len(sec)
                f2.append(math.sqrt(sum((h - m) ** 2 for h in sec) / len(sec)))
            else:
                f2.append(0.0)
        qlo = max(0, i - QUIET_WINDOW + 1)
        quiet.append(sum(e2["move"] for e2 in epochs[qlo:i + 1]))
    return f1, f2, quiet


def night_level(epochs, restaged, pctl):
    """Night-relative HR anchor: pctl-th percentile of valid sleep-epoch HR."""
    hrs = sorted(e["hr"] for e, s in zip(epochs, restaged)
                 if s != 0 and e["hr"] > 0)
    if not hrs:
        return 0
    return hrs[min(len(hrs) - 1, len(hrs) * pctl // 100)]


def candidates(epochs, restaged, f1, quiet, level, t1):
    """REM candidate mask: still LIGHT, HR above the night-relative level,
    jagged HR, past REM latency. Episode merge + min-length filter."""
    cand = []
    for i, e in enumerate(epochs):
        ok = (restaged[i] == 1 and quiet[i] <= QUIET_MAX
              and e["hr"] > 0 and e["hr"] >= level
              and f1[i] >= t1 and i >= REM_LATENCY_EPOCHS)
        cand.append(ok)
    # bridge non-candidate gaps up to MERGE_GAP between candidate runs
    i = 0
    while i < len(cand):
        if not cand[i]:
            j = i
            while j < len(cand) and not cand[j]:
                j += 1
            if i > 0 and j < len(cand) and j - i <= MERGE_GAP:
                for k in range(i, j):
                    cand[k] = True
            i = j
        else:
            i += 1
    # drop runs shorter than MIN_EPISODE
    out, i = cand[:], 0
    while i < len(cand):
        if cand[i]:
            j = i
            while j < len(cand) and cand[j]:
                j += 1
            if j - i < MIN_EPISODE:
                for k in range(i, j):
                    out[k] = False
            i = j
        else:
            i += 1
    return out


def episode_stats(cand):
    """(count, total_min, first_onset_epoch, [durations_min])."""
    durs, start = [], None
    for i in range(len(cand) + 1):
        on = i < len(cand) and cand[i]
        if on and start is None:
            start = i
        elif not on and start is not None:
            durs.append((i - start) * EPOCH // 60)
            start = None
    return (len(durs), sum(durs), durs)


def main():
    slp = sys.argv[1]
    prb_path = next((a for a in sys.argv[2:] if a.endswith(".csv")), None)
    out = next((a for a in sys.argv[2:] if a.endswith(".png")),
               slp.rsplit(".", 1)[0] + "_rem.png")
    date_key, bed, epochs, hdr = load(slp)
    prb = load_prb(prb_path) if prb_path else None
    restaged, baseline = restage(epochs)
    f1, f2, quiet = features(epochs, prb, bed)
    ts = [e["t"] for e in epochs]

    t1 = 2.0
    level = night_level(epochs, restaged, LEVEL_PCTL)
    cand = candidates(epochs, restaged, f1, quiet, level, t1)
    n_ep, rem_min, durs = episode_stats(cand)
    total = len(epochs) * EPOCH // 60
    asleep = sum(1 for s in restaged if s != 0) * EPOCH // 60
    print(f"night {date_key}: total {total}m asleep {asleep}m "
          f"baseline {baseline} level P{LEVEL_PCTL} {level}")
    for t in (1.5, 2.0, 2.5, 3.0):
        c = candidates(epochs, restaged, f1, quiet, level, t)
        ne, rm, du = episode_stats(c)
        lat = next((i for i, x in enumerate(c) if x), None)
        lat_m = lat * EPOCH // 60 if lat is not None else None
        print(f"  F1>={t}: REM {rm}m ({rm * 100 // max(asleep, 1)}% of sleep) "
              f"episodes {ne} {du} latency {lat_m}m")

    rows = 5 if prb else 4
    ratios = [3, 1.2] + ([1.2] if prb else []) + [1.6, 1.1]
    fig, axes = plt.subplots(rows, 1, figsize=(13, 11), sharex=True,
                             height_ratios=ratios, facecolor="#101018")
    for ax in axes:
        style(ax)
    ax_hr, ax_f1 = axes[0], axes[1]
    ax_f2 = axes[2] if prb else None
    ax_hyp, ax_mv = (axes[3], axes[4]) if prb else (axes[2], axes[3])

    valid = [(t, e["hr"]) for t, e in zip(ts, epochs) if e["hr"] > 0]
    vt, vh = zip(*valid)
    ax_hr.plot(vt, vh, color="#e05570", lw=0.9, alpha=0.85)
    if baseline:
        ax_hr.axhline(baseline, color="#7fd4a0", lw=1.0, ls="--",
                      label=f"baseline {baseline}")
        ax_hr.legend(loc="upper right", fontsize=8, facecolor="#181822",
                     labelcolor="#c8c8d0")
    ax_hr.set_ylabel("bpm", color="#c8c8d0")
    fig.suptitle(
        f"REM study — night of {date_key}   REM candidates (F1>={t1}): "
        f"{rem_min}m in {n_ep} episodes {durs}", color="#e8e8f0", fontsize=11)

    ax_f1.plot(ts, f1, color="#b78ef0", lw=1.1)
    ax_f1.axhline(t1, color="#7a68c0", lw=0.9, ls=":")
    ax_f1.set_ylabel("F1 stdev\n5min, bpm", color="#c8c8d0", fontsize=8)
    if ax_f2:
        ax_f2.plot(ts, f2, color="#6fc3c9", lw=1.1)
        ax_f2.set_ylabel("F2 stdev\n1Hz, bpm", color="#c8c8d0", fontsize=8)

    # hypnogram + REM candidate overlay (purple band under the stage band)
    from plot_night import runs
    for t0, t1_, s in runs(ts, restaged):
        ax_hyp.fill_between([t0, t1_], STAGE_Y[s] - 0.45, STAGE_Y[s] + 0.45,
                            color=STAGE_COLOR[s], linewidth=0)
    for t0, t1_, s in runs(ts, [3 if c else -1 for c in cand]):
        if s == 3:
            ax_hyp.fill_between([t0, t1_], 0.0, 0.35,
                                color="#8e44ad", linewidth=0)
    ax_hyp.set_ylim(-0.2, 3.6)
    ax_hyp.set_yticks([3, 2, 1])
    ax_hyp.set_yticklabels(["AWAKE", "LIGHT", "DEEP"], color="#c8c8d0",
                           fontsize=9)
    ax_hyp.legend(handles=[Patch(color="#8e44ad", label="REM candidate")],
                  loc="upper right", fontsize=8, facecolor="#181822",
                  labelcolor="#c8c8d0")

    ax_mv.bar(ts, [e["move"] for e in epochs], width=0.0004,
              color="#c8b45a", linewidth=0)
    ax_mv.set_ylabel("motion", color="#c8c8d0")
    ax_mv.set_ylim(bottom=0)
    ax_mv.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M", tz=LOCAL_TZ))
    ax_mv.xaxis.set_major_locator(mdates.HourLocator(tz=LOCAL_TZ))

    fig.tight_layout(rect=(0, 0, 1, 0.955))
    fig.savefig(out, dpi=140, facecolor=fig.get_facecolor())
    print(out)


if __name__ == "__main__":
    main()
