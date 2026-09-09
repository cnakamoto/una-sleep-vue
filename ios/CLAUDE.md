# Agent notes — ios/ (SleepVue iOS companion)

Read `../AGENTS.md` first (repo-wide rules). Full human documentation:
`./README.md`.

## Verify changes without the Xcode GUI

- **Typecheck (primary gate):**
  ```bash
  SIM_SDK=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk
  swiftc -sdk "$SIM_SDK" -target arm64-apple-ios17.0-simulator -typecheck \
    ios/SleepVueSync/**/*.swift ios/SleepVueSync/*.swift
  ```
- **Parser gate:** rebuild and run ParserCheck (see README §Debugging) —
  parser output must stay byte-exact against `tools/plot_night.py` and
  `zlib.crc32` on `nights/*.bin`.
- **After editing `project.pbxproj`:** `plutil -lint` it.
- Full `xcodebuild` from CLI is flaky here (CoreSimulator 1051.54 vs
  Xcode 26.6's 1051.55 — destination resolution fails). Use
  `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer` and treat
  the typecheck as the gate; the user builds/runs from Xcode.app.

## Invariants — do not break these

- `FTS/FTSProtocol.swift` and `Model/NightFile.swift` must stay
  **Foundation-only** (no CoreBluetooth/SwiftUI imports) — ParserCheck
  compiles them on macOS.
- Never DELETE `slp_cur.bin`, `slp_last.bin`, or `slp_idx.bin` on the
  watch; only archived `slp_YYYYMMDD.bin` files, and only after verified
  transfer (+ Health export when enabled).
- User-deleted nights are tombstoned in `deletedDateKeys` and must never
  re-sync; `AppState.delete` only ever removes the archive file on the
  watch (best-effort, when connected).
- All night-detail charts must use `sleepChartXAxis(_:)` with the same
  domain — that shared plot geometry is what keeps the panels aligned.
- Watch discovery order: saved identifier →
  `retrieveConnectedPeripherals` → scan. The watch never advertises while
  iOS holds its ANCS link; a scan-only implementation is a regression.
- HealthKit export must stay idempotent: every sample needs
  `HKMetadataKeySyncIdentifier` (`sleepvue.<dateKey>.<run>`) + sync
  version. Write-only — do not add read permissions without asking.
- Bump `MARKETING_VERSION` in **both** target build configurations on
  every behavioral change.

## Wire-format references

- FTS protocol: `../una-sdk/Docs/BLE-File-Transfer-Service.md`
- GATT service map: `../una-sdk/Docs/BLE-Services-Overview.md`
- SLP1 night layout: `../SleepVue/Software/Libs/Header/SleepTypes.hpp`

## pbxproj conventions

Hand-written project; object IDs are `AA…` hex. Add new source files as
file ref (`…01xx`) + build file (`…02xx`) + group child + Sources phase
entry. Info.plist is explicit (not generated) — background modes and
`BGTaskSchedulerPermittedIdentifiers` live there.
