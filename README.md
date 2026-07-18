# LocalLens Ingestion Station

LocalLens imports removable camera-card exports into a verified local media store. This milestone covers fixture-backed layouts for 70mai A810, Tapo D210, and Reolink Argus PT cards, content hashing, immutable storage, dedupe, timestamp capture, SQLite provenance, status output, and a safe-eject audit signal.

## Scope

- Supported now: manual SD-card import from mounted folders, staging, SHA-256 verification, atomic promotion, provenance-preserving dedupe, SQLite catalog, JSON-line logs, fixture tests.
- Not included: private cloud APIs, wireless extraction, transcoding, camera control, automatic source deletion, or production Pi throughput claims.

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
```

The importer never modifies or deletes source media. It skips symlinks while scanning, copies files to staging, hashes source and staged bytes, then promotes verified files into `store/media/<sha-prefix>/<sha-prefix>/<sha>.<ext>`.

## Verification

Current local evidence:

```sh
make test
```

The fixture test creates synthetic 70mai, Tapo, and Reolink card layouts plus a symlink escape attempt. Expected result: 3 import sessions, 3 unique media objects, 4 provenance rows, and 0 errors.

## Security and Privacy

Card contents are treated as hostile input. The scanner does not follow symlinks, source files are read-only by default, logs contain relative paths and hashes, and stored media is content-addressed. Production deployment should place `var/store` on an encrypted SSD and run the service with least-privilege systemd sandboxing.

## Limitations

Hardware mount detection, Pi throughput measurement, power-loss rehearsal, and media-container timestamp extraction are still pending real device access. This milestone records filesystem timestamps with `filesystem` confidence.
