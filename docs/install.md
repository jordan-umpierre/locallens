# Raspberry Pi Install Notes

1. Build with CMake on Raspberry Pi OS.
2. Mount the encrypted SSD at `/var/lib/locallens`.
3. Create a least-privilege `locallens` user.
4. Install `build/locallens` to `/usr/local/bin/locallens`.
5. Install `packaging/systemd/locallens.service`.

Rollback is replacing `/usr/local/bin/locallens` with the previous tested binary and restarting the unit. This milestone does not auto-import on udev events; invoke `locallens <db> import <mount> <store>` after confirming the mounted card path.
