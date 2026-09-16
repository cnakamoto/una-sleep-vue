#!/usr/bin/env python3
"""Replay raw 1 Hz probe HR (prb_*.csv) through the candidate HR-cleaning
pipeline and A/B it against the night the watch recorded (slp_*.bin).

Pipeline under test (shipped in v0.9.0, mirrors SleepTypes.hpp Config
and Service::acceptHrSample — keep the two byte-exact):
  1. trust gate:      accept only trustLevel >= HR_MIN_TRUST
  2. range gate:      accept only 30 <= bpm <= 200
  3. spike confirm:   a jump of > HR_SPIKE_JUMP bpm vs the last accepted
                      sample must repeat (next sample within 10 of the
                      pending value) before it is accepted; stale last
                      (>60 s) re-arms unconditional acceptance. The chain
                      spans epochs within a session (watch behaviour).
  4. epoch aggregate: upper MEDIAN of the first HR_MAX_SAMPLES accepted
                      samples (was: mean of all); the quality byte counts
                      every accepted sample (cap 63) + an any-dropped bit

Study mode reports per night: per-epoch HR deltas, restaged stage totals
and DEEP-run structure (old vs new epoch HRs, same staging algorithm),
header stats old (raw min/max) vs new (P5/P95 + coverage):

  hr_filter_study.py <prb.csv> <slp.bin> [...]

Compare mode is the v0.9.0 ship gate: replay a night recorded by the
0.9.0 watch and diff the .bin against the replay — epoch HR, hrSamples,
hrDropped, and header P5/avg/P95/coverage must all match exactly. Exit
status 1 on any mismatch:

  hr_filter_study.py --compare <prb.csv> <slp.bin> [...]
"""
import struct
import sys
from datetime import datetime, timedelta, timezone

LOCAL_TZ = timezone(timedelta(hours=-4))  # EDT

# --- candidate filter knobs (mirror planned SleepTypes.hpp Config) ---
HR_MIN_TRUST = 2
HR_MIN_VALID_BPM = 30
HR_MAX_VALID_BPM = 200
HR_SPIKE_JUMP_BPM = 20
HR_SPIKE_CONFIRM_BPM = 10
HR_STALE_SEC = 60
HR_MAX_SAMPLES = 32     # kEpochHrMaxSamples: buffered per epoch for the median
HR_QUALITY_CAP = 63     # 6-bit hrSamples field

# --- staging constants (mirror SleepTypes.hpp / plot_night.py) ---
AWAKE_MOVEMENT = 3
DEEP_HR_DROP_PCT = 10
DEEP_WINDOW_EPOCHS = 40
DEEP_MAX_WINDOW_MOVE = 2
BASELINE_WINDOW_EPOCHS = 180
BASELINE_MIN_EPOCHS = 30


def load_probe(path):
    """-> (start_epoch, {epoch_idx: [(bpm_int, trust)]})"""
    start = None
    samples = {}
    for line in open(path):
        p = line.rstrip().split(",")
        if p[0] == "S":
            start = int(p[1])
        elif p[0] == "H" and start is not None:
            t = int(p[1])
            bpm = int(int(p[2]) / 10.0 + 0.5)  # watch rounds per-sample
            trust = int(p[3])
            samples.setdefault((t - start) // 30, []).append((t, bpm, trust))
    return start, samples


class HrFilter:
    """Service::acceptHrSample, sample for sample. The spike-guard chain
    (last accepted, pending) persists across epochs for the whole session;
    construct one per night."""

    def __init__(self):
        self.last_bpm, self.last_t, self.pending = None, None, None

    def accept(self, t, bpm, trust):
        if trust < HR_MIN_TRUST or not (HR_MIN_VALID_BPM <= bpm <= HR_MAX_VALID_BPM):
            return False
        if self.last_bpm is not None and t - self.last_t <= HR_STALE_SEC:
            if abs(bpm - self.last_bpm) > HR_SPIKE_JUMP_BPM:
                if self.pending is None or abs(bpm - self.pending) > HR_SPIKE_CONFIRM_BPM:
                    self.pending = bpm      # first sighting: hold, drop
                    return False
                # second sample agrees: real transition
            self.pending = None
        else:
            self.pending = None             # no chain / stale: re-arm
        self.last_bpm, self.last_t = bpm, t
        return True


def filter_epoch(filt, samples):
    """One epoch's raw samples -> (median, n_accepted, dropped_any).

    Median is sorted[n//2] (upper median) over the first HR_MAX_SAMPLES
    accepted samples, as on the watch; n_accepted counts them all.
    """
    accepted = []
    n_acc = 0
    dropped = False
    for t, bpm, trust in samples:
        if filt.accept(t, bpm, trust):
            n_acc += 1
            if len(accepted) < HR_MAX_SAMPLES:
                accepted.append(bpm)
        else:
            dropped = True
    if not accepted:
        return 0, 0, dropped
    accepted.sort()
    return accepted[len(accepted) // 2], n_acc, dropped


def load_night(path):
    with open(path, "rb") as f:
        blob = f.read()
    magic, date_key, bed, wake, n = struct.unpack("<4sIIIH", blob[:18])
    assert magic == b"SLP1", magic
    hdr = dict(zip(("hrMin", "hrAvg", "hrMax", "flags", "hrCoverage"),
                   struct.unpack("<BBBBB", blob[26:31])))
    epochs = []
    for i in range(n):
        (bits,) = struct.unpack("<I", blob[32 + i * 4: 36 + i * 4])
        q = (bits >> 24) & 0xFF
        epochs.append({
            "stage": bits & 0x3,
            "move": (bits >> 2) & 0x3F,
            "hr": (bits >> 8) & 0xFF,
            "hrSamples": q & 0x3F,      # 0 in pre-0.9.0 files
            "hrDropped": bool(q & 0x40),
        })
    return date_key, bed, epochs, hdr


def frozen_baseline(hrs, moves):
    samples = sorted(
        h for h, m in zip(hrs[:BASELINE_WINDOW_EPOCHS], moves[:BASELINE_WINDOW_EPOCHS])
        if m == 0 and h > 0
    )
    return samples[len(samples) // 2] if len(samples) >= BASELINE_MIN_EPOCHS else None


def restage(hrs, moves):
    baseline = frozen_baseline(hrs, moves)
    window, out = [], []
    for hr, mv in zip(hrs, moves):
        window.append(min(mv, 10))
        window = window[-DEEP_WINDOW_EPOCHS:]
        if mv >= AWAKE_MOVEMENT:
            out.append(0)
        elif (baseline and hr > 0 and len(window) == DEEP_WINDOW_EPOCHS
              and sum(window) <= DEEP_MAX_WINDOW_MOVE
              and hr * 100 <= baseline * (100 - DEEP_HR_DROP_PCT)):
            out.append(2)
        else:
            out.append(1)
    return out, baseline


def pct(sorted_vals, p):
    """Integer percentile rule shared by watch/Swift/Python:
    sorted[(p*(n-1))//100]. Degrades to raw extremes on small n."""
    if not sorted_vals:
        return 0
    return sorted_vals[(p * (len(sorted_vals) - 1)) // 100]


def stage_totals(stages):
    return (sum(1 for s in stages if s == 0) // 2,
            sum(1 for s in stages if s == 1) // 2,
            sum(1 for s in stages if s == 2) // 2)


def deep_runs(stages):
    """(number of DEEP runs, longest run in minutes). Gap epochs (hr=0)
    can never stage DEEP, so more gaps = more fragmented runs; this line
    is what decides whether a short-gap hold is ever worth adding."""
    runs, cur, longest = 0, 0, 0
    for s in stages:
        if s == 2:
            cur += 1
            if cur == 1:
                runs += 1
            longest = max(longest, cur)
        else:
            cur = 0
    return runs, longest // 2


def replay(probe, n):
    """Run the whole night through one HrFilter -> per-epoch
    (median, n_accepted, dropped) list of length n."""
    filt = HrFilter()
    return [filter_epoch(filt, probe.get(i, [])) for i in range(n)]


def report(prb_path, slp_path):
    start, probe = load_probe(prb_path)
    date_key, bed, epochs, _ = load_night(slp_path)
    n = len(epochs)

    old_hr = [e["hr"] for e in epochs]
    moves = [e["move"] for e in epochs]

    # Replay the night's raw samples through the filter.
    new_hr, coverage, n_dropped_epochs = [], 0, 0
    replayed_mean = []  # sanity: unfiltered mean of the same samples
    for i, (med, n_acc, dropped) in enumerate(replay(probe, n)):
        new_hr.append(med)
        if n_acc:
            coverage += 1
        if dropped:
            n_dropped_epochs += 1
        raw = probe.get(i, [])
        replayed_mean.append(sum(b for _, b, _ in raw) // len(raw) if raw else 0)

    # Harness sanity: replayed unfiltered mean vs what the watch recorded.
    pair = [(a, b) for a, b in zip(old_hr, replayed_mean) if a > 0 and b > 0]
    sane = sorted(abs(a - b) for a, b in pair)
    s_n = len(sane)

    deltas = sorted(abs(a - b) for a, b in zip(old_hr, new_hr) if a > 0 and b > 0)
    d_n = len(deltas)
    gained = sum(1 for a, b in zip(old_hr, new_hr) if a == 0 and b > 0)
    lost = sum(1 for a, b in zip(old_hr, new_hr) if a > 0 and b == 0)

    old_stages, old_base = restage(old_hr, moves)
    new_stages, new_base = restage(new_hr, moves)
    oa, ol, od = stage_totals(old_stages)
    na, nl, nd = stage_totals(new_stages)
    flips = sum(1 for a, b in zip(old_stages, new_stages) if a != b)

    old_valid = sorted(h for h in old_hr if h > 0)
    new_valid = sorted(h for h in new_hr if h > 0)

    print(f"== {date_key}  ({prb_path.rsplit('/', 1)[-1]})")
    print(f"  epochs {n} ({n // 2} min)   probe epochs with samples: {len(probe)}")
    if s_n:
        print(f"  harness check |recorded mean - replayed mean|: "
              f"p50={sane[s_n // 2]} p99={sane[int(s_n * .99)]} max={sane[-1]} bpm (n={s_n})")
    if d_n:
        print(f"  |recorded - filtered median|: p50={deltas[d_n // 2]} "
              f"p90={deltas[int(d_n * .9)]} p99={deltas[int(d_n * .99)]} max={deltas[-1]} bpm (n={d_n})")
    print(f"  HR coverage: {sum(1 for h in old_hr if h > 0)}/{n} epochs -> "
          f"{coverage}/{n} ({coverage * 100 // n}%)   "
          f"gained {gained}, lost {lost} (junk-only epochs -> hr=0)")
    print(f"  epochs where filter dropped >=1 sample: {n_dropped_epochs}")
    print(f"  baseline: {old_base} -> {new_base} bpm")
    print(f"  restaged  old: awake {oa} light {ol} deep {od}   "
          f"new: awake {na} light {nl} deep {nd}   epoch flips: {flips}")
    rec_a, rec_l, rec_d = stage_totals([e["stage"] for e in epochs])
    print(f"  (recorded on watch: awake {rec_a} light {rec_l} deep {rec_d})")
    (orn, orl), (nrn, nrl) = deep_runs(old_stages), deep_runs(new_stages)
    print(f"  DEEP runs  old: {orn} (longest {orl} min)   new: {nrn} (longest {nrl} min)")
    for tag, v in (("old", old_valid), ("new", new_valid)):
        if v:
            print(f"  {tag} stats: min {v[0]} P5 {pct(v, 5)} "
                  f"avg {sum(v) // len(v)} P95 {pct(v, 95)} max {v[-1]}   (n={len(v)})")
    print()


def compare(prb_path, slp_path):
    """Ship gate: the 0.9.0 watch's .bin must equal the replay exactly."""
    start, probe = load_probe(prb_path)
    date_key, bed, epochs, hdr = load_night(slp_path)
    n = len(epochs)
    bad = []
    if start != bed:
        bad.append(f"probe start {start} != bedEpoch {bed}")
    for i, (med, n_acc, dropped) in enumerate(replay(probe, n)):
        e = epochs[i]
        want = (med, min(n_acc, HR_QUALITY_CAP), dropped)
        got = (e["hr"], e["hrSamples"], e["hrDropped"])
        if want != got:
            bad.append(f"epoch {i}: bin hr/samples/dropped {got} != replay {want}")
    valid = sorted(e["hr"] for e in epochs if e["hr"] > 0)
    want_hdr = {
        "hrMin": pct(valid, 5),
        "hrAvg": sum(valid) // len(valid) if valid else 0,
        "hrMax": pct(valid, 95),
        "hrCoverage": len(valid) * 100 // n if n else 0,
    }
    for k, v in want_hdr.items():
        if hdr[k] != v:
            bad.append(f"header {k}: bin {hdr[k]} != recomputed {v}")
    print(f"== {date_key}  compare: {n} epochs, "
          f"{'OK' if not bad else f'{len(bad)} mismatch(es)'}")
    for line in bad[:40]:
        print("  " + line)
    if len(bad) > 40:
        print(f"  ... {len(bad) - 40} more")
    return not bad


def main():
    args = sys.argv[1:]
    mode = compare if args and args[0] == "--compare" else report
    if mode is compare:
        args = args[1:]
    if len(args) % 2 or not args:
        sys.exit("usage: hr_filter_study.py [--compare] <prb.csv> <slp.bin> [...]")
    ok = True
    for i in range(0, len(args), 2):
        if mode(args[i], args[i + 1]) is False:
            ok = False
    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
