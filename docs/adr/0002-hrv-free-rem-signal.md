# HRV-free REM signal, tuned against borrowed labels

REM was planned to use beat-to-beat HRV from the HEART_BEAT sensor (0x40),
the same signal every consumer wearable uses. A K-diagnostic probe
(v0.8.1, 2026-09-14) proved the sensor type is **not registered on this
firmware** — `isValid()` returned 0, zero beat events in three sessions —
so no beat-level signal exists on the device (raw PPG 0xF0 was considered
and rejected unprobed: same "no parser" smell, likely heavy power/DSP).

The fallback is HR-variance features computed from the epoch records
already on flash. An offline study over 9 nights (`tools/rem_study.py`)
found: within-epoch 1 Hz stdev (F2) has no discriminative value (medians
~1 bpm in every stage group), which eliminates the planned RAM feature
array and SLP2 feature trailer — the close-time REM pass (ADR-0001) reads
only epoch records. Across-epoch stdev (F1) shows real but weak,
night-dependent separation, and the study also confirmed that the
first-90-min HR baseline is a fragile anchor (misfired on 09-07, 09-09,
09-13) — REM rules must use night-relative statistics computed at close.

**Consequences**: F1-only thresholds cannot meet the 20–25 % REM stats
gate credibly (observed 0–16 % across nights). Tuning is therefore
deferred until a borrowed reference wearable (Apple Watch via HealthKit
preferred) provides per-epoch REM labels for ~5 co-worn nights; without
labels, threshold choice is aesthetic, not measured. Until then the REM
pass is not implemented and `kRemEnabled` stays off. The probe build
(0.8.1) stays installed as the 1 Hz corpus collector.
