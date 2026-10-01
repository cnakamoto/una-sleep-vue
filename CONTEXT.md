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

**HR sample**:
One raw heart-rate reading from the wrist sensor, delivered about once a
second with a trust level. Samples are cleaned (trust, range, spike gates)
before the survivors are aggregated into an epoch's HR; a rejected sample
leaves only a mark on the epoch, never a number. Distinct from an Epoch —
"sample" is still to be avoided as a synonym for that.
_Avoid_: reading, tick, measurement

**Gap epoch**:
An epoch in which no HR sample survived cleaning (or none arrived). It has
no heart rate, can never be staged DEEP, and does not feed the session
baseline; a run of them is a dropout, which charts must show as a break,
never bridge. The share of non-gap epochs is the session's HR coverage.
_Avoid_: missing epoch, dropped epoch (an epoch with drops may still have
a heart rate)

**Night HR range**:
The P5–P95 span of the epoch-level heart rate over a session — the "HR
52–88" shown on the watch and phone. Deliberately not the extremes: on a
wrist sensor the single lowest and highest readings are almost always
artifacts, so the range is trimmed. Zero means no valid HR all session.
_Avoid_: min/max HR, lowest/highest HR, resting HR (a different, unshipped
concept)

**Session**:
One night's recording, from sleep onset to wake. The app models at most one
real session per date; short or motionless captures are discarded at close.
_Avoid_: night file (that's the storage form), recording

**Latest night**:
The synced session with the most recent date — the one spotlighted at the
top of the phone app's home screen. It is whatever was synced most
recently by date, so it is not necessarily last night's sleep; it always
carries its date so staleness is visible.
_Avoid_: last night, most recent sleep, current night (the watch's
in-progress session is a different thing)

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
A copy of the user's nights and tombstones outside the app's own storage,
so that deleting and reinstalling the app (or losing the phone) does not
lose sleep history. Normally the app's iCloud Drive container, which is
also where the app keeps its nights — so writing a night is backing it
up, with no separate step; a folder the user exports to covers the cases
iCloud can't. Deletions recorded in a backup are honored on restore — a
tombstone always beats a copied night file. A backup is a mirror, not an
archive: deleting a night deletes its backup copy.
_Avoid_: archive (implies append-only — and this one explicitly is not),
sync — "sync" already means the BLE transfer from watch to phone; the
backup is phone to elsewhere

**Restore**:
The recovery of nights and tombstones from a Backup onto a fresh install
of the app, by importing the backup folder. If a watch sync races the
restore, tombstones always win, eventually.
_Avoid_: recovery
