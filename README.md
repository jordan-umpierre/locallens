# LocalLens Release Candidate

LocalLens is a CLI release candidate for importing removable camera-card exports into a verified local media store, then indexing them into a SQLite catalog for chronology, search, provenance, correction audit, derived previews, opaque playback grants, and encrypted recovery.

## Scope

- Supported now: manual SD-card import from mounted folders, durable staging, SHA-256 verification, atomic promotion, provenance-preserving dedupe, SQLite FTS search, chronological listing, timestamp/tag/note correction records, bounded preview assets, opaque grant-based range reads, client-side AES-256-GCM backup objects and manifests, ciphertext verification, clean restore, health/capacity status, JSON-line logs, fixture tests.
- Not included: private cloud APIs, wireless extraction, transcoding, camera control, automatic source deletion, facial recognition, license-plate surveillance, cloud inference, S3 SDK upload adapter, or production Pi throughput claims.

## Build

```sh
make test
```

On a Raspberry Pi or Linux workstation with CMake:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Use

```sh
./build/locallens var/catalog.db import /media/card var/store
./build/locallens var/catalog.db status
./build/locallens var/catalog.db health var/store
./build/locallens var/catalog.db timeline
./build/locallens var/catalog.db search "front reviewed"
./build/locallens var/catalog.db annotate 1 2026-07-18T07:00:00-05:00 "front reviewed" "clock drift corrected" local-user
./build/locallens var/catalog.db preview 1 var/store/derived 65536
./build/locallens var/catalog.db grant 1
./build/locallens var/catalog.db read <grant-token> 0 1048576 > clip-range.bin
./build/locallens var/catalog.db backup var/store var/remote var/recovery.key
./build/locallens var/catalog.db verify-backup var/remote var/recovery.key <manifest-id>
./build/locallens var/catalog.db restore var/remote var/restore var/recovery.key <manifest-id>
```

The importer never modifies or deletes source media. It skips symlinks while scanning, copies files to staging, hashes source and staged bytes, then promotes verified files into `store/media/<sha-prefix>/<sha-prefix>/<sha>.<ext>`.

Playback never accepts filesystem paths. `grant` creates a short-lived opaque token for one catalog media id, and `read` streams only the requested byte range for that grant.

Backup writes versioned encrypted manifests and AES-256-GCM ciphertext objects under the remote target. The recovery key is a separate 32-byte hex file with owner-only permissions; keep it offline and away from the remote backup. The current target is a filesystem-backed provider simulation used for restore drills. An S3 multipart adapter is a later transport layer, not current evidence.

The proportional CLI/local-stream failure, retry, correlation, redaction, and alert policy is in [`docs/network-contract.md`](docs/network-contract.md).

The code-first [architecture review](docs/architecture.md) and [decision records](docs/adr/README.md) trace the current boundaries to the binary, schema, filesystem, and deployment evidence.
The [operations runbook](docs/operations.md) records ownership, health, alerts, retention, restore, migration, rollback, key rotation, dependency outage, and cost procedures.
The [release-readiness matrix](docs/evidence/release-readiness.md) separates verified candidate behavior from hardware, UI, provider, and signing blockers.

## Verification

Current local evidence:

```sh
make test
```

The fixture test creates synthetic 70mai, Tapo, and Reolink card layouts plus control-character filenames and a symlink escape attempt. Expected result: 3 import sessions, 4 unique media objects, 5 provenance rows, and 0 errors. It also validates every emitted JSON line, strict encrypted-manifest parsing, positive bounded search limits, FTS escaping, chronological timezone output, duplicate provenance, audited annotation search, bounded preview generation, opaque range playback, path-traversal grant rejection, AES-GCM backup, missing/wrong-key failure, object and manifest tampering, old-set verification after a newer backup, clean-target enforcement, restored inventory, and source-file immutability.

## Security and Privacy

Card contents are treated as hostile input. The scanner does not follow symlinks, source files are read-only by default, logs contain relative paths and hashes, stored media is content-addressed, playback uses catalog ids plus short grants instead of exposed paths, and backup objects are encrypted locally before leaving the station. Production deployment should place `var/store` on an encrypted SSD and run the service with least-privilege systemd sandboxing.

## Limitations

This is `0.2.0-rc1`, not a production release. Hardware mount detection, Pi throughput/thermal measurement, power-loss rehearsal, browser management UI, live S3-compatible storage, signed artifacts, real-camera validation, and media-container timestamp extraction remain pending. Filesystem timestamps use `filesystem` confidence and `UTC` unless corrected by a user annotation.
