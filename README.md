# SleepVue

Sleep analytics app for the UNA Watch: hands-free night tracking
(auto-start/auto-wake), on-watch staging (AWAKE/LIGHT/DEEP), 7-night
history, FIT export for phone sync. See `SleepVue/ARCHITECTURE.md` for
the design. Agent-facing operational notes live in `AGENTS.md`.

## Night charts (`nights/`)

Every closed night can be rendered as a four-panel PNG:

1. **Heart rate** — raw 30 s HR + 5-min trend, frozen baseline and the
   DEEP gate from `SleepTypes.hpp`
2. **Recorded hypnogram** — stages as classified on the watch
3. **Restaged hypnogram** — the same epochs re-run offline through the
   current algorithm (regression check: should match panel 2)
4. **Motion** — per-epoch movement events

`nights/` is gitignored — personal health data stays local.

### Generate

```bash
# one-time tooling setup (matplotlib + fitparse)
python3 -m venv .tools-venv && .tools-venv/bin/pip install matplotlib fitparse

# 1. plug in the watch, wait for /Volumes/UNA WATCH to mount
# 2. copy the night file over
cp "/Volumes/UNA WATCH/Apps/SleepVue/slp_YYYYMMDD.bin" nights/

# 3. render (out path optional: defaults to <input>.png)
.tools-venv/bin/python tools/plot_night.py nights/slp_YYYYMMDD.bin nights/YYYY-MM-DD.png
```

Options:

- `--end=HH:MM` — trim the session at a fixed local time. Use when the
  morning stop happened well after waking; the title notes the original
  stop time. (With auto-wake enabled this should be rare.)

### Validate a FIT export

Session closes also write `Activity/YYYYMM/activity_*.fit` (phone-sync
spike). Sanity-check one with:

```bash
.tools-venv/bin/python - <<'EOF'
from fitparse import FitFile
for m in FitFile("nights/activity_....fit").get_messages():
    print(m.name)
EOF
```

## Build & flash

```bash
source env.sh                       # UNA_SDK + venv
cd SleepVue/build
cmake -G "Unix Makefiles" ../Software/Apps/HelloWorld-CMake && make
# install: copy SleepVue/Output/SleepVue_X.Y.Z.uapp to
#   /Volumes/UNA WATCH/Apps/SleepVue/, verify md5, eject, power cycle
```
