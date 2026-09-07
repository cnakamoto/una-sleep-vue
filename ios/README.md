# SleepVue for iOS

iPhone companion app for a UNA Watch running the
[SleepVue](../SleepVue/ARCHITECTURE.md) sleep-tracking app. It downloads
recorded nights over Bluetooth LE, renders hypnogram / heart-rate / movement
charts, writes sleep to Apple Health, keeps the watch clock in sync, can
prune transferred nights off the watch, and syncs periodically in the
background.

No account, no server, no network access — everything stays on the phone.

## Features

| Feature | Since | Notes |
|---|---|---|
| BLE night sync (FTS) | v0.1.0 | LISTDIR → windowed READ → CRC32 DIGEST verify |
| Hypnogram + HR + movement charts | v0.1.0 | Same panels as `tools/plot_night.py` |
| ANCS-aware discovery | v0.3.0 | Works while iOS holds the watch's system link |
| Apple Health export | v0.4.0 | Write-only, duplicate-proof |
| Watch clock sync (CTS) | v0.5.0 | Automatic on every connect |
| Prune-after-sync (DELETE) | v0.5.0 | Opt-in toggle, off by default |
| Background sync | v0.6.0 | BGAppRefreshTask + persistent debug log |

## Requirements

- iPhone with iOS 17 or later.
- UNA Watch with the SleepVue app installed (tested against v0.7.x,
  FTS protocol v4/v5 auto-negotiated).
- Xcode 15+ to build. **A physical device is required** — the Simulator
  has no Bluetooth.
- A **paid Apple Developer account** for the HealthKit capability. With
  free provisioning, Xcode will refuse to sign once the HealthKit
  entitlement is present.

## Build & run

1. `open ios/SleepVueSync.xcodeproj`
2. Select the **SleepVueSync** target → *Signing & Capabilities* → pick
   your team. HealthKit is enabled automatically from
   `SleepVueSync.entitlements`.
3. Run on your iPhone.
4. First launch: allow Bluetooth, tap **Connect**, accept the pairing
   sheet (the watch requires a bonded, encrypted link).

## How it works

### Transport — BLE File Transfer Service (FTS)

All file access uses the UNA File Transfer Service (`0xFEBB`), documented
in [`una-sdk/Docs/BLE-File-Transfer-Service.md`](../una-sdk/Docs/BLE-File-Transfer-Service.md):
Adafruit CircuitPython file-transfer base protocol plus UNA's v5
extensions (windowed transfers, CRC32 `DIGEST`, resume). Reads use a
4096-byte window, reassemble strictly by `chunkOffset`, and pace from the
contiguous end — a dropped notification is re-requested, never skipped.
The same code degrades to stop-and-wait against a v4 watch.

Other GATT services used (`una-sdk/Docs/BLE-Services-Overview.md`):
Current Time `0x1805` (clock sync), and Device Information / Battery /
Nordic UART UUIDs for discovery matching.

### Finding the watch — why scanning is the last resort

The watch firmware uses iOS **ANCS** for notifications, and ANCS links
are held by the iOS *system*, not by any app — so the watch shows
"Connected" in Settings even with no app running, and a connected BLE
peripheral **stops advertising**. A plain scan can never see it.

`connect()` therefore tries, in order:

1. the previously-connected watch, by saved identifier
   (`retrievePeripherals(withIdentifiers:)`);
2. peripherals the system is already connected to
   (`retrieveConnectedPeripherals(withServices:)` over the watch's known
   services) — this is the normal path;
3. a plain scan (FTS-filtered, then unfiltered + name match) for a
   factory-fresh watch.

Do **not** "Forget This Device" in Settings — the bond is what makes
all of this work.

### Sync pipeline

1. `LISTDIR /Apps/SleepVue/` → `slp_YYYYMMDD.bin` files not yet stored.
2. Windowed `READ` of each new file (progress by contiguous bytes).
3. Parse + validate as SLP1 (see below) before trusting anything.
4. `DIGEST` (protocol v5+): compare file size + CRC32 against the local
   computation — integrity proof without read-back.
5. Save to `Documents/SleepNights/` (raw `.bin` kept as the source of truth).
6. If enabled, export to Apple Health.
7. If the prune toggle is on, `DELETE` the archive from the watch.
   `slp_cur.bin`, `slp_last.bin` and `slp_idx.bin` are never touched, so
   the watch's own summary/history screens keep working.

### Night format (SLP1)

Source of truth:
[`SleepVue/Software/Libs/Header/SleepTypes.hpp`](../SleepVue/Software/Libs/Header/SleepTypes.hpp).
A 32-byte little-endian header (`SLP1` magic, dateKey, bed/wake unix
epochs, totals, HR stats, flags) followed by 4-byte epoch records
(`stage:2 | movement:6 | hr:8 | spo2:8 | flags:8`, 30 s epochs). The
parser (`Model/NightFile.swift`) is a port of
[`tools/plot_night.py`](../tools/plot_night.py) and is verified
byte-exact against it — see *Parser verification* below.

### Apple Health export

Write-only (`HKCategoryType.sleepAnalysis`): one `.inBed` sample over
bed→wake plus one sample per maximal stage run — AWAKE → `.awake`,
LIGHT → `.asleepCore`, DEEP → `.asleepDeep`, attributed to device
"UNA Watch". Every sample carries `HKMetadataKeySyncIdentifier` keyed to
night + run, so re-exports are no-ops and **duplicates are impossible**.
Results appear under Health → Browse → Sleep with SleepVue as the source.

### Watch clock sync (CTS)

On every connect, the app writes local wall-clock time (SIG Current
Time `0x2A2B`, incl. day-of-week and adjust-reason) and timezone/DST
(`0x2A0F`). Automatic and best-effort: failures are logged, never block
syncing.

### Background sync

A `BGAppRefreshTask` (`com.sleepvue.sync.refresh`) is registered at
launch and re-chained after every run and on backgrounding; requested
every 3 h, actual timing at iOS's discretion. On fire it runs the normal
sync headless (discovery path 2 above means no advertisement is needed),
inside the ~30 s window; on expiration it disconnects and bails.

Caveats: iOS stops scheduling refresh if you **force-quit** the app
(until you open it again), and actual cadence depends on usage patterns.

## Project layout

```
ios/
├── SleepVueSync.xcodeproj/        project + shared scheme
├── SleepVueSync/
│   ├── SleepVueSyncApp.swift      @main; BG task registration, scenePhase scheduling
│   ├── AppState.swift             sync pipeline, toggles, Health export coordination
│   ├── BackgroundSync.swift       BGAppRefreshTask register/schedule
│   ├── Info.plist                 explicit plist (BG modes, task id, usage strings)
│   ├── SleepVueSync.entitlements  HealthKit capability
│   ├── FTS/
│   │   ├── FTSProtocol.swift      wire protocol codecs, CRC32, LE helpers (Foundation-only)
│   │   └── FTSClient.swift        CoreBluetooth: discovery, handshake, windowed READ,
│   │                              LISTDIR/DIGEST/DELETE ops, CTS clock write, debug log
│   ├── Model/NightFile.swift      SLP1 parser + stage runs (Foundation-only)
│   ├── Storage/
│   │   ├── NightStore.swift       Documents/SleepNights/ persistence
│   │   └── DebugLog.swift         persistent rolling log (Documents/ble-debug.log)
│   ├── Health/HealthKitExporter.swift
│   └── UI/                        ContentView, NightDetailView, HypnogramView, Formatters
└── ParserCheck/                   swiftc CLI harness (see below)
```

## Debugging

**In-app BLE log** — the *BLE debug log* section on the main screen shows
timestamped events and persists to `Documents/ble-debug.log` (rolling
128 KB, reloaded at launch), so background syncs leave a trail. Clear
button included.

**Force a background refresh** from the Xcode debugger console:

```
e -l objc -- (void)[[BGTaskScheduler shared] _simulateLaunchForTaskWithIdentifier:@"com.sleepvue.sync.refresh"]
```

**Parser verification (ParserCheck)** — the SLP1 parser and FTS packet
codecs are Foundation-only, so they compile on macOS and can be checked
against real night files pulled from the watch:

```bash
swiftc -o ios/ParserCheck/parsercheck ios/ParserCheck/main.swift \
    ios/SleepVueSync/Model/NightFile.swift ios/SleepVueSync/FTS/FTSProtocol.swift
./ios/ParserCheck/parsercheck nights/slp_20260903.bin
```

Output must match `tools/plot_night.py`-equivalent stats exactly, and
`crc32` must match `python3 -c 'import zlib; print(zlib.crc32(open("...","rb").read()))'`.

**Typecheck without Xcode GUI** (full `xcodebuild` is flaky from CLI on
this machine — CoreSimulator version mismatch):

```bash
SIM_SDK=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk
swiftc -sdk "$SIM_SDK" -target arm64-apple-ios17.0-simulator -typecheck ios/SleepVueSync/**/*.swift ios/SleepVueSync/*.swift
```

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| Connect spins, no watch found | Another phone/app holds the link; keep this build ≥ v0.3.0 (ANCS-aware discovery). Check the debug log for `system-connected peripheral`. |
| "Pairing" stalls | Bond broken — toggle Bluetooth, retry; last resort: Settings → Bluetooth → forget, re-pair (loses ANCS until re-paired). |
| CRC mismatch after transfer | Interference/range — move closer, sync again. Data was not saved. |
| Nights not in Apple Health | Toggle is on but permission denied → Settings → Health → Data Access & Devices → SleepVue. Status shows under the toggle. |
| Background sync never fires | Expected: iOS controls cadence. Force-quit disables it entirely. Verify with the `_simulateLaunchForTaskWithIdentifier` command and the persisted log. |
| Signing error mentioning HealthKit | Free provisioning account — HealthKit needs a paid Developer account. |

## Privacy

Bluetooth LE only; no network calls, no analytics, no accounts. Sleep
data lives in the app's Documents folder and, if you opt in, in Apple
Health (write-only access — the app never reads Health data).

## Versioning

`MARKETING_VERSION` in `project.pbxproj` (**both** target
configurations), semver, bumped on every behavioral change. Git commits
only on request, per the repo's `AGENTS.md`.
