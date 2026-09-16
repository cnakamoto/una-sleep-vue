#!/usr/bin/env python3
"""Plot a SleepVue night file (slp_YYYYMMDD.bin).

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
        q = (bits >> 24) & 0xFF  # v0.9.0 quality byte; 0 in older files
        epochs.append({
            "t": datetime.fromtimestamp(bed + i * 30, timezone.utc).astimezone(LOCAL_TZ),
            "stage": bits & 0x3,
            "move": (bits >> 2) & 0x3F,
            "hr": (bits >> 8) & 0xFF,
            "hrSamples": q & 0x3F,
            "hrDropped": bool(q & 0x40),
        })
    hdr = dict(zip(("total", "awake", "light", "deep", "hrMin", "hrAvg", "hrMax",
                    "flags", "hrCoverage"),
                   struct.unpack("<HHHHBBBBB", blob[18:31])))
    return date_key, bed, epochs, hdr


def pct(sorted_vals, p):
    """Night HR range rule shared by watch/Swift/Python (ADR-0005):
    sorted[(p*(n-1))//100]. Degrades to raw extremes on small n."""
    if not sorted_vals:
        return 0
    return sorted_vals[(p * (len(sorted_vals) - 1)) // 100]


# A run of this many consecutive gap epochs (hr=0) breaks the HR trace;
# a single missing epoch is bridged. Same rule as NightDetailView.swift.
GAP_BREAK_EPOCHS = 2


def hr_segments(ts, epochs):
    """Valid (t, hr) points split into runs separated by >= GAP_BREAK_EPOCHS
    gap epochs — never draw a line across a real dropout."""
    segs, cur, gap = [], [], 0
    for t, e in zip(ts, epochs):
        if e["hr"] > 0:
            if gap >= GAP_BREAK_EPOCHS and cur:
                segs.append(cur)
                cur = []
            cur.append((t, e["hr"]))
            gap = 0
        else:
            gap += 1
    if cur:
        segs.append(cur)
    return segs


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
    end_arg = next((a.split("=", 1)[1] for a in sys.argv[1:]
                    if a.startswith("--end=")), None)
    date_key, bed, epochs, hdr = load(path)

    trimmed_from = None
    if end_arg:
        hh, mm = (int(x) for x in end_arg.split(":"))
        cut = epochs[0]["t"].replace(hour=hh, minute=mm, second=0, microsecond=0)
        if cut < epochs[0]["t"]:
            cut += timedelta(days=1)  # end is on the wake side of midnight
        if cut < epochs[-1]["t"]:
            trimmed_from = epochs[-1]["t"]
            epochs = [e for e in epochs if e["t"] <= cut]

    ts = [e["t"] for e in epochs]
    recorded = [e["stage"] for e in epochs]
    new_stages, baseline = restage(epochs)
    new_deep_min = sum(1 for s in new_stages if s == 2) // 2

    # Totals from epochs (single source of truth — also correct after trim)
    total_min = len(epochs) // 2
    rec_deep = sum(1 for s in recorded if s == 2) // 2
    rec_light = sum(1 for s in recorded if s == 1) // 2
    rec_awake = sum(1 for s in recorded if s == 0) // 2
    hrs_valid = sorted(e["hr"] for e in epochs if e["hr"] > 0)
    hr_min = pct(hrs_valid, 5)     # Night HR range = P5–P95, not extremes
    hr_avg = sum(hrs_valid) // len(hrs_valid) if hrs_valid else 0
    hr_max = pct(hrs_valid, 95)
    coverage = len(hrs_valid) * 100 // len(epochs) if epochs else 0
    n_dropped = sum(1 for e in epochs if e["hrDropped"])
    n_gap = len(epochs) - len(hrs_valid)

    fig, axes = plt.subplots(4, 1, figsize=(13, 10.5), sharex=True,
                             height_ratios=[3, 1.6, 1.6, 1.3],
                             facecolor="#101018")
    ax_hr, ax_hyp, ax_new, ax_mv = axes
    for ax in axes:
        style(ax)

    trim_note = (f"   (trimmed from {trimmed_from:%H:%M} manual stop)"
                 if trimmed_from else "")
    fig.suptitle(
        f"SleepVue — night of {date_key}   {ts[0]:%a %H:%M} → {ts[-1]:%a %H:%M}   "
        f"total {total_min//60}h{total_min%60:02d}   "
        f"HR {hr_min}–{hr_max} avg {hr_avg}  ({coverage}% coverage, "
        f"{n_gap} gap epochs, {n_dropped} with drops){trim_note}\n"
        f"recorded: deep {rec_deep}m · light {rec_light}m · awake {rec_awake}m     "
        f"restaged: deep {new_deep_min}m ({new_deep_min * 100 // total_min}%)",
        color="#e8e8f0", fontsize=11, y=0.98)

    # --- HR panel ---
    k = 11
    for si, seg in enumerate(hr_segments(ts, epochs)):
        vt, vh = zip(*seg)
        ax_hr.plot(vt, vh, color="#e05570", lw=0.9, alpha=0.85,
                   label="HR (30 s)" if si == 0 else None)
        if len(vh) > k:
            sm = [sum(vh[i - k // 2: i + k // 2 + 1]) / k
                  for i in range(k // 2, len(vh) - k // 2)]
            ax_hr.plot(vt[k // 2: len(vh) - k // 2], sm, color="#ffb3c0", lw=2.0,
                       label="HR trend (5 min)" if si == 0 else None)
    # Quality ticks (v0.9.0): epochs where the watch rejected >= 1 sample.
    dropped_t = [t for t, e in zip(ts, epochs) if e["hrDropped"]]
    if dropped_t:
        y0 = min(hrs_valid) - 3 if hrs_valid else 0
        ax_hr.plot(dropped_t, [y0] * len(dropped_t), "|", color="#f0c060",
                   ms=6, mew=0.8, label=f"sample drops ({len(dropped_t)} epochs)")
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
    ax_hyp.set_title("recorded on watch", color="#9090a0",
                     fontsize=9, loc="left", pad=2)
    draw_hypnogram(ax_new, ts, new_stages)
    ax_new.set_title("restaged offline (same epochs — regression check)",
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
