#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-./build/locallens}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/build/test-run"
export LOCALLENS_TRACE_ID="network-test-29"

rm -rf "$TMP"
mkdir -p "$TMP/cards/70mai/DCIM/Movie" "$TMP/cards/tapo/Tapo/Record" "$TMP/cards/reolink/DCIM/RECORD" "$TMP/store"

printf 'front-camera\n' > "$TMP/cards/70mai/DCIM/Movie/2026_07_18_120000.mp4"
printf 'control-name\n' > "$TMP/cards/70mai/DCIM/Movie/"$'control\nname.mp4'
printf 'doorbell\n' > "$TMP/cards/tapo/Tapo/Record/event.mov"
printf 'patio\n' > "$TMP/cards/reolink/DCIM/RECORD/clip.avi"
printf 'front-camera\n' > "$TMP/cards/reolink/DCIM/RECORD/duplicate.mp4"
ln -s /etc/passwd "$TMP/cards/70mai/DCIM/Movie/escape.mp4"

"$BIN" "$TMP/catalog.db" import "$TMP/cards/70mai" "$TMP/store" > "$TMP/70mai.log"
"$BIN" "$TMP/catalog.db" import "$TMP/cards/tapo" "$TMP/store" > "$TMP/tapo.log"
"$BIN" "$TMP/catalog.db" import "$TMP/cards/reolink" "$TMP/store" > "$TMP/reolink.log"
"$BIN" "$TMP/catalog.db" status > "$TMP/status.json"
"$BIN" "$TMP/catalog.db" health "$TMP/store" > "$TMP/health-before-backup.json"
"$BIN" "$TMP/catalog.db" version > "$TMP/version.txt"
"$BIN" "$TMP/catalog.db" timeline > "$TMP/timeline.jsonl"
"$BIN" "$TMP/catalog.db" search 'Movie " ; *' 10 > "$TMP/search-escaped.jsonl"
"$BIN" "$TMP/catalog.db" annotate 1 2026-07-18T07:00:00-05:00 'front reviewed' 'clock drift corrected' local-user > "$TMP/annotate.json"
"$BIN" "$TMP/catalog.db" search reviewed 10 > "$TMP/search-annotated.jsonl"
for BAD_LIMIT in -1 0 501; do
  if "$BIN" "$TMP/catalog.db" search reviewed "$BAD_LIMIT" > "$TMP/bad-limit-$BAD_LIMIT.json" 2> "$TMP/bad-limit-$BAD_LIMIT.log"; then
    echo "invalid search limit unexpectedly succeeded" >&2
    exit 1
  fi
done
"$BIN" "$TMP/catalog.db" preview 1 "$TMP/store/derived" 5 > "$TMP/preview.json"
"$BIN" "$TMP/catalog.db" grant 1 > "$TMP/grant.json"
TOKEN="$(sed -n 's/.*"token":"\([^"]*\)".*/\1/p' "$TMP/grant.json")"
"$BIN" "$TMP/catalog.db" read "$TOKEN" 0 5 > "$TMP/range.bin"
"$BIN" "$TMP/catalog.db" backup "$TMP/store" "$TMP/remote" "$TMP/recovery.key" > "$TMP/backup.json"
MANIFEST="$(sed -n 's/.*"manifest_id":"\([^"]*\)".*/\1/p' "$TMP/backup.json")"
"$BIN" "$TMP/catalog.db" verify-backup "$TMP/remote" "$TMP/recovery.key" "$MANIFEST" > "$TMP/verify.json"
"$BIN" "$TMP/catalog.db" health "$TMP/store" > "$TMP/health-after-backup.json"
if "$BIN" "$TMP/catalog.db" verify-backup "$TMP/remote" "$TMP/missing.key" "$MANIFEST" > "$TMP/missing-key.json" 2> "$TMP/missing-key.log"; then
  echo "missing recovery key unexpectedly verified" >&2
  exit 1
fi
test ! -e "$TMP/missing.key"
printf '0000000000000000000000000000000000000000000000000000000000000000\n' > "$TMP/wrong.key"
if "$BIN" "$TMP/catalog.db" verify-backup "$TMP/remote" "$TMP/wrong.key" "$MANIFEST" > "$TMP/wrong-key.json" 2> "$TMP/wrong-key.log"; then
  echo "wrong recovery key unexpectedly verified" >&2
  exit 1
fi
FIRST_OBJECT="$(find "$TMP/remote/objects" -type f | head -n 1)"
cp "$FIRST_OBJECT" "$TMP/object.good"
printf x >> "$FIRST_OBJECT"
if "$BIN" "$TMP/catalog.db" verify-backup "$TMP/remote" "$TMP/recovery.key" "$MANIFEST" > "$TMP/tamper.json" 2> "$TMP/tamper.log"; then
  echo "tampered backup unexpectedly verified" >&2
  exit 1
fi
cp "$TMP/object.good" "$FIRST_OBJECT"
cp "$TMP/remote/manifests/$MANIFEST.bin" "$TMP/manifest.good"
printf x >> "$TMP/remote/manifests/$MANIFEST.bin"
if "$BIN" "$TMP/catalog.db" verify-backup "$TMP/remote" "$TMP/recovery.key" "$MANIFEST" > "$TMP/manifest-tamper.json" 2> "$TMP/manifest-tamper.log"; then
  echo "tampered manifest unexpectedly verified" >&2
  exit 1
fi
cp "$TMP/manifest.good" "$TMP/remote/manifests/$MANIFEST.bin"
"$BIN" "$TMP/catalog.db" restore "$TMP/remote" "$TMP/restore" "$TMP/recovery.key" "$MANIFEST" > "$TMP/restore.json"
"$BIN" "$TMP/restore/catalog.db" status > "$TMP/restore-status.json"
"$BIN" "$TMP/catalog.db" backup "$TMP/store" "$TMP/remote" "$TMP/recovery.key" > "$TMP/backup-2.json"
MANIFEST_2="$(sed -n 's/.*"manifest_id":"\([^"]*\)".*/\1/p' "$TMP/backup-2.json")"
"$BIN" "$TMP/catalog.db" verify-backup "$TMP/remote" "$TMP/recovery.key" "$MANIFEST" > "$TMP/verify-old-after-new.json"
"$BIN" "$TMP/catalog.db" verify-backup "$TMP/remote" "$TMP/recovery.key" "$MANIFEST_2" > "$TMP/verify-2.json"
mkdir "$TMP/not-empty"
printf keep > "$TMP/not-empty/file"
if "$BIN" "$TMP/catalog.db" restore "$TMP/remote" "$TMP/not-empty" "$TMP/recovery.key" "$MANIFEST" > "$TMP/nonempty-restore.json" 2> "$TMP/nonempty-restore.log"; then
  echo "restore unexpectedly overwrote a non-empty target" >&2
  exit 1
fi

grep -q '"layout":"70mai-a810"' "$TMP/70mai.log"
grep -q '"layout":"tapo-d210"' "$TMP/tapo.log"
grep -q '"layout":"reolink-argus-pt"' "$TMP/reolink.log"
grep -q '"sessions":3,"objects":4,"sources":5,"errors":0' "$TMP/status.json"
grep -q '"status":"ok","schema_version":2' "$TMP/health-before-backup.json"
grep -q '"latest_backup":""' "$TMP/health-before-backup.json"
grep -q '"latest_verification":"ok"' "$TMP/health-after-backup.json"
grep -q '^0.2.0-rc1$' "$TMP/version.txt"
grep -q '"timezone":"UTC"' "$TMP/timeline.jsonl"
grep -q '"provenance_count":2' "$TMP/timeline.jsonl"
grep -q '"media_id":1' "$TMP/search-escaped.jsonl"
grep -q '"event":"annotation_added"' "$TMP/annotate.json"
grep -q '"media_id":1' "$TMP/annotate.json"
grep -q '"media_id":1' "$TMP/search-annotated.jsonl"
grep -q '"event":"preview_created"' "$TMP/preview.json"
grep -q '"media_id":1,"bytes":5' "$TMP/preview.json"
grep -q '"event":"backup_complete"' "$TMP/backup.json"
grep -q '"trace_id":"network-test-29"' "$TMP/backup.json"
grep -q '"event":"backup_verified"' "$TMP/verify.json"
grep -q '"event":"restore_complete"' "$TMP/restore.json"
grep -q '"sessions":3,"objects":4,"sources":5,"errors":0' "$TMP/restore-status.json"
test "$(cat "$TMP/range.bin")" = "front"
if "$BIN" "$TMP/catalog.db" read "$TOKEN" 0 1048577 > "$TMP/large-range.bin" 2> "$TMP/large-range.log"; then
  echo "oversized playback range unexpectedly succeeded" >&2
  exit 1
fi
if "$BIN" "$TMP/catalog.db" read ../../etc/passwd 0 5 > "$TMP/bad-range.bin" 2> "$TMP/bad-range.log"; then
  echo "path traversal grant unexpectedly succeeded" >&2
  exit 1
fi
grep -q '"trace_id":"network-test-29","code":"operation_failed"' "$TMP/bad-range.log"
if grep -q 'passwd' "$TMP/bad-range.log"; then
  echo "fatal log leaked caller input" >&2
  exit 1
fi
test "$(find "$TMP/store/media" -type f | wc -l | tr -d ' ')" = "4"
test "$(find "$TMP/remote/objects" -type f | wc -l | tr -d ' ')" = "10"
test "$(find "$TMP/restore/media" -type f | wc -l | tr -d ' ')" = "4"
test "$(cat "$TMP/cards/70mai/DCIM/Movie/2026_07_18_120000.mp4")" = "front-camera"
python3 "$ROOT/tests/validate-json.py" "$TMP"

echo "fixture imports passed"
