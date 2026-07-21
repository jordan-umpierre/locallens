# Operations and recovery

LocalLens is a single-host Linux service with a SQLite catalog, immutable media store, encrypted backup target, and separately held recovery key. No cloud service, remote API, or achieved Raspberry Pi recovery time is claimed.

| Concern | Current control | Operator action |
|---|---|---|
| Health | `locallens <catalog> health <store>` and the hardened one-shot systemd unit report schema, capacity, import errors, and backup state | Investigate `review-required` or a non-zero exit; do not ingest until the catalog and store paths are writable by only the service account. |
| Logs | JSON lines with operation event and trace ID; fatal details redact caller input | Alert on `operation_failed`, failed backup verification, or any import error. Do not collect filenames beyond the documented relative-path boundary. |
| Backup | Client-side AES-256-GCM objects plus an authenticated encrypted manifest; an optional persistent daily timer is packaged | Enable only after a manual restore drill; retain the last 7 daily and 4 weekly manifests once pruning exists. Keep the recovery key offline and separate. |
| Restore | `restore` writes into a clean directory and verifies every plaintext hash | Run `make test` for the fixture drill, then verify restored status before switching paths. Never restore over the only catalog or media copy. |

## Runbook

- **Target:** future single-host production RPO 24 hours and RTO 4 hours; real Pi timing remains release evidence.
- **Migration:** stop ingestion, back up, copy the catalog, install the tested binary, and run `status`. SQLite schema changes are transactional; forward repair is preferred.
- **Rollback:** restore the previous binary, catalog copy, and unit file, then run `status`. If catalog/media integrity is uncertain, restore the encrypted backup into a new root.
- **Dependency outage:** removable media stays untouched; remote-target failure leaves local catalog/media authoritative. Resume backup with the same manifest only after verifying the target.
- **Credential rotation:** create a new offline recovery key and full backup set, verify it, retain the old set through the retention window, then destroy the old key according to the device-owner procedure.
- **Cost:** owner is Jordan Umpierre. Local storage and any future off-site target stay within Stackwalk's $80 soft alert and $100 ordinary monthly ceiling.
- **Configuration recovery:** the tagged binary and source-controlled systemd unit reconstruct service configuration. Keys and device-specific paths remain outside source.

Evidence: `make test` covers encrypted backup, missing/wrong-key rejection, object and manifest tampering, prior-set verification after a later backup, non-empty-target refusal, clean catalog/media restore, and restored inventory.
