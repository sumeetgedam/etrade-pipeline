#!/usr/bin/env python3
# compute_latency_stat.py <csv>
# prints count, min, max, mean, p50, p95, p99 ( latency_us column expeceted as 4th column )

import sys, csv, statistics

if len(sys.argv) < 2:
    print("Usage: compute_latency_stats.py <csv>")
    sys.exit(1)

vals = []

with open(sys.argv[1]) as f:
    r = csv.reader(f)
    next(r) # header
    for row in r:
        try:
            vals.append(float(row[3]))
        except:
            pass

if not vals:
    print("no samples")
    sys.exit(0)

vals.sort()

def pct(p):
    i = int(len(vals)*p/100.0)
    i = min(max(i, 0), len(vals)-1)

    return vals[i]

print("count  : ", len(vals))
print("min(us) : ", min(vals))
print("max(us) : ", max(vals))
print("mean(us) : ", statistics.mean(vals))
print("p50(us) : ", pct(50))
print("p95(us) : ", pct(95))
print("p99(us) : ", pct(99))
