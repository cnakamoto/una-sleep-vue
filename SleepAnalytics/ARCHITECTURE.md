# SleepAnalytics — Architecture Sketch

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

Out of scope for v1: REM staging (needs HRV — unproven on this hardware),
sleep score, alarms/smart-wake, phone sync.

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
the power), HEART_BEAT (0x40, no parser — revisit if we attempt HRV/REM).

`HEART_RATE_METRICS` (0x42, AHR/RHR) may already give us resting HR for
free — check whether the platform aggregates it overnight; if so, subscribe
instead of deriving our own baseline.

## 5. Staging algorithm (v1 heuristic)

Classic actigraphy + HR, 30-second epochs:

- **Movement intensity** per epoch: count of MOTION/SIG_MOTION events +
  mean accel magnitude deviation.
- **HR features** per epoch: mean BPM, drop vs. session baseline (first-hour
  median while motionless).
- Classification (deliberately simple, tunable thresholds in one header):
  - movement above threshold → AWAKE
  - no movement, HR within ~5% of baseline → LIGHT
  - no movement sustained 20+ min, HR ≥10% below baseline → DEEP
- No ML, no floating-point-heavy DSP — integer-friendly, runs on the epoch
  tick. All thresholds in `SleepStagingConfig.hpp` for field tuning.

## 6. Data model & storage (`mKernel.fs`)

One file per night + a small index. Binary, fixed-layout, versioned:

```
/SleepAnalytics/
    index.bin            # ring of session headers (last 14 nights)
    night_YYYYMMDD.bin   # header + packed epoch records
```

- **Session header** (fixed size): magic, format version, date, bed/wake
  epoch, totals (duration, stage minutes, HR min/avg/max, movement count,
  spo2 min/avg), flags (spo2 enabled, aborted).
- **Epoch record** (4 bytes): stage:2 | movement:6 | hr:8 (bpm, 0=none) |
  spo2:8 (%, 0=none) | flags:8 → 960 epochs ≈ 4 KB/night. 14 nights ≈ 60 KB
  total — trivial for flash.
- Flush every 10 min during TRACKING + on close (seek+rewrite header last).
- FIFO cleanup at 14 nights. JSON (like AnalogFace's fix cache) only for
  user settings (bedtime window, spo2 opt-in) — never for epoch data.

## 7. GUI screens (TouchGFX)

1. **Main** — last night: total sleep big, stage bar (AWAKE/LIGHT/DEEP),
   bed→wake times, HR min/avg. Empty state: "No sleep recorded".
2. **History** — 7-night scroll: duration bars + stage split per night.
3. **Session** — while TRACKING: elapsed time, live HR, stop button
   (and "Start sleep" entry point from Main).

## 8. Message protocol (Commands.hpp, following AnalogFace conventions)

Service → GUI:
- `SLEEP_SUMMARY` (0x01) — last-night header stats; sentinel values render "--".
- `SESSION_STATE` (0x02) — IDLE/TRACKING + elapsed minutes + live HR.
- `HISTORY_ENTRY` (0x03) — one night per message, streamed on request.

GUI → Service:
- `TRACKING_TOGGLE` (0x04) — start/stop a night session.
- `SUMMARY_REQUEST` (0x05), `HISTORY_REQUEST` (0x06).

## 9. Later increments

- **Auto-detect**: arm a window (e.g., 21:00–02:00); STILL activity +
  no wrist motion + HR trending down → auto-start; symmetric wake detect.
- **REM via HRV**: needs beat-to-beat (HEART_BEAT raw or PPG) — investigate.
- **Sleep score**, smart alarm, FIT export of nights.

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

**Deploy** — install `SleepAnalytics/Output/SleepAnalytics_0.1.0.uapp`
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
- Raw data: pull `probe.csv` over BLE FTS (`/Apps/SleepAnalytics/` area)
  for per-sample analysis if the summary raises questions.
