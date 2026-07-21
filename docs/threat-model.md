# Threat Model

## Assets

- Original camera-card media
- Imported immutable media store
- SQLite catalog and provenance
- Playback grants and derived preview assets
- Encrypted backup objects and manifests
- Offline recovery key material
- Operator privacy and filenames

## Trust Boundaries

- Removable SD cards are untrusted.
- The encrypted SSD store is trusted after mount.
- Remote backup storage is untrusted and receives ciphertext only.
- Recovery key storage is separate from the remote backup target.
- CLI arguments are operator-provided but validated before use.

## Threats and Mitigations

- Path escape through symlinks: recursive scanner skips symlinks.
- Source media loss: importer never deletes or modifies source files.
- Corrupt copy: source and staged SHA-256 must match before promotion.
- Duplicate imports: content hash is unique while source provenance is preserved.
- Path disclosure through playback: playback reads require short opaque grants tied to catalog media ids.
- Search injection or malformed query input: search terms are normalized before SQLite FTS matching.
- Correction tampering: user corrections append annotation rows and audit events instead of overwriting observed metadata.
- Preview parser failure: current preview generation is a bounded byte copy and records derived output separately from originals.
- Provider compromise: backup objects are encrypted locally with AES-256-GCM before writing to the remote target.
- Backup tampering: verification checks remote ciphertext hashes and authenticated decryption before restore.
- Manifest tampering/path traversal: manifests are authenticated ciphertext, restored paths must stay relative, and restore refuses a non-empty target.
- Backup-set overwrite: remote object names use each ciphertext hash, so later randomized encryption cannot replace bytes referenced by an earlier manifest.
- Key loss: restore requires separately held recovery material; missing or wrong keys fail closed.
- Catalog corruption after interruption: SQLite WAL and per-file idempotent writes allow reruns.
- Sensitive logs: logs are structured and avoid file contents.

## Residual Risk

The remote target is currently filesystem-backed test storage, not a live S3 adapter. Backup currently buffers a complete object in memory, so real video archives require chunked streaming before production use. Media container parsing and browser LAN access are not enabled in this milestone. Add sandboxed metadata tooling when container timestamps become required evidence, and keep LAN binding disabled until local authentication is configured.
