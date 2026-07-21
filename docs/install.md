# Raspberry Pi installation and rollback

These steps target 64-bit Raspberry Pi OS on a Pi 5. The repository does not yet contain measured Pi evidence.

## Build and verify

```sh
sudo apt-get update
sudo apt-get install --yes build-essential cmake libsqlite3-dev libssl-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake
sha256sum locallens-0.2.0-rc1-Linux.tar.gz
```

Compare the checksum with the CI artifact before installing. A checksum detects corruption; this candidate does not claim signed release provenance.

## Station setup

1. Mount the encrypted SSD at `/var/lib/locallens`.
2. Create a locked `locallens` system user, create `/var/lib/locallens/store`, and make the user the owner of both paths.
3. Install the archive into `/usr/local` and copy the systemd units from `packaging/systemd`.
4. Mount the backup target at `/mnt/locallens-backup` and the separately held recovery media at `/media/locallens-recovery`. Create the recovery key once with `openssl rand -hex 32`, owned by `locallens` with mode `0600`, then remount or expose that media read-only to the service. Use a systemd override if the station needs different paths.
5. Start the health unit. Enable the backup timer only after a manual backup, verification, and clean-target restore succeed.

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now locallens.service
sudo systemctl start locallens-backup.service
sudo systemctl enable --now locallens-backup.timer
systemctl list-timers locallens-backup.timer
```

Import remains an explicit operator command after confirming the mounted card path. The importer never modifies or deletes source files.

## Update and rollback

Before an update, stop imports, run and verify a backup, copy `catalog.db`, record `locallens <db> version`, then install the checksummed candidate. Validate `health`, timeline, one playback range, and backup verification before resuming imports.

Rollback replaces the binary, catalog copy, and units with the previous tested set. Never downgrade a catalog whose schema version is newer than the previous binary supports; restore the pre-update catalog copy or restore into a clean root instead.

For interrupted shutdown or suspected filesystem damage, keep camera cards untouched, stop LocalLens operations, check the encrypted SSD, and restore into a new empty directory. Do not restore over the only catalog or media copy.
