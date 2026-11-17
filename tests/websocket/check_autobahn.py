"""Reads an Autobahn report (index.json) and fails if any case FAILED.
OK, NON-STRICT, INFORMATIONAL and UNIMPLEMENTED (compression, which is not
offered) are accepted."""
import json, sys
from collections import Counter

path, agent = sys.argv[1], sys.argv[2]
report = json.load(open(path))[agent]
counts = Counter()
failed = []
for case, r in sorted(report.items(), key=lambda kv: [int(x) for x in kv[0].split(".")]):
    counts[r["behavior"]] += 1
    if r["behavior"] == "FAILED" or r.get("behaviorClose") == "FAILED":
        failed.append(f'{case}: behavior={r["behavior"]} close={r.get("behaviorClose")} code={r.get("remoteCloseCode")}')
print(f"{path}: {len(report)} cases, " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
for f in failed:
    print("FAILED", f)
sys.exit(1 if failed or not report else 0)
