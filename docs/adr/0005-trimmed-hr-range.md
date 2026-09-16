# Trimmed night HR range under an unchanged magic

From v0.9.0 the session header's `hrMin`/`hrMax` hold the **P5/P95** of
the valid epoch HRs (rank rule `sorted[(p*(n-1))/100]`), not the raw
extremes, and the file keeps the `SLP1` magic. Motivation: on a wrist
PPG sensor the single lowest and highest epoch are almost always artifact
(the 2026-09-13 loose-band night set a 51 bpm "minimum" from junk
samples), so the extremes were never the number a user should see. The
same trimmed range is the contract for every output — watch face, phone
(which recomputes it from epochs), `plot_night.py`, and the FIT export's
`max_heart_rate`, which therefore deliberately does *not* equal the
literal maximum. The magic stays `SLP1` because the change is purely
additive (`hrCoverage` fills a reserved header byte, the epoch quality
byte was zero) and both existing readers already ignore those bytes; a
magic bump would have forced parser branches for a semantically identical
file.

**Considered options**: keep raw min/max and trim only on display (leaves
the wrong number on flash and in FIT); bump to SLP2 now (SLP2 is reserved
for REM, ADR-0001, and old readers would reject good files).

**Consequences**:

- Pre-0.9.0 and 0.9.0 files store *different quantities* in the same
  header bytes. The only discriminator is `hrCoverage ≠ 0` (older files
  have 0). Readers must not compare header ranges across that boundary;
  the phone sidesteps this by recomputing from epochs, so old nights
  display the trimmed range too.
- ParserCheck's expected output changes once for every existing night
  (raw → P5/P95) — an intentional regeneration, not a regression.
- Any future "resting HR" is a separate concept (P5 over *still* epochs),
  not a relabelling of `hrMin`.
