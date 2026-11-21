"""Validate a completed Windows Raw resource lab run (Python standard library)."""
import csv
import sys
from pathlib import Path
from statistics import mean


def read(directory, filename):
    with (directory / filename).open(newline="") as source:
        return [
            {k: v if k in ("scenario", "mode") else int(v) for k, v in row.items()}
            for row in csv.DictReader(source, delimiter="\t")
        ]


def select(rows, case, mode):
    return [r for r in rows if r["scenario"] == case and r["mode"] == mode]


def check(directory):
    totals = read(directory, "flows.tsv")
    windows = read(directory, "windows.tsv")
    allocation = read(directory, "allocation.tsv")
    cases = {"one20", "pair100", "weighted", "eight1pct", "many300",
             "classes", "churn", "outage", "ramp"}
    assert {r["scenario"] for r in totals} == cases
    for case in sorted(cases):
        raw = select(totals, case, "raw")
        balanced = select(totals, case, "balanced")
        assert [r["offered_B"] for r in raw] == [r["offered_B"] for r in balanced]
        for r in raw + balanced:
            assert r["offered"] >= r["accepted"] == r["sent"] + r["expired"]
            assert r["sent"] == r["delivered"] + r["lost"]
        trace = select(windows, case, "balanced")
        assert len(trace) == 43
        assert max(r["local_peak_B"] for r in trace) <= 2 * 1145 * len(balanced)
        assert max(r["local_age_us"] for r in trace) <= 200000
        assert max(r["flight_peak_B"] for r in trace) <= 1024 * 1024
        assert max(r["link_peak_B"] for r in trace) <= 256 * 1024
        if case not in {"ramp", "outage"}:
            low = mean(r["sent_charge"] for r in trace[18:24]) * 8e-6
            high = mean(r["useful_bytes"] for r in trace[32:40]) * 8e-6
            assert low <= 2.5 and high >= 4.5, (case, low, high)
        if case in {"pair100", "eight1pct", "many300", "weighted"}:
            shares = [r["useful_B"] / r["weight"] for r in balanced]
            fairness = sum(shares) ** 2 / (len(shares) * sum(v*v for v in shares))
            assert fairness > .995, (case, fairness)
        if case == "classes":
            control = balanced[0]
            assert control["p95_us"] < raw[0]["p95_us"] / 2
            phases = select(allocation, case, "balanced")
            assert all(r["useful_B"] > 0 for r in phases if 18 <= r["second"] < 24)
        if case == "churn":
            phases = select(allocation, case, "balanced")
            assert all(r["useful_B"] == 0 for r in phases
                       if r["flow"] >= 4 and (r["second"] < 8 or r["second"] >= 30))
    print("PASS: offered-load parity, conservation, queue and flight limits, "
          "equal/weighted allocation, priority, active-set changes and step response")


if __name__ == "__main__":
    if not __debug__:
        raise SystemExit("Run without -O to enable validation")
    check(Path(sys.argv[1]))
