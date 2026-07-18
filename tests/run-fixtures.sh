#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-./build/locallens}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/build/test-run"

rm -rf "$TMP"
mkdir -p "$TMP/cards/70mai/DCIM/Movie" "$TMP/cards/tapo/Tapo/Record" "$TMP/cards/reolink/DCIM/RECORD" "$TMP/store"

printf 'front-camera\n' > "$TMP/cards/70mai/DCIM/Movie/2026_07_18_120000.mp4"
printf 'doorbell\n' > "$TMP/cards/tapo/Tapo/Record/event.mov"
printf 'patio\n' > "$TMP/cards/reolink/DCIM/RECORD/clip.avi"
printf 'front-camera\n' > "$TMP/cards/reolink/DCIM/RECORD/duplicate.mp4"
ln -s /etc/passwd "$TMP/cards/70mai/DCIM/Movie/escape.mp4"

"$BIN" "$TMP/catalog.db" import "$TMP/cards/70mai" "$TMP/store" > "$TMP/70mai.log"
"$BIN" "$TMP/catalog.db" import "$TMP/cards/tapo" "$TMP/store" > "$TMP/tapo.log"
"$BIN" "$TMP/catalog.db" import "$TMP/cards/reolink" "$TMP/store" > "$TMP/reolink.log"
"$BIN" "$TMP/catalog.db" status > "$TMP/status.json"

grep -q '"layout":"70mai-a810"' "$TMP/70mai.log"
grep -q '"layout":"tapo-d210"' "$TMP/tapo.log"
grep -q '"layout":"reolink-argus-pt"' "$TMP/reolink.log"
grep -q '"sessions":3,"objects":3,"sources":4,"errors":0' "$TMP/status.json"
test "$(find "$TMP/store/media" -type f | wc -l | tr -d ' ')" = "3"

echo "fixture imports passed"
