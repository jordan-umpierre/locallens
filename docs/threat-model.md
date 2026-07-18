# Threat Model

## Assets

- Original camera-card media
- Imported immutable media store
- SQLite catalog and provenance
- Operator privacy and filenames

## Trust Boundaries

- Removable SD cards are untrusted.
- The encrypted SSD store is trusted after mount.
- CLI arguments are operator-provided but validated before use.

## Threats and Mitigations

- Path escape through symlinks: recursive scanner skips symlinks.
- Source media loss: importer never deletes or modifies source files.
- Corrupt copy: source and staged SHA-256 must match before promotion.
- Duplicate imports: content hash is unique while source provenance is preserved.
- Catalog corruption after interruption: SQLite WAL and per-file idempotent writes allow reruns.
- Sensitive logs: logs are structured and avoid file contents.

## Residual Risk

Media container parsing is not enabled in this milestone. Add sandboxed metadata tooling when container timestamps become required evidence.
