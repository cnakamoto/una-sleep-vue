# Close-time REM staging

REM is staged by a dedicated pass at session close, not live on the epoch
tick like AWAKE/LIGHT/DEEP. The live tick keeps classifying the three
existing stages and additionally accumulates per-epoch HRV features (from
HEART_BEAT RR intervals) into a RAM array (~2 KB/night). At close, the REM
pass replays the whole night with full-night context — REM's strongest
signals are positional (cycle structure, episodes lengthening toward
morning, "an isolated REM-looking epoch inside a DEEP run is not REM"),
and forward context is unavailable at live-tick time. The pass promotes
eligible LIGHT epochs to REM and rewrites stage bits + header in the same
pass that already finalizes the header.

**Considered options**: live per-epoch REM (trailing context only — weaker,
and worthless to the user since the GUI is never alive at night); hybrid
live-flag + close-confirm (more moving parts for marginal gain).

**Consequences**:

- REM must be computed on-watch: the phone only ever receives epoch
  records, so the HRV features that feed the pass never leave the device.
  (The features are persisted in the SLP2 trailer for offline re-tuning,
  but classification is not the phone's job.)
- Discard rules (<3 h sessions, motionless-table captures) run *before*
  the REM pass; discarded captures are never REM-staged.
- A crash or interruption mid-night degrades gracefully to exactly the
  pre-REM 3-stage behavior: flushed epochs carry valid AWAKE/LIGHT/DEEP
  stages and the header path is unchanged until close.
- Disabling REM (`kRemEnabled` off) means skipping the close-time pass;
  the underlying 3-stage pipeline is untouched.
