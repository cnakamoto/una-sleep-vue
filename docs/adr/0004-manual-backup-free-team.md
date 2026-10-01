# Manual backup until a paid developer account

**Status**: superseded by ADR-0006 (v0.11.0) — the paid enrollment
arrived and the iCloud entitlement is back. Manual export/import itself
was *not* removed: it is still the backup when signed out of iCloud, and
the tombstones.json decision below outlived the constraint that forced
it.

ADR-0003's automatic iCloud backup shipped as v0.8.0 but couldn't be
built: free "personal team" provisioning does not support the iCloud
capability (Xcode refuses to create a provisioning profile), so the
entitlements had to come straight back out. We replaced it with manual
backup export/import through the document picker — the one API that
reaches user-chosen locations like iCloud Drive *without* the iCloud
entitlement. A backup is a plain folder (`SleepVue Backup/`) containing
the raw night files plus `tombstones.json`; import unions tombstones
before copying nights and never imports a tombstoned night. Tombstones
moved from iCloud KVS (also entitlement-gated) to that JSON file inside
the store directory, making the store folder a complete backup unit.

**Consequences.** Backups are only as fresh as the last manual export;
the app surfaces no staleness reminder (yet). The automatic mode from
ADR-0003 is not deleted, only dormant: `NightStore` still resolves the
ubiquity container when entitled and falls back to `Documents/` when
not. Re-enabling it later = restoring the iCloud entitlements +
`NSUbiquitousContainers` plist entry once a paid Apple Developer account
exists — no code redesign needed. Also corrected by this episode: the
README's claim that HealthKit needs a paid account was wrong — free
teams sign it fine; iCloud/Push is where Apple's paywall actually is.
