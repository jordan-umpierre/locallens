# Architecture decision records

## ADR-LL-001: One local C++ binary

- Status: accepted
- Decision: keep import, catalog, search, preview, and backup operations in one C++ process with command-level boundaries.
- Alternatives: local services add authentication and lifecycle failure modes; separate binaries duplicate schema and configuration ownership.
- Trigger: split only when a long-running UI/API or independently scheduled operation is implemented and needs process isolation.

## ADR-LL-002: SQLite metadata plus content-addressed files

- Status: accepted
- Decision: store searchable metadata, provenance, audit, and grants in SQLite/FTS5; store media bytes by SHA-256 on the filesystem.
- Alternatives: database blobs make large-media backup and playback awkward; filesystem-only metadata weakens transactions, provenance, and search.
- Trigger: replace SQLite only after measured concurrent writers or catalog size exceed its documented operating envelope on target hardware.

## ADR-LL-003: Authenticated encrypted filesystem backup

- Status: accepted
- Decision: encrypt each catalog/media object with AES-256-GCM, keep the recovery key separate, record plaintext and ciphertext hashes, and verify before restore.
- Alternatives: archive-level encryption makes partial verification/restore coarse; unencrypted copies violate the private-media threat boundary.
- Trigger: add a cloud adapter only with a real provider, credential model, retry contract, retention policy, and restore drill.

## ADR-LL-004: Immutable observations, auditable corrections

- Status: accepted
- Decision: retain observed timestamps/layout/provenance and store user time, tag, and note corrections separately with actor audit.
- Alternatives: overwriting observations destroys forensic context; refusing corrections makes uncertain camera clocks unusable.
- Trigger: add richer versioning only when multi-user edit conflicts exist.

## ADR-LL-005: Local-only privacy boundary

- Status: accepted
- Decision: expose local CLI results and opaque short-lived playback grants; do not add a network listener or analytics.
- Alternatives: a local web service increases attack surface before a user-facing management interface exists; direct paths leak storage layout.
- Trigger: introduce a listener only with authentication, origin policy, TLS/deployment boundary, range tests, and explicit retention behavior.
