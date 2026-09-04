# Agent notes — SleepVue (UNA Watch sleep app)

Project: UNA Watch sleep tracker. App folder `SleepVue/` (service in
`Software/Libs/`, GUI in `Software/Apps/TouchGFX-GUI/gui/`), SDK cloned
to `una-sdk/` (gitignored), build via `source env.sh` + cmake/make in
`SleepVue/build/` (see README.md). `SleepVue/ARCHITECTURE.md` is the
design doc — keep it current when behavior changes.

## On every watch access (whenever `/Volumes/UNA WATCH` is mounted)

Do this routine without being asked:

1. **Pull new night files**: compare
   `/Volumes/UNA WATCH/Apps/SleepVue/slp_*.bin` against local `nights/`
   (gitignored). Copy any new/changed ones into `nights/`.
2. **Always generate the PNG chart for new data**:
   `.tools-venv/bin/python tools/plot_night.py nights/slp_YYYYMMDD.bin nights/YYYY-MM-DD.png`
   then view the PNG and report the night (totals, deep %, anomalies,
   flags) to the user. If the user says they stopped late, re-render
   with `--end=HH:MM` at the wake transition.
3. **Pull new FIT exports** (`Apps/SleepVue/Activity/YYYYMM/*.fit`) to
   `nights/` as well; validate structure with `fitparse` if asked or if
   phone-sync behavior is being investigated.
4. **Installing builds**: copy the new `.uapp` into
   `Apps/SleepVue/` (remove the old version first), remove any `._*`
   AppleDouble files, verify with `md5`, `sync`, then eject
   (`diskutil eject`, force-unmount only after a sync if loginwindow
   dissents). Remind the user to power cycle.

## Conventions

- Thresholds/params: `SleepVue/Software/Libs/Header/SleepTypes.hpp`
  (comment each change with the data that motivated it).
- Validate algorithm changes by replaying real epochs offline
  (`nights/*.bin`) before shipping — established pattern for the DEEP
  fix and both auto-detect rules.
- Bump `BUILD_VERSION` in `SleepVue/Software/Apps/HelloWorld-CMake/CMakeLists.txt`
  on every behavioral change; keep it semver-clean.
- Git: commit only when the user asks. Message style: short imperative
  summary + a body with the data/evidence behind the change.
