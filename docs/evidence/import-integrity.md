# Import Integrity Evidence

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

Expected status:

```json
{"sessions":3,"objects":3,"sources":4,"errors":0}
```

Hardware evidence is intentionally not claimed until Raspberry Pi 5, multicard reader, and SSD tests are run.
