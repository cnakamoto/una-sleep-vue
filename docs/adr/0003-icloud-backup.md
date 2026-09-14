# iCloud backup for the iOS companion app

The iOS app's founding principle was "no account, no server, no network
access — everything stays on the phone," but that meant deleting the app
(or losing the phone) destroyed all synced nights, and with
prune-after-sync enabled there was no copy anywhere. We added a backup
keyed to the user's Apple account: night files live in the app's iCloud
Drive container (writing a file *is* the backup; restore is automatic on
a fresh install), and tombstones live in iCloud's key-value store, one key
per night, so deletions survive reinstall too. Backup is mirrored, not
append-only: deleting a night deletes the backup copy.

**Considered options.** DigitalOcean Spaces (the original suggestion) was
rejected: "keyed to Apple ID" requires an auth server (Sign in with Apple
→ scoped credentials) we don't want to build or run, and without it a
shared secret in the binary would own the bucket — plus $5/mo for ~4 KB
per night. CloudKit was rejected as record-mapping overhead for data that
is already files. Settings and the debug log are deliberately *not*
backed up: toggles are cheap to re-flip and restoring them would have
surprising failure modes.

**Consequences.** The README/privacy "no network" claim is narrowed:
BLE-only for watch traffic, but nights and tombstones now sync through
the user's iCloud account. Reconciliation invariant: tombstones always
win, eventually (a BLE sync can race a restore on a fresh install).
Multi-device is tolerated by construction (immutable files named by
dateKey; union-merged tombstones) but not supported or tested.
