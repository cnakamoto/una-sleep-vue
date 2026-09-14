# SleepVue

UNA Watch sleep tracker: a watch app that records one sleep session per night
and stages it, plus an iPhone companion app that syncs the recordings.

## Language

**Stage**:
The sleep classification of one epoch. One of AWAKE, LIGHT, DEEP — with REM
planned. Exactly one stage per epoch; there is no "unknown" stage.
_Avoid_: phase, state, sleep level

**Epoch**:
The fixed 30-second unit of a sleep session. All staging, movement, and HR
data is aggregated per epoch; a night file is a sequence of epoch records.
_Avoid_: window, sample, tick

**Session**:
One night's recording, from sleep onset to wake. The app models at most one
real session per date; short or motionless captures are discarded at close.
_Avoid_: night file (that's the storage form), recording

**REM**:
Rapid-eye-movement sleep. On the wrist it can only be *inferred* — the target
signature is a motionless body (as in DEEP) with an elevated, irregular heart
rhythm (unlike DEEP's low-and-steady), recurring in ~90-minute cycles that
lengthen toward morning. This device has no beat-level heart signal
(HEART_BEAT is absent on the firmware), so the irregularity is read from
HR variance in the epoch records, not true HRV.
_Avoid_: paradoxical sleep, dream sleep

**RR interval**:
The time between two consecutive heartbeats, in milliseconds. The raw
ingredient of HRV. Not obtainable on this hardware (ADR-0002); the term
survives because docs cite the rejected approach.
_Avoid_: inter-beat interval (IBI), beat gap

**HRV**:
Heart-rate variability — beat-to-beat variation computed from RR intervals
over a window. The REM discriminator used by consumer wearables, and the
road not taken here (no beat-level source on the device).
_Avoid_: HR variance (that's variance of the smoothed BPM stream — the
degraded signal this project actually has)

**REM pass**:
The classification step that assigns the REM stage to epochs of a completed
session, using whole-night context. REM is the only stage assigned this way —
AWAKE, LIGHT, and DEEP are assigned per epoch during the session itself.
_Avoid_: reclassification, post-processing

**Tombstone**:
The record that a session was deleted by the user. A tombstoned session must
never reappear — not from the watch, not after an app reinstall — so the
tombstones themselves are part of what the Backup preserves.
_Avoid_: deleted list

**Backup**:
The off-device replica of the user's nights and tombstones, keyed to the
user's Apple account, so that deleting and reinstalling the app (or losing
the phone) does not lose sleep history. Mirrored, not append-only: deleting
a night deletes its backup copy too.
_Avoid_: archive (implies append-only), sync — "sync" already means the BLE
transfer from watch to phone; the backup is phone to cloud

**Restore**:
The automatic recovery of nights and tombstones from the Backup onto a fresh
install of the app. Unprompted — the user's own history simply reappears.
If a watch sync races the backup's arrival, tombstones always win,
eventually.
_Avoid_: import, recovery
