import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
for path in [*root.glob("*.json"), *root.glob("*.jsonl"), *root.glob("*.log")]:
    for line in path.read_text(errors="strict").splitlines():
        if line.strip().startswith("{"):
            json.loads(line)
