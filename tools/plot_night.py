#!/usr/bin/env python3
"""Plot a SleepAnalytics night file (slp_YYYYMMDD.bin).

Panels: heart rate (baseline + DEEP gate), recorded hypnogram, hypnogram
restaged with the current algorithm, movement.
Usage: plot_night.py <night.bin> [out.png]  (run with .tools-venv python)
"""
import struct
import sys
from datetime import datetime, timedelta, timezone

import matplotlib
matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

LOCAL_TZ = timezone(timedelta(hours=-4))  # EDT

# --- staging constants (mirror SleepTypes.hpp) ---
AWAKE_MOVEMENT = 3
DEEP_HR_DROP_PCT = 10
DEEP_WINDOW_EPOCHS = 40
DEEP_MAX_WINDOW_MOVE = 2
BASELINE_WINDOW_EPOCHS = 180  # 90 min
BASELINE_MIN_EPOCHS = 30

STAGE_COLOR = {0: "#e8a33d", 1: "#4f8fd0", 2: "#25348f"}
STAGE_NAME = {0: "AWAKE", 1: "LIGHT", 2: "DEEP"}
STAGE_Y = {0: 3, 1: 2, 2: 1}  # classic hypnogram: AWAKE top, DEEP bottom


def load(path):
    with open(path, "rb") as f:
        blob = f.read()
    magic, date_key, bed, wake, n = struct.unpack("<4sIIIH", blob[:18])
    assert magic == b"SLP1", magic
    epochs = []
    for i in range(n):
        (bits,) = struct.unpack("<I", blob[32 + i * 4: 36 + i * 4])
        epochs.append({
            "t": datetime.fromtimestamp(bed + i * 30, timezone.utc).astimezone(LOCAL_TZ),
            "stage": bits & 0x3,
            "move": (bits >> 2) & 0x3F,
            "hr": (bits >> 8) & 0xFF,
        })
    hdr = dict(zip(("total", "awake", "light", "deep", "hrMin", "hrAvg", "hrMax"),
                   struct.unpack("<HHHHBBB", blob[18:29])))
    return date_key, bed, epochs, hdr


def frozen_baseline(epochs):
    samples = sorted(
        e["hr"] for e in epochs[:BASELINE_WINDOW_EPOCHS]
        if e["move"] == 0 and e["hr"] > 0
    )
    return samples[len(samples) // 2] if len(samples) >= BASELINE_MIN_EPOCHS else None


def restage(epochs):
    """Replay the current (rolling-window) algorithm over recorded epochs."""
    baseline = frozen_baseline(epochs)
    window, out = [], []
    for e in epochs:
        window.append(min(e["move"], 10))
        window = window[-DEEP_WINDOW_EPOCHS:]
        hr, mv = e["hr"], e["move"]
        if mv >= AWAKE_MOVEMENT:
            out.append(0)
        elif (baseline and hr > 0 and len(window) == DEEP_WINDOW_EPOCHS
              and sum(window) <= DEEP_MAX_WINDOW_MOVE
              and hr * 100 <= baseline * (100 - DEEP_HR_DROP_PCT)):
            out.append(2)
        else:
            out.append(1)
    return out, baseline


def runs(ts, stages):
    """Maximal (t_start, t_end, stage) runs for broken_barh."""
    out, start = [], 0
    for i in range(1, len(stages) + 1):
        if i == len(stages) or stages[i] != stages[start]:
            out.append((ts[start], ts[i - 1] + timedelta(seconds=30), stages[start]))
            start = i
    return out


def draw_hypnogram(ax, ts, stages):
    for t0, t1, s in runs(ts, stages):
        ax.fill_between([t0, t1], STAGE_Y[s] - 0.45, STAGE_Y[s] + 0.45,
                        color=STAGE_COLOR[s], linewidth=0)
    ax.set_ylim(0.4, 3.6)
    ax.set_yticks([3, 2, 1])
    ax.set_yticklabels(["AWAKE", "LIGHT", "DEEP"], color="#c8c8d0", fontsize=9)
    ax.margins(x=0)


def style(ax):
    ax.set_facecolor("#101018")
    ax.tick_params(colors="#c8c8d0", labelsize=9)
    for spine in ax.spines.values():
        spine.set_color("#404050")
    ax.grid(color="#2a2a38", linewidth=0.5, alpha=0.7)
    ax.margins(x=0)


def main():
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else path.rsplit(".", 1)[0] + ".png"
    date_key, bed, epochs, hdr = load(path)

    ts = [e["t"] for e in epochs]
    recorded = [e["stage"] for e in epochs]
    new_stages, baseline = restage(epochs)
    new_deep_min = sum(1 for s in new_stages if s == 2) // 2

    fig, axes = plt.subplots(4, 1, figsize=(13, 10.5), sharex=True,
                             height_ratios=[3, 1.6, 1.6, 1.3],
                             facecolor="#101018")
    ax_hr, ax_hyp, ax_new, ax_mv = axes
    for ax in axes:
        style(ax)

    fig.suptitle(
        f"SleepAnalytics — night of {date_key}   {ts[0]:%a %H:%M} → {ts[-1]:%a %H:%M}   "
        f"total {hdr['total']//60}h{hdr['total']%60:02d}   HR {hdr['hrMin']}–{hdr['hrMax']} avg {hdr['hrAvg']}\n"
        f"recorded: deep {hdr['deep']}m · light {hdr['light']}m · awake {hdr['awake']}m     "
        f"restaged (v0.3.1): deep {new_deep_min}m ({new_deep_min * 100 // hdr['total']}%)",
        color="#e8e8f0", fontsize=11, y=0.98)

    # --- HR panel ---
    valid = [(t, e["hr"]) for t, e in zip(ts, epochs) if e["hr"] > 0]
    vt, vh = zip(*valid)
    ax_hr.plot(vt, vh, color="#e05570", lw=0.9, alpha=0.85, label="HR (30 s)")
    k = 11
    sm = [sum(vh[i - k // 2: i + k // 2 + 1]) / k for i in range(k // 2, len(vh) - k // 2)]
    ax_hr.plot(vt[k // 2: len(vh) - k // 2], sm, color="#ffb3c0", lw=2.0,
               label="HR trend (5 min)")
    if baseline:
        gate = baseline * (100 - DEEP_HR_DROP_PCT) / 100
        ax_hr.axhline(baseline, color="#7fd4a0", lw=1.0, ls="--",
                      label=f"baseline {baseline} bpm")
        ax_hr.axhline(gate, color="#5aa87e", lw=1.0, ls=":",
                      label=f"DEEP gate {gate:.0f} bpm")
    ax_hr.set_ylabel("bpm", color="#c8c8d0")
    ax_hr.legend(loc="upper right", fontsize=8, facecolor="#181822",
                 labelcolor="#c8c8d0", framealpha=0.9)

    # --- hypnograms ---
    draw_hypnogram(ax_hyp, ts, recorded)
    ax_hyp.set_title("recorded on watch (v0.3.0 staging)", color="#9090a0",
                     fontsize=9, loc="left", pad=2)
    draw_hypnogram(ax_new, ts, new_stages)
    ax_new.set_title("restaged offline with the v0.3.1 fix (same epochs)",
                     color="#9090a0", fontsize=9, loc="left", pad=2)
    ax_new.legend(
        handles=[Patch(color=STAGE_COLOR[s], label=STAGE_NAME[s]) for s in (0, 1, 2)],
        loc="upper right", ncol=3, fontsize=8, facecolor="#181822",
        labelcolor="#c8c8d0", framealpha=0.9)

    # --- movement ---
    ax_mv.bar(ts, [e["move"] for e in epochs], width=0.0004,
              color="#c8b45a", linewidth=0)
    ax_mv.set_ylabel("motion", color="#c8c8d0")
    ax_mv.set_ylim(bottom=0)
    ax_mv.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M", tz=LOCAL_TZ))
    ax_mv.xaxis.set_major_locator(mdates.HourLocator(tz=LOCAL_TZ))
    ax_mv.set_xlabel("local time (EDT)", color="#c8c8d0")

    fig.tight_layout(rect=(0, 0, 1, 0.945))
    fig.savefig(out, dpi=140, facecolor=fig.get_facecolor())
    print(out)


if __name__ == "__main__":
    main()
