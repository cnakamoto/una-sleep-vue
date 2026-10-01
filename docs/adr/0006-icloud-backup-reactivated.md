# iCloud backup reactivated on a paid developer account

**Status**: accepted (v0.11.0). Supersedes ADR-0004 and reactivates
ADR-0003, which has been dormant since v0.8.0.

The paid Apple Developer enrollment that ADR-0004 was waiting for now
exists, so the iCloud entitlement signs. ADR-0003's design is restored
unchanged in substance — the night store root becomes the app's iCloud
Drive container, writing a night *is* the backup, and a fresh install
re-downloads its nights with no user action. `NightStore` needed no
redesign, exactly as ADR-0004 predicted: the dormant
`activateICloudIfAvailable()` path, the `Documents/SleepNights/`
fallback, and the migration of locally-stored nights into the container
were all already written and wired into `AppState`.

Three things are deliberately *different* from ADR-0003 as written:

**Tombstones stay in `tombstones.json`.** ADR-0003 put them in iCloud
KVS; ADR-0004 moved them into the store directory to escape the
entitlement, and that turned out to be the better design — the store
folder is a single complete backup unit, identical whether it travels
through the container or through a manual export, with no second
sync mechanism to reason about. The KVS entitlement is not requested and
the (long dead) `didChangeExternallyNotification` observer is deleted.
Consequence: tombstone arrivals now reach the app only through
`NightStore`'s metadata query, so that query's predicate had to widen
from `slp_*.bin` to include `tombstones.json` — it was silently missing
them, which would have broken "tombstones win, eventually" in precisely
the fresh-install case the rule exists for.

**The container is user-visible.** `NSUbiquitousContainers` with a public
document scope puts a `SleepVue` folder in Files ▸ iCloud Drive holding
the raw `slp_YYYYMMDD.bin` files. ADR-0003's text and `NightStore`'s
header comment both already claimed this, but the plist entry was never
added, so it was never true. Being able to pull a night file onto a Mac
for `tools/plot_night.py` without the phone is worth real money during
algorithm work, and an inspectable backup beats an opaque one. The cost:
the user can delete or move files there, and the store is a mirror, so a
deletion in Files deletes the night. Accepted — same semantics as the
in-app delete, single known user. Note for future changes:
`NSUbiquitousContainers` edits only take effect when `CFBundleVersion`
increases (hence `CURRENT_PROJECT_VERSION` 1 → 2 here).

**Manual export/import is kept, not removed.** It is now redundant for
the ordinary backup case, and it is demoted below the iCloud status row
in Settings, but it remains the only backup available when signed out of
iCloud and the only one that leaves Apple's infrastructure entirely. It
also remains the migration tool between bundle identifiers — it is what
carried the night history across the `com.sleepvue.sync` →
`com.claero.sleepvue` rename.

**Consequences.** ADR-0003's narrowing of the "no network" claim applies
again and is now live rather than theoretical: nights and tombstones
leave the phone through the user's iCloud account. Multi-device remains
tolerated-by-construction (immutable files named by dateKey, union-merged
tombstones) but untested. The iCloud entitlement is now load-bearing for
signing: dropping the paid membership breaks the build again, and the
honest answer at that point is ADR-0004's manual mode, which is why none
of it was deleted.
