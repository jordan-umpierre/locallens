# Release candidate readiness

Candidate: `0.2.0-rc1`. Date: July 18, 2026. All automated evidence uses synthetic files on macOS unless stated otherwise.

| Release criterion | Status | Evidence or blocker |
|---|---|---|
| Three supported export layouts | Verified fixture | Synthetic 70mai A810, Tapo D210, and Reolink Argus PT layouts import without source mutation. |
| Catalog, search, corrections, playback | Verified local | `make test` covers chronology, escaped FTS input, audited correction, bounded preview, opaque grant, and range limits. |
| Encrypted backup and clean restore | Verified local | AES-256-GCM objects and encrypted manifests reject wrong/missing keys, object/manifest tampering, and non-empty targets. Two successive backups retain verification of the older set. |
| Health, schema, capacity, backup status | Verified local | `health` reports schema version, storage capacity/availability, import errors, latest backup, and latest verification. |
| Linux packaging and checksums | CI-defined | CMake/CPack and GitHub Actions build a Linux candidate archive and SHA-256 file. A successful public CI run is not claimed in this workspace. |
| Least-privilege service and backup timer | Source-verified | Hardened systemd health and optional daily backup units are packaged; target-host execution remains pending. |
| Real Raspberry Pi 5, SSD, reader | Pending hardware | Install, thermal, throughput, disk-pressure, unplug, shutdown, migration, and rollback drills require the target station. |
| Three real camera cards and playback | Pending hardware | No private footage or real export-layout result is published or claimed. |
| Browser UI and WCAG audit | Not implemented | The candidate is CLI-only. A React/private-LAN surface requires authentication and real browser validation before enabling a listener. |
| S3-compatible provider | Not implemented | Backup currently targets a mounted filesystem. Live provider retry, credentials, retention, cost, and restore evidence remain pending. |
| Signed release and public tag | Pending external | CI produces checksums only. Sign and tag only after hardware acceptance and clean-station recovery pass. |

Result: the software is a locally verified release candidate, not the Space 38 production release. The production definition of done remains blocked by the explicit hardware, UI/accessibility, provider, and signed-release rows above.
