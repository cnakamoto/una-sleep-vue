# SleepVue — Architecture Sketch

Status: **draft / sketch** — no sleep code exists yet; the app is the stock
HelloWorld skeleton. (The original tutorial doc is preserved in git history
and at `una-sdk/Docs/Tutorials/HelloWorld/ARCHITECTURE.md`.)

## 1. What the app does (v1 scope)

Track one sleep session per night, store it on-watch, show analytics:

- Nightly session: bedtime, wake time, total sleep, per-epoch sleep stages
  (AWAKE / LIGHT / DEEP), HR stats, movement counts.
- Last-night summary screen + 7-night history.
- Manual session start/stop (user taps "Start sleep" at bedtime).
  Automatic detection is a later increment (see §9).

Out of scope for v1: REM staging (design decided in §9, gated on the
HEART_BEAT probe), sleep score, alarms/smart-wake, phone sync.

## 2. Runtime model — the decision that shapes everything

The GUI is only alive while the user looks at it (COMMAND_APP_NOTIF_GUI_RUN /
STOP). Sleep data collection must run **all night with the GUI stopped**.

- The **service** is the always-on component. `APP_AUTOSTART` (CMake, default
  Off) launches the app service at boot without GUI — v1 sets
  `set(APP_AUTOSTART On)`. *Verify on hardware that an autostarted Utility
  app service truly stays resident overnight; this is the project's #1 risk.*
- Service RAM budget is 500K (una-app.cmake default) — plenty for an epoch
  ring buffer, no problem for the design below.
- Battery is the real budget: overnight HR + motion must not kill the watch.
  Duty-cycle aggressively (§4).

## 3. Service state machine

```
            (boot / app start)
                  │
              ┌───▼───┐  start cmd   ┌───────────┐
              │ IDLE  │─────────────▶│ TRACKING  │
              └───▲───┘  (GUI tap)   │ (night)   │
                  │                  └─────┬─────┘
                  │   session closed       │ wake detected / stop cmd
                  │◀─────────────────────┘
                  │                  ┌─────▼─────┐
                  └──────────────────│  SUMMARY  │ (persist, then IDLE)
                     summary saved   └───────────┘
```

- **IDLE** — daytime. No sensor subscriptions (except maybe hourly RHR).
  Responds to GUI requests for last-night/history data.
- **TRACKING** — night session. Owns all sensor subscriptions, aggregates
  epochs, flushes to flash periodically (crash-safe: lose ≤ N minutes).
  Aborts to IDLE on: unworn (TOUCH_DETECT), battery critical, stop command.
- **SUMMARY** — transient: close file, compute session stats, back to IDLE.
  The summary is computed from stored epochs, so the GUI can re-request it
  any time — the GUI is stateless rendering.

## 4. Sensor plan (from SDK SensorTypes)

| Sensor | Type | Period while TRACKING | Purpose |
|---|---|---|---|
| HEART_RATE | 0x41 | ~0.1 Hz (every 10 s) | Stage classification, HR curve, resting HR |
| MOTION_DETECT | 0xB0 | event-driven | Movement epochs; wake/abort heuristics |
| ACCELEROMETER | 0x10 | burst 1 Hz, duty-cycled | Movement magnitude when MOTION too coarse |
| TOUCH_DETECT | 0x140 | event-driven | Watch taken off → pause/abort session |
| BATTERY_LEVEL | 0x120 | every 5 min | Abort + warn if critically low |
| SPO2 | 0xF1 | spot-check every 30 min (opt-in) | Oxygen events; **power-hungry, off by default** |
| AMBIENT_TEMPERATURE | 0x70 | hourly (free-ish) | Nice-to-have context on the summary |

Not used: GPS (indoors, huge power), gyroscope (marginal staging value for
the power). HEART_BEAT (0x40, no parser) is under probe as the REM signal
source — see §9.

`HEART_RATE_METRICS` (0x42, AHR/RHR) may already give us resting HR for
free — check whether the platform aggregates it overnight; if so, subscribe
instead of deriving our own baseline.

## 5. Staging algorithm (v1 heuristic)

Classic actigraphy + HR, 30-second epochs:

- **Movement intensity** per epoch: count of MOTION/SIG_MOTION events +
  mean accel magnitude deviation.
- **HR features** per epoch: epoch BPM, drop vs. session baseline (first-hour
  median while motionless).
- **HR sample cleaning** (v0.9.0, knobs `kHr*` in SleepTypes.hpp): each
  raw 1 Hz HEART_RATE sample must pass a trust gate (platform trustLevel
  ≥ 2), a range gate (30–200 bpm) and a spike guard (a > 20 bpm step vs
  the last accepted sample is held until a second sample confirms it;
  a > 60 s measurement gap re-arms unconditional acceptance; the chain
  spans epochs). Epoch BPM = upper median of the accepted samples, not
  the mean. An epoch with no accepted sample is a *gap epoch* (hr = 0):
  it never stages DEEP and never feeds the baseline — falls to LIGHT (or
  AWAKE by motion). Motivation: the 2026-09-13 loose-band night fed 22 %
  low-trust samples straight into the means, producing a 51 bpm baseline
  and 32 min of phantom DEEP. Validated by offline replay of the probe
  logs (`tools/hr_filter_study.py`); the same script's `--compare` mode
  is the ship gate — a 0.9.0 night's `.bin` must equal its `prb_*.csv`
  replay byte-for-byte.
- Classification (deliberately simple, tunable thresholds in one header):
  - movement above threshold → AWAKE
  - no movement, HR within ~5% of baseline → LIGHT
  - no movement sustained 20+ min, HR ≥10% below baseline → DEEP
- No ML, no floating-point-heavy DSP — integer-friendly, runs on the epoch
  tick. All thresholds in `SleepStagingConfig.hpp` for field tuning.

## 6. Data model & storage (`mKernel.fs`)

One file per night + a small index. Binary, fixed-layout, versioned:

```
/SleepVue/
    index.bin            # ring of session headers (last 14 nights)
    night_YYYYMMDD.bin   # header + packed epoch records
```

- **Session header** (fixed size): magic, format version, date, bed/wake
  epoch, totals (duration, stage minutes, HR range/avg, movement count,
  spo2 min/avg), flags (spo2 enabled, aborted), HR coverage.
  - *Night HR range* (v0.9.0, ADR-0005): `hrMin`/`hrMax` are the P5/P95
    of the valid epoch HRs (rank rule `sorted[(p*(n-1))/100]`), never the
    raw extremes; `hrCoverage` = % of epochs with valid HR (0 in older
    files = unknown, and the only way to tell the two header generations
    apart — the magic stays SLP1). The phone recomputes range and
    coverage from the epochs, so old nights display the trimmed range too.
- **Epoch record** (4 bytes): stage:2 | movement:6 | hr:8 (bpm, 0=gap) |
  spo2:8 (%, 0=none) | quality:8 → 960 epochs ≈ 4 KB/night. 14 nights ≈
  60 KB total — trivial for flash. Quality (v0.9.0) = hrSamples:6
  (accepted 1 Hz samples, cap 63) | hrDropped:1 (≥ 1 sample rejected) |
  reserved:1 — the lightweight rejection trail; the full per-sample
  reasons are reconstructible from the probe log.
- Flush every 10 min during TRACKING + on close (seek+rewrite header last).
- FIFO cleanup at 14 nights. JSON (like AnalogFace's fix cache) only for
  user settings (bedtime window, spo2 opt-in) — never for epoch data.

## 7. GUI screens (TouchGFX)

1. **Main** — status field (IDLE/SLEEP) centered at the top; last night:
   stage timeline as an arc band around the bottom edge (90° span,
   45–135°, bed at the left end, wake at the right; 160 columns,
   per-column stage color — DEEP indigo / LIGHT blue / AWAKE amber, same
   palette as tools/plot_night.py; span limited by the button legend
   icons at ~30°/160°) + centered text (total, per-stage minutes,
   bed→wake times, HR min–max) kept inside the ring. Empty state:
   "NO SLEEP YET".
2. **History** — 7-night scroll: duration bars + stage split per night.
3. **Session** — while TRACKING: elapsed time, live HR, stop button
   (and "Start sleep" entry point from Main).

## 8. Message protocol (Commands.hpp, following AnalogFace conventions)

Service → GUI:
- `SLEEP_SUMMARY` (0x01) — last-night header stats; sentinel values render "--".
- `SESSION_STATE` (0x02) — IDLE/TRACKING + elapsed minutes + live HR.
- `HISTORY_ENTRY` (0x03) — one night per message, streamed on request.
- `SLEEP_TIMELINE` (0x07) — last night's epochs downsampled to 160 stage
  columns (2 bits each, majority stage per column, ties go lighter),
  streamed in 4 chunks; pushed with `SLEEP_SUMMARY`, no request needed.

GUI → Service:
- `TRACKING_TOGGLE` (0x04) — start/stop a night session.
- `SUMMARY_REQUEST` (0x05), `HISTORY_REQUEST` (0x06).

## 9. Later increments

- ~~**Auto-detect**~~ — **done (v0.4.0/v0.5.0), fully hands-free**:
  - Auto-wake (v0.4.0): ≥16 of trailing 20 epochs with motion, after
    60-min grace + ≥60 quiet epochs seen → session closes itself.
  - Auto-start (v0.5.0): in IDLE, ≥22 of trailing 24 epochs quiet,
    worn, local time in 20:00–03:00 → TRACKING begins, bed backdated
    to last significant motion (≤30 min, backfilled as LIGHT).
  - Sessions < 3 h are discarded at close (couch captures, naps) —
    one real night per date is the app's model.
  - Motionless sessions are also discarded at close (v0.7.6): a longest
    motion-free stretch ≥ 2 h means the watch lay unworn somewhere
    perfectly still — TOUCH_DETECT is not a reliable unworn signal on a
    bedside table (the 2026-09-05 table capture ran 12.6 h motionless
    with plausible garbage HR). Real nights never exceed ~0.7 h.
  - Both rules were validated by offline replay over real nights
    before shipping; knobs in SleepTypes.hpp Config.
- **REM** — *in study phase (2026-09-14); see ADR-0001 (close-time pass)
  and ADR-0002 (signal pivot + evidence)*:
  - Signal: ~~beat-to-beat HRV from HEART_BEAT (0x40)~~ — **absent on
    this firmware** (v0.8.1 K-probe: isValid()=0, zero events in 3
    sessions). Fallback: HR-variance features from the epoch records
    already on flash — no RAM feature array, no feature trailer (the
    1 Hz within-epoch stdev proved worthless, tools/rem_study.py).
  - Staging: close-time REM pass (ADR-0001) promoting LIGHT→REM with
    whole-night context; reads only epoch records. Rule family under
    study: stillness + across-epoch HR stdev + night-relative HR level
    (the first-90-min baseline is a proven-fragile anchor) + REM latency
    and episode structure.
  - Validation: deferred until a borrowed reference wearable provides
    per-epoch REM labels (~5 co-worn nights, Apple Watch via HealthKit
    preferred); F1-only thresholds tuned blind hit 0–16 % REM across
    9 study nights vs the 20–25 % physiological target. The stats gate
    becomes a relative-agreement check once labels exist.
  - Format (when implemented): SLP2/SIDX2. REM=3 fits the existing 2-bit
    epoch stage field; remMin takes the last SessionHeader reserved byte
    (uint8, v0.9.0 spent the other on hrCoverage) or SLP2 grows the
    header — its magic bump frees the layout;
    IndexSlot grows (rebuilt from night files on migration). Parsers
    handle both magics; ParserCheck covers old + new nights. FIT
    dev-field, GUI timeline/palette, HealthKit .asleepREM are mechanical
    extensions.
  - Rollout: kRemEnabled compile-time flag, Off until label-tuned rules
    pass validation; no GUI toggle. The 0.8.1 probe build stays installed
    as the 1 Hz corpus collector (prb_*.csv per session).
- **Sleep score**, smart alarm, FIT export of nights.
- **24/7 model** (post-hoc segmentation instead of armed sessions):
  onset+wake rules above are its segmentation engine; storage redesign
  (day files) and DailyHealth platform-data investigation come first.

## 10. Open questions (verify early, in order)

1. Does an `APP_AUTOSTART=On` Utility service actually stay resident
   overnight on hardware? → **probe implemented, awaiting an overnight run**
2. Battery cost of HEART_RATE @ 0.1 Hz overnight — measure before
   committing to the staging design. → **same probe answers this**
3. Does the platform already aggregate sleep-adjacent signals we can
   consume (HEART_RATE_METRICS RHR, ACTIVITY STILL) while apps sleep?
4. SpO2 spot-check power cost — is 30-min cadence viable at all?

## 11. Residency + battery probe (current build)

The current `.uapp` is the probe for questions 1–2, not a sleep tracker.

**What it does** — service autostarts at boot, subscribes HEART_RATE @
0.1 Hz + BATTERY_LEVEL @ 5 min, and appends CSV lines to `probe.csv`
(open/seek-end/write/flush/close per line, so a crash loses ≤ 1 line):

```
B,<epoch>,<uptimeMs>          service boot
X,<epoch>,<uptimeMs>          COMMAND_APP_STOP received
A,<epoch>,<uptimeMs>,<battD>  alive marker, 1/min (battery deci-%)
H,<epoch>,<bpm>,<trust>       heart-rate sample
```

The file rotates at boot beyond 200 KB. On GUI open the service parses
the log and pushes a `ProbeStats` summary; the main screen shows span,
boots, stops, alive count, HR samples, longest gap, battery first>last,
and current service uptime.

**Deploy** — install `SleepVue/Output/SleepVue_0.1.0.uapp`
via the companion app, then reboot the watch (so the autostart path is
what launches the service — not an app open). Wear it overnight.

**Reading the verdict in the morning** — open the app:

- `BOOTS 1 STOP 0` + `ALIVE ~480` (8 h) → service stayed resident.
  `BOOTS 2` with the second boot at app-open time → the system killed it
  and autostart only fires at watch boot; residency assumption broken.
- `MAXGAP` should stay near 60 s (alive cadence); hours-long gaps mark
  where the service (or its sensors) went silent.
- `BATT 100>91` → 9 %/night at HR 0.1 Hz ≈ acceptable; much more and the
  sensor plan in §4 needs re-thinking before any staging work.
- Raw data: pull `probe.csv` over BLE FTS (`/Apps/SleepVue/` area)
  for per-sample analysis if the summary raises questions.

### Probe run 1 (2026-08-31 → 09-01): autostart did NOT fire

Watch power-cycled 12:55 EDT after install, worn overnight, plugged in
06:53 EDT. Entire log:

```
B,1788260004,64669638   <- 06:53 EDT, watch already up 17.96 h
A,1788260004,64669649,1000
H,1788260005,0,0        <- bpm 0: sensor ramp-up, not a real sample
H,1788260006,0,0
X,1788260007,64672503   <- stopped 3 s after starting
```

Findings:

1. **The service's first boot came 17.96 h after watch boot** — the
   boot-time autoRun path did not start it, despite flags 0x29 in the
   .uapp, byte-identical to the stock Alarm app (which does autorun on
   this unit; `alarms.json` exists). Watch `SystemLog/LastLog.txt` shows
   the mechanism (`App.Manager::autoRun: Start all autorun applications`)
   from an older boot.
2. The 3-second run coincides with the user opening the app / plugging
   in USB that morning; ended by COMMAND_APP_STOP.
3. Hypothesis now under test: **autoRun only applies to apps that were
   registered (app_list.json scan) at a previous boot** — i.e. a
   sideloaded app becomes autorun-eligible from its *second* boot.
4. Probe v0.1.1 adds `G,<epoch>,<uptimeMs>` (GUI started) so a boot-time
   start (B alone) is distinguishable from a manual app open (B then G).

### Probe run 2 (2026-09-01, ~20 min bench test): AUTOSTART WORKS

User power-cycled and plugged back in after ~20 min. Verdicts from the
log (1,247 lines):

1. **Residency: CONFIRMED.** `B` at uptime **7.1 s** with no `G` — pure
   boot-time autoRun, second-boot hypothesis confirmed (a sideloaded app
   becomes autorun-eligible once registered in `app_list.json` at a
   prior boot). The runtime model in §2 stands.
2. **20.4 min of continuous HR logging** (epochs 1788261064→1788262289)
   with per-line open/flush/close file I/O — no crashes, no gaps, alive
   markers steady at ~60 s cadence.
3. **HR arrives at ~1 Hz, not the requested 0.1 Hz.** The 10 s period
   wasn't honored — almost certainly because the platform's own health
   tracking already runs the HR sensor at 1 Hz and the sensor layer fans
   out at the fastest subscribed rate. Silver lining: the marginal
   sensor cost of our subscription may be ~zero; our real costs are
   wake-ups + flash writes. Revisit the §4 duty-cycle table with this.
4. **USB plug-in sends COMMAND_APP_STOP to app services** (mass-storage
   flush/unmount): X at uptime 1233.9 s, then a B/A/H/H/X flush cycle
   19 s later — the same 3-second signature as run 1, which settles how
   run 1's lone run happened (USB plug-in, not an app open).
5. **Power-off also sends COMMAND_APP_STOP** (X at uptime 17 s of boot
   1 — the user power-cycled promptly).
6. Battery unreadable this test: constant 100.0 % throughout.
   → Overnight run still needed for the §10.2 drain verdict.

### Probe run 3 (2026-09-01 → 09-02, 25 h wear): ALL CLEAR

Worn continuously from Tue 07:40 EDT to Wed 08:45 EDT plug-in.
84,278 log lines. Verdicts:

1. **Residency: total.** 25.06 h continuous service life — zero
   reboots, zero stops, zero GUI opens, 1,495 alive markers (exactly
   60/h), no gaps > 90 s in the entire record.
2. **Battery: 100.0 % → 98.0 % = 2.0 % over 25.1 h (~0.6 %/night).**
   The §10.2 concern is dead; the §4 sensor plan is viable with an
   order of magnitude to spare. SpO2 spot-checks (§10.4) become worth
   testing.
3. **HR coverage: 80,540 valid samples, 98 % of the 1 Hz stream**,
   trust level 3 (max) for the bulk of the night.
4. **First real sleep data — and the §5 heuristic holds up.** Nightly
   HR curve: evening onset ~23:10–23:30 (86→79 bpm), sustained low of
   73 bpm in the 03:30–05:40 window (**23 % dip** vs onset), a restless
   02:00–03:00 stretch (89–93), and a crisp wake spike at 06:41 (110).
   The planned "≥10 % below baseline → DEEP candidate" threshold would
   have segmented this night correctly from HR alone.
5. §10.3 update: the 1 Hz fan-out strongly implies the platform's
   health service runs HR continuously anyway — subscribing to
   HEART_RATE_METRICS (AHR/RHR) instead of deriving our own baseline is
   now the default plan.

**Probe phase complete.** Questions 1–2 answered; the probe app has
earned its retirement. The real tracker (IDLE → TRACKING → close-out,
per §3–§8) is implemented in v0.2.0: manual R1 start/stop, 30 s epoch
staging per §5, §6 binary storage with crash recovery, and the summary
GUI. Field-tuning night: compare its stage split against the probe's
raw HR curve from run 3.

**GUI independence: verified on hardware (2026-09-02).** Exiting the
app (R2 → `sys.exit()`) while TRACKING does not stop the service —
reopening shows the session still running with correct elapsed time.
The intended overnight flow (R1 at bedtime, watch face as usual, R1 in
the morning) works end to end.
