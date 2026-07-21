# Catalog and Import Evidence

Command:

```sh
make test
```

Coverage:

- 70mai A810 fixture layout: `DCIM/Movie`
- Tapo D210 fixture layout: `Tapo/Record`
- Reolink Argus PT fixture layout: `DCIM/RECORD`
- Symlink escape attempt is skipped
- Duplicate bytes produce one media object and multiple provenance rows
- Chronology reports explicit `UTC` timezone and timestamp confidence
- SQLite FTS handles escaped punctuation input
- User timestamp/tag/note corrections are audited without changing observations
- Search indexes annotation tags and notes
- Bounded preview generation records a derived asset
- Playback uses an opaque grant and byte range, not a filesystem path
- Invalid path-like grant tokens are rejected
- Each backup writes four immutable encrypted remote objects: catalog plus three media files
- A second backup cannot invalidate verification of the first set
- Backup manifests are encrypted and authenticated
- Missing recovery keys fail without creating replacement key material
- Remote verification rejects wrong recovery keys
- Remote verification rejects ciphertext tampering
- Restore to a clean directory recreates the catalog and media inventory
- Restore refuses to overwrite a non-empty target
- Original fixture media remains unchanged after import, preview, annotation, and playback

Expected status:

```json
{"sessions":3,"objects":3,"sources":4,"errors":0}
```

Hardware evidence is intentionally not claimed until Raspberry Pi 5, multicard reader, and SSD tests are run.
