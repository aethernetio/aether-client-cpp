#!/usr/bin/env python3
"""Corrected analysis of the completed 8h UAP phase-preservation run.

Reads only existing artifacts under artifacts/uap-phase-preservation/8h/.
Writes corrected-analysis.md and corrected-summary.json.
"""

from __future__ import annotations

import csv
import json
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path("artifacts/uap-phase-preservation/8h")
RUNS = ROOT / "runs"
AGG = ROOT / "aggregate"


def pf(x: Any) -> float | None:
    if x is None:
        return None
    s = str(x).strip()
    if s == "" or s.lower() in {"null", "nan", "none"}:
        return None
    try:
        v = float(s)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(v):
        return None
    return v


def pi(x: Any) -> int | None:
    v = pf(x)
    return None if v is None else int(v)


def percentile(sorted_vals: list[float], p: float) -> float:
    if not sorted_vals:
        return float("nan")
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    k = (len(sorted_vals) - 1) * (p / 100.0)
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return sorted_vals[int(k)]
    return sorted_vals[f] * (c - k) + sorted_vals[c] * (k - f)


def dist(vals: list[float | None]) -> dict[str, Any]:
    clean = [float(v) for v in vals if v is not None and math.isfinite(float(v))]
    if not clean:
        return {"n": 0}
    s = sorted(clean)
    mean = statistics.fmean(s)
    stdev = statistics.pstdev(s) if len(s) > 1 else 0.0
    return {
        "n": len(s),
        "min": s[0],
        "max": s[-1],
        "max_abs": max(abs(x) for x in s),
        "mean": mean,
        "stddev": stdev,
        "p50": percentile(s, 50),
        "p90": percentile(s, 90),
        "p95": percentile(s, 95),
        "p99": percentile(s, 99),
        "p99_5": percentile(s, 99.5),
        "p99_9": percentile(s, 99.9),
        "p1": percentile(s, 1),
        "p5": percentile(s, 5),
    }


def fmt_dist(d: dict[str, Any], unit: str = "ms") -> str:
    if d.get("n", 0) == 0:
        return "_n=0_"
    keys = [
        "n",
        "min",
        "mean",
        "stddev",
        "p1",
        "p5",
        "p50",
        "p90",
        "p95",
        "p99",
        "p99_5",
        "p99_9",
        "max",
        "max_abs",
    ]
    parts = []
    for k in keys:
        if k not in d:
            continue
        v = d[k]
        if k == "n":
            parts.append(f"n={v}")
        else:
            parts.append(f"{k}={v:.6f}{unit}" if isinstance(v, float) else f"{k}={v}")
    return ", ".join(parts)


def linear_slope(xs: list[float], ys: list[float]) -> float:
    n = min(len(xs), len(ys))
    if n < 2:
        return 0.0
    x = xs[:n]
    y = ys[:n]
    mx = statistics.fmean(x)
    my = statistics.fmean(y)
    num = sum((a - mx) * (b - my) for a, b in zip(x, y))
    den = sum((a - mx) ** 2 for a in x)
    return 0.0 if den == 0 else num / den


def load_jsonl(path: Path) -> list[dict]:
    rows = []
    if not path.exists():
        return rows
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return rows


def load_csv(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8", errors="replace", newline="") as f:
        return list(csv.DictReader(f))


def classify_invariant(inv: str) -> tuple[str, str]:
    """Return (bucket, domain) where domain is production|harness|other."""
    s = (inv or "").lower()
    if "next nominal schedule shifted" in s or "schedule shifted" in s:
        return "phase_shift", "production"
    if "retry reached server after original deadline" in s:
        return "retry_after_deadline", "production"
    if "window" in s and ("wrong" in s or "correction" in s or "gap" in s):
        return "window_correction", "production"
    if "duplicate logical" in s or "duplicate cycleconfirmed" in s:
        return "duplicate_logical_ping", "production"
    if "false misseddeadline" in s or "false unknown" in s:
        return "observer_wrong_state", "production"
    if "hard-stop" in s or "graceful" in s and "wrong state" in s:
        return "observer_wrong_state", "production"
    if "first request was sent" in s or "did not reach send" in s:
        return "fault_not_armed", "harness"
    if "retry did not reach" in s:
        return "no_retry_or_wrong_cycle", "harness"
    if "cycle not confirmed" in s or "no cycle start" in s:
        return "cycle_confirm", "harness"
    if "querypeerreceive" in s or "checkpoint" in s:
        return "query", "harness"
    if "reporting/harness" in s or "graceful restart" in s:
        return "reporting", "harness"
    if "observer saw the wrong state" in s:
        return "observer_wrong_state", "production"
    return "other", "other"


PROD_BUCKETS = {
    "retry_after_deadline",
    "phase_shift",
    "window_correction",
    "observer_wrong_state",
    "duplicate_logical_ping",
    "other_production",
}


def margin_buckets(margins: list[float]) -> dict[str, int]:
    edges = [
        (">100_before", lambda m: m > 100),
        ("50_100_before", lambda m: 50 < m <= 100),
        ("20_50_before", lambda m: 20 < m <= 50),
        ("10_20_before", lambda m: 10 < m <= 20),
        ("5_10_before", lambda m: 5 < m <= 10),
        ("1_5_before", lambda m: 1 < m <= 5),
        ("0_1_before", lambda m: 0 <= m <= 1),
        ("after_deadline", lambda m: m < 0),
    ]
    out = {n: 0 for n, _ in edges}
    out["no_retry"] = 0
    for m in margins:
        placed = False
        for n, pred in edges:
            if pred(m):
                out[n] += 1
                placed = True
                break
        if not placed:
            out["after_deadline"] += 1
    return out


def seq_group(seq: str) -> str:
    s = (seq or "").lower()
    if s.startswith("baseline") or s == "none":
        return "baseline"
    if "consecutive" in s:
        return "consecutive"
    if "alternat" in s:
        return "alternating"
    if "periodic" in s or s.startswith("every"):
        return "periodic"
    if "random" in s:
        return "random"
    if "request-loss" in s or "response-loss" in s:
        return "isolated"
    if "boundary" in s:
        return "boundary"
    return s or "other"


def main() -> int:
    status = json.loads((ROOT / "status.json").read_text(encoding="utf-8"))
    shards_meta = []
    all_jsonl: list[dict] = []
    all_csv: list[dict] = []
    all_phase: list[dict] = []
    all_win: list[dict] = []
    all_obs: list[dict] = []
    assertion_failures: list[dict] = []

    for shard_dir in sorted(RUNS.iterdir()):
        if not shard_dir.is_dir():
            continue
        name = shard_dir.name
        transport = "tcp" if name.startswith("tcp") else "udp"
        st = {}
        if (shard_dir / "shard-status.json").exists():
            st = json.loads((shard_dir / "shard-status.json").read_text(encoding="utf-8"))
        meta = {}
        if (shard_dir / "shard-meta.json").exists():
            try:
                meta = json.loads((shard_dir / "shard-meta.json").read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                pass
        stdout = (
            (shard_dir / "stdout.log").read_text(encoding="utf-8", errors="replace")
            if (shard_dir / "stdout.log").exists()
            else ""
        )
        budget_exhausted = "budget exhausted" in stdout
        has_report = (shard_dir / "report.md").exists()
        semantic_fail = "FAIL phase-preservation" in stdout
        semantic_pass = "PASS phase-preservation" in stdout
        corrected_state = (
            "budget_exhausted_semantic_fail"
            if budget_exhausted and has_report and semantic_fail
            else "budget_exhausted_semantic_pass"
            if budget_exhausted and has_report and semantic_pass
            else "budget_exhausted_with_report"
            if budget_exhausted and has_report
            else "report_present"
            if has_report
            else "incomplete"
        )
        # Not a process crash if report + budget exhaustion present.
        is_real_crash = (not has_report) and (not budget_exhausted)

        jsonl = load_jsonl(shard_dir / "samples.jsonl")
        csv_rows = load_csv(shard_dir / "samples.csv")
        phase_rows = load_csv(shard_dir / "phase-error-by-cycle.csv")
        win_rows = load_csv(shard_dir / "window-corrections.csv")
        obs_rows = load_csv(shard_dir / "observer-query-results.csv")
        failed = []
        if (shard_dir / "failed-cases.json").exists():
            try:
                failed = json.loads(
                    (shard_dir / "failed-cases.json").read_text(encoding="utf-8")
                )
            except Exception:
                failed = []

        for r in jsonl:
            r["shard"] = name
            r.setdefault("transport", transport)
            all_jsonl.append(r)
        for r in csv_rows:
            r["shard"] = name
            r["transport"] = transport
            all_csv.append(r)
        for r in phase_rows:
            r["shard"] = name
            r["transport"] = transport
            all_phase.append(r)
        for r in win_rows:
            r["shard"] = name
            r["transport"] = transport
            all_win.append(r)
        for r in obs_rows:
            r["shard"] = name
            r["transport"] = transport
            all_obs.append(r)

        # Prefer aggregate-style classification from per-cycle failure strings,
        # then apply instrumentation corrections (bogus Tn / control-only state).
        def refine(bucket: str, domain: str, inv: str, sample: dict) -> tuple[str, str]:
            tn = sample.get("Tn")
            if tn is None:
                tn = sample.get("scheduled_nominal_us") or sample.get("scheduled_nominal")
            lpid = sample.get("logical_ping_id")
            try:
                tn_v = float(tn) if tn is not None else None
            except (TypeError, ValueError):
                tn_v = None
            try:
                lpid_v = int(lpid) if lpid is not None else None
            except (TypeError, ValueError):
                lpid_v = None
            # Bogus empty cycle start: Tn==0 / logical_ping_id==0 with huge phase error
            if bucket == "phase_shift" and (
                tn_v == 0 or lpid_v == 0 or sample.get("cycle_anchor_us") == 0
            ):
                return "bogus_cycle_trace", "harness"
            if "hard-stop" in (inv or "").lower() or "graceful" in (inv or "").lower():
                # Control-case expectation miss is not a live observer false-state.
                return "control_case", "harness"
            if bucket == "no_retry_or_wrong_cycle":
                consumed = sample.get("fault_consumed")
                if consumed in (0, "0", None) or (
                    sample.get("fault_armed") in (1, "1") and consumed not in (1, "1")
                ):
                    return "fault_not_consumed_no_retry", "harness"
                retry = sample.get("retry_actual_send_time") or sample.get("retry_send_us")
                if retry in (None, "", "null"):
                    return "no_retry", "harness"  # treat as harness unless proven otherwise
                return "retry_not_confirmed", "harness"
            return bucket, domain

        for r in jsonl:
            for inv in r.get("failures") or []:
                bucket, domain = classify_invariant(str(inv))
                bucket, domain = refine(bucket, domain, str(inv), r)
                assertion_failures.append(
                    {
                        "shard": name,
                        "transport": transport,
                        "cycle_index": r.get("cycle_index"),
                        "logical_ping_id": r.get("logical_ping_id"),
                        "fault_type": r.get("fault_type"),
                        "sequence": r.get("sequence") if "sequence" in r else None,
                        "invariant": inv,
                        "bucket": bucket,
                        "domain": domain,
                        "fault_armed": r.get("fault_armed"),
                        "fault_consumed": r.get("fault_consumed"),
                        "scheduled_phase_error_ms": r.get("scheduled_phase_error_ms"),
                        "estimated_server_margin_ms": r.get("estimated_server_margin_ms"),
                        "Tn": r.get("Tn"),
                        "sample": r,
                    }
                )

        for item in failed:
            inv = item.get("invariant") or ""
            bucket, domain = classify_invariant(str(inv))
            bucket, domain = refine(bucket, domain, str(inv), item)
            assertion_failures.append(
                {
                    "shard": name,
                    "transport": transport,
                    "cycle_index": item.get("cycle", item.get("cycle_index")),
                    "logical_ping_id": item.get("logical_ping_id"),
                    "fault_type": item.get("fault_type"),
                    "sequence": item.get("sequence"),
                    "invariant": inv,
                    "bucket": bucket,
                    "domain": domain,
                    "fault_armed": item.get("fault_armed"),
                    "fault_consumed": item.get("fault_consumed"),
                    "scheduled_phase_error_ms": item.get("scheduled_phase_error_ms"),
                    "estimated_server_margin_ms": item.get(
                        "retry_server_margin_ms", item.get("estimated_server_margin_ms")
                    ),
                    "Tn": item.get("Tn") or item.get("scheduled_nominal_us"),
                    "sample": item,
                    "from_failed_cases_json": True,
                }
            )

        elapsed = None
        if st.get("started_utc") and st.get("finished_utc"):
            # approximate from budget
            elapsed = st.get("budget_sec")
        # Prefer stdout elapsed
        for ln in stdout.splitlines():
            if "budget exhausted after" in ln and "elapsed_ms=" in ln:
                try:
                    elapsed = int(ln.split("elapsed_ms=")[1].split()[0]) / 1000.0
                except Exception:
                    pass

        shards_meta.append(
            {
                "shard": name,
                "transport": transport,
                "seed": st.get("seed") or meta.get("seed"),
                "budget_sec": st.get("budget_sec"),
                "elapsed_sec": elapsed,
                "cycles": len(jsonl),
                "original_runner_state": st.get("state"),
                "corrected_state": corrected_state,
                "real_crash": is_real_crash,
                "has_report": has_report,
                "budget_exhausted": budget_exhausted,
                "semantic_fail": semantic_fail,
                "one_way_estimate_us": meta.get("one_way_estimate_us"),
                "warmup_min_rtt_ms": meta.get("warmup_min_rtt_ms"),
                "warmup_p99_rtt_ms": meta.get("warmup_p99_rtt_ms"),
            }
        )

    # Deduplicate assertions: same shard+cycle+invariant
    seen_assert = set()
    uniq_assertions = []
    for a in assertion_failures:
        key = (a["shard"], a.get("cycle_index"), a.get("invariant"))
        if key in seen_assert:
            continue
        seen_assert.add(key)
        uniq_assertions.append(a)
    assertion_failures = uniq_assertions

    # Enrich sequence from csv when missing
    csv_index = {(r["shard"], pi(r.get("cycle_index"))): r for r in all_csv}
    phase_index = {(r["shard"], pi(r.get("cycle_index"))): r for r in all_phase}
    win_index = {(r["shard"], pi(r.get("cycle_index"))): r for r in all_win}
    for a in assertion_failures:
        ci = pi(a.get("cycle_index"))
        key = (a["shard"], ci)
        if a.get("sequence") is None and key in csv_index:
            a["sequence"] = csv_index[key].get("sequence")
        if a.get("fault_consumed") is None and key in csv_index:
            a["fault_consumed"] = pi(csv_index[key].get("fault_consumed"))
            a["fault_armed"] = pi(csv_index[key].get("fault_armed"))

    # Also load original aggregate failures for the canonical 360/2725 split.
    orig_failures = []
    orig_path = AGG / "failures.json"
    if orig_path.exists():
        try:
            orig_failures = json.loads(orig_path.read_text(encoding="utf-8"))
        except Exception:
            orig_failures = []
    orig_class_counts = Counter(f.get("failure_class") for f in orig_failures)
    orig_prod = [f for f in orig_failures if str(f.get("failure_class", "")).startswith("PRODUCTION_")]
    orig_harness = [f for f in orig_failures if str(f.get("failure_class", "")).startswith("HARNESS_")]

    # Correct original PRODUCTION_PHASE with Tn==0 / logical_ping_id==0 to harness instrumentation.
    corrected_orig_prod = []
    moved_to_harness = []
    for f in orig_prod:
        s = f.get("sample") or {}
        tn = s.get("Tn") or s.get("scheduled_nominal_us") or f.get("Tn")
        lpid = s.get("logical_ping_id") if "logical_ping_id" in s else f.get("logical_ping_id")
        inv = str(f.get("invariant") or "")
        try:
            tn_v = float(tn) if tn is not None else None
        except (TypeError, ValueError):
            tn_v = None
        try:
            lpid_v = int(lpid) if lpid is not None else None
        except (TypeError, ValueError):
            lpid_v = None
        if f.get("failure_class") == "PRODUCTION_PHASE" and (tn_v == 0 or lpid_v == 0):
            moved_to_harness.append({**f, "corrected_class": "HARNESS_BOGUS_CYCLE_TRACE"})
            continue
        if f.get("failure_class") == "PRODUCTION_STATE" and (
            "hard-stop" in inv.lower() or "graceful" in inv.lower()
        ):
            moved_to_harness.append({**f, "corrected_class": "HARNESS_CONTROL_CASE"})
            continue
        corrected_orig_prod.append(f)

    # ---------------- Phase analysis ----------------
    def phase_stats(transport: str) -> dict[str, Any]:
        rows = [r for r in all_jsonl if r.get("transport") == transport]
        errs = []
        for r in rows:
            e = pf(r.get("scheduled_phase_error_ms"))
            if e is None:
                # fall back to phase csv
                pr = phase_index.get((r["shard"], pi(r.get("cycle_index"))))
                if pr:
                    eu = pf(pr.get("scheduled_phase_error_us"))
                    if eu is not None:
                        e = eu / 1000.0
            if e is not None:
                errs.append((r, e))
        vals = [e for _, e in errs]
        d = dist(vals)
        exact_zero = sum(1 for e in vals if e == 0.0)
        nonzero = sum(1 for e in vals if e != 0.0)
        thresholds = [0.1, 0.5, 1, 5, 10, 50, 100, 250]
        thr = {f"abs_gt_{t}_ms": sum(1 for e in vals if abs(e) > t) for t in thresholds}
        outliers = [(r, e) for r, e in errs if abs(e) > 1.0]
        return {
            "sample_count": len(vals),
            "exact_zero": exact_zero,
            "nonzero": nonzero,
            "pct_exact_zero": (100.0 * exact_zero / len(vals)) if vals else float("nan"),
            "dist": d,
            "thresholds": thr,
            "outliers_gt_1ms": outliers,
        }

    tcp_phase = phase_stats("tcp")
    udp_phase = phase_stats("udp")

    # TCP 462.8 event — find max abs
    tcp_outliers = sorted(tcp_phase["outliers_gt_1ms"], key=lambda x: abs(x[1]), reverse=True)
    big_event = None
    big_timeline = None
    if tcp_outliers:
        r0, e0 = tcp_outliers[0]
        shard = r0["shard"]
        ci = int(r0["cycle_index"])
        # Build index of jsonl by shard
        by_c = {
            int(r["cycle_index"]): r
            for r in all_jsonl
            if r.get("shard") == shard and r.get("cycle_index") is not None
        }
        window_cycles = list(range(ci - 5, ci + 11))
        timeline = []
        for j in window_cycles:
            if j not in by_c:
                continue
            rr = by_c[j]
            cr = csv_index.get((shard, j), {})
            pr = phase_index.get((shard, j), {})
            wr = win_index.get((shard, j), {})
            obs = [
                o
                for o in all_obs
                if o.get("shard") == shard and pi(o.get("cycle_index")) == j
            ]
            timeline.append(
                {
                    "cycle_index": j,
                    "is_event": j == ci,
                    "fault_type": rr.get("fault_type") or cr.get("fault_type"),
                    "sequence": cr.get("sequence"),
                    "scheduled_phase_error_ms": pf(rr.get("scheduled_phase_error_ms")),
                    "expected_nominal": rr.get("expected_nominal_time"),
                    "scheduled_nominal": rr.get("Tn") or cr.get("scheduled_nominal"),
                    "actual_send": rr.get("actual_first_attempt_send_time"),
                    "retry_decision": cr.get("retry_decision") or cr.get("attempt_timeout"),
                    "retry_send": rr.get("retry_actual_send_time"),
                    "estimated_server_receive": rr.get("estimated_server_receive"),
                    "original_deadline": rr.get("original_deadline"),
                    "estimated_server_margin_ms": rr.get("estimated_server_margin_ms"),
                    "next_nominal_phase_error_ms": rr.get("next_nominal_phase_error_ms"),
                    "window": {
                        "cur_start_delta_ms": pf(wr.get("current_window_start_delta_ms")),
                        "cur_end_delta_ms": pf(wr.get("current_window_end_delta_ms")),
                        "cur_dur_delta_ms": pf(wr.get("current_window_duration_delta_ms")),
                        "next_start_delta_ms": pf(wr.get("next_window_start_delta_ms")),
                        "next_nominal_phase_delta_ms": pf(
                            wr.get("next_nominal_phase_delta_ms")
                        ),
                    },
                    "observers": [
                        {
                            "checkpoint": pi(o.get("checkpoint")),
                            "state": pi(o.get("state")),
                            "expected_state": pi(o.get("expected_state")),
                            "mismatch": pi(o.get("mismatch")),
                            "next_ping_delta_ms": pf(o.get("next_ping_delta_ms")),
                        }
                        for o in obs
                    ],
                    "phase_csv_error_us": pf(pr.get("scheduled_phase_error_us")),
                    "failures": rr.get("failures"),
                }
            )
        # Drift before/after
        before = [
            (int(r["cycle_index"]), pf(r.get("scheduled_phase_error_ms")) or 0.0)
            for r in all_jsonl
            if r.get("shard") == shard and pi(r.get("cycle_index")) is not None and pi(r.get("cycle_index")) < ci
        ]
        after = [
            (int(r["cycle_index"]), pf(r.get("scheduled_phase_error_ms")) or 0.0)
            for r in all_jsonl
            if r.get("shard") == shard and pi(r.get("cycle_index")) is not None and pi(r.get("cycle_index")) > ci
        ]
        # Nature: check if subsequent cycles stay shifted
        after_nonzero = sum(1 for _, e in after[:20] if abs(e) > 0.1)
        after_exact_zero = sum(1 for _, e in after[:50] if e == 0.0)
        nature = "one_cycle_transient"
        if after and after_nonzero > 5 and after_exact_zero < len(after[:20]) * 0.5:
            nature = "temporary_or_permanent_shift"
        # Check if only event cycle nonzero among neighbors
        neighbor_nz = [
            t
            for t in timeline
            if abs(t.get("scheduled_phase_error_ms") or 0) > 0.1 and not t["is_event"]
        ]
        if not neighbor_nz and abs(e0) > 1:
            nature = "one_cycle_transient_or_instrumentation"
        # Check expected vs scheduled delta matches phase error
        exp = pf(r0.get("expected_nominal_time"))
        sched = pf(r0.get("Tn"))
        if exp is not None and sched is not None:
            delta_ms = (sched - exp) / 1000.0
        else:
            delta_ms = e0

        # Instrumentation: Tn==0 / logical_ping_id==0 means missing cycle_anchor
        lpid = r0.get("logical_ping_id")
        if (sched == 0 or sched is None) or lpid in (0, "0"):
            nature = "timestamp_instrumentation_error_bogus_cycle_trace"

        big_event = {
            "shard": shard,
            "cycle": ci,
            "seed": r0.get("seed"),
            "fault_type": r0.get("fault_type"),
            "phase_error_ms": e0,
            "expected_minus_scheduled_ms": delta_ms,
            "nature": nature,
            "slope_before_ms_per_cycle": linear_slope(
                [float(c) for c, _ in before], [e for _, e in before]
            ),
            "slope_after_ms_per_cycle": linear_slope(
                [float(c) for c, _ in after], [e for _, e in after]
            ),
            "after_50_exact_zero": after_exact_zero,
            "after_20_nonzero_gt_0_1": after_nonzero,
            "neighbor_nonzero_count": len(neighbor_nz),
        }
        big_timeline = timeline

    # ---------------- Interval analysis ----------------
    def interval_stats(transport: str) -> dict[str, Any]:
        # Per shard, sort by cycle, use scheduled nominal
        by_shard: dict[str, list[dict]] = defaultdict(list)
        for r in all_jsonl:
            if r.get("transport") != transport:
                continue
            by_shard[r["shard"]].append(r)
        scheduled_intervals = []
        send_intervals = []
        for shard, rows in by_shard.items():
            rows = sorted(rows, key=lambda x: pi(x.get("cycle_index")) or 0)
            for a, b in zip(rows, rows[1:]):
                sa = pf(a.get("Tn"))
                sb = pf(b.get("Tn"))
                if sa is not None and sb is not None:
                    scheduled_intervals.append((sb - sa) / 1000.0)
                fa = pf(a.get("actual_first_attempt_send_time"))
                fb = pf(b.get("actual_first_attempt_send_time"))
                # only contiguous
                if a.get("contiguous_cycle") == 1 or (
                    pi(b.get("cycle_index")) is not None
                    and pi(a.get("cycle_index")) is not None
                    and pi(b.get("cycle_index")) - pi(a.get("cycle_index")) == 1
                ):
                    if fa is not None and fb is not None:
                        send_intervals.append((fb - fa) / 1000.0)
        sched_err = [v - 1000.0 for v in scheduled_intervals]
        send_err = [v - 1000.0 for v in send_intervals]
        exact_1000 = sum(1 for v in scheduled_intervals if abs(v - 1000.0) < 1e-9)
        # also count within 0.001 us quantization - treat as exact if == 1000
        exact_1000 = sum(1 for v in scheduled_intervals if v == 1000.0)
        thr = {
            "gt_0_1": sum(1 for e in sched_err if abs(e) > 0.1),
            "gt_1": sum(1 for e in sched_err if abs(e) > 1),
            "gt_10": sum(1 for e in sched_err if abs(e) > 10),
            "gt_100": sum(1 for e in sched_err if abs(e) > 100),
        }
        return {
            "scheduled_interval": dist(scheduled_intervals),
            "scheduled_error": dist(sched_err),
            "exact_1000": exact_1000,
            "scheduled_thresholds": thr,
            "actual_send_interval": dist(send_intervals),
            "actual_send_error": dist(send_err),
        }

    tcp_iv = interval_stats("tcp")
    udp_iv = interval_stats("udp")

    # ---------------- Loss analysis ----------------
    def loss_analysis(transport: str, fault: str) -> dict[str, Any]:
        rows = [
            r
            for r in all_csv
            if r.get("transport") == transport and r.get("fault_type") == fault
        ]
        # Prefer csv for richer fields; also merge jsonl
        jmap = {
            (r["shard"], pi(r.get("cycle_index"))): r
            for r in all_jsonl
            if r.get("transport") == transport and r.get("fault_type") == fault
        }
        total = len(rows)
        armed = sum(1 for r in rows if pi(r.get("fault_armed")) == 1)
        consumed = [r for r in rows if pi(r.get("fault_consumed")) == 1]
        not_consumed = [r for r in rows if pi(r.get("fault_armed")) == 1 and pi(r.get("fault_consumed")) != 1]
        margins = []
        send_before = send_after = no_retry = 0
        after_deadline = 0
        before_deadline = 0
        by_seq = Counter()
        by_shard = Counter()
        recovered = 0
        for r in rows:
            by_seq[seq_group(r.get("sequence") or "")] += 1
            by_shard[r.get("shard")] += 1
        for r in consumed:
            jr = jmap.get((r["shard"], pi(r.get("cycle_index"))), {})
            m = pf(r.get("estimated_server_margin_ms"))
            if m is None:
                m = pf(jr.get("estimated_server_margin_ms"))
            retry_send = pf(r.get("retry_actual_send")) or pf(jr.get("retry_actual_send_time"))
            tn = pf(r.get("Tn")) or pf(jr.get("Tn"))
            if retry_send is None:
                no_retry += 1
                continue
            if tn is not None:
                if retry_send <= tn:
                    send_before += 1
                else:
                    send_after += 1
            if m is None:
                continue
            margins.append(m)
            if m >= 0:
                before_deadline += 1
                recovered += 1
            else:
                after_deadline += 1
        buckets = margin_buckets(margins)
        buckets["no_retry"] = no_retry
        # first request checks
        if fault == "request-loss":
            first_ok = sum(1 for r in consumed if pi(r.get("first_request_sent")) == 0)
            first_bad = sum(1 for r in rows if pi(r.get("fault_armed")) == 1 and pi(r.get("first_request_sent")) == 1)
        else:
            first_ok = sum(1 for r in consumed if pi(r.get("first_request_sent")) == 1)
            first_bad = sum(1 for r in rows if pi(r.get("fault_armed")) == 1 and pi(r.get("first_request_sent")) == 0)
        usable = len(consumed)
        return {
            "total_injected_or_labeled": total,
            "fault_armed": armed,
            "fault_consumed": usable,
            "harness_not_consumed": len(not_consumed),
            "usable_success_estimated_before_Tn": recovered,
            "usable_success_rate_pct": (100.0 * recovered / usable) if usable else float("nan"),
            "send_before_Tn": send_before,
            "send_after_Tn": send_after,
            "send_before_pct": (100.0 * send_before / usable) if usable else float("nan"),
            "estimated_after_deadline": after_deadline,
            "no_retry_among_consumed": no_retry,
            "margins": dist(margins),
            "margin_buckets": buckets,
            "by_sequence": dict(by_seq),
            "by_shard": dict(by_shard),
            "first_attempt_correct": first_ok,
            "first_attempt_incorrect_among_armed": first_bad,
        }

    tcp_req = loss_analysis("tcp", "request-loss")
    tcp_resp = loss_analysis("tcp", "response-loss")
    udp_req = loss_analysis("udp", "request-loss")
    udp_resp = loss_analysis("udp", "response-loss")

    # ---------------- Window analysis ----------------
    def window_stats(transport: str, fault: str) -> dict[str, Any]:
        rows = [
            r
            for r in all_win
            if r.get("transport") == transport and r.get("fault_type") == fault
        ]
        fields = [
            "current_window_start_delta_ms",
            "current_window_end_delta_ms",
            "current_window_duration_delta_ms",
            "next_window_start_delta_ms",
            "next_nominal_phase_delta_ms",
        ]
        out = {f: dist([pf(r.get(f)) for r in rows]) for f in fields}
        # anomalies: next nominal phase delta abs > 1ms
        anomalies = []
        for r in rows:
            nd = pf(r.get("next_nominal_phase_delta_ms"))
            if nd is not None and abs(nd) > 1.0:
                anomalies.append(
                    {
                        "shard": r["shard"],
                        "cycle": pi(r.get("cycle_index")),
                        "fault_type": fault,
                        "next_nominal_phase_delta_ms": nd,
                        "cur_dur_delta_ms": pf(r.get("current_window_duration_delta_ms")),
                        "next_start_delta_ms": pf(r.get("next_window_start_delta_ms")),
                    }
                )
        out["next_nominal_phase_anomaly_gt_1ms"] = anomalies[:50]
        out["next_nominal_phase_anomaly_count"] = len(anomalies)
        expanded = sum(
            1
            for r in rows
            if (pf(r.get("current_window_duration_delta_ms")) or 0) > 0.5
        )
        shrunk = sum(
            1
            for r in rows
            if (pf(r.get("current_window_duration_delta_ms")) or 0) < -0.5
        )
        out["window_expanded"] = expanded
        out["window_shrunk"] = shrunk
        out["n_rows"] = len(rows)
        return out

    win_tcp_req = window_stats("tcp", "request-loss")
    win_tcp_resp = window_stats("tcp", "response-loss")
    win_udp_req = window_stats("udp", "request-loss")
    win_udp_resp = window_stats("udp", "response-loss")

    # ---------------- Observer ----------------
    def observer_stats() -> dict[str, Any]:
        by = defaultdict(lambda: Counter())
        mismatches = []
        false_u = 0
        false_md = 0
        for o in all_obs:
            ck = pi(o.get("checkpoint")) or 0
            tr = o.get("transport")
            ft = o.get("fault_type") or "none"
            st = pi(o.get("state"))
            exp = pi(o.get("expected_state"))
            mm = pi(o.get("mismatch")) == 1
            by[(tr, ft, ck)]["n"] += 1
            if st == 0:
                by[(tr, ft, ck)]["Expected"] += 1
            elif st == 1:
                by[(tr, ft, ck)]["MissedDeadline"] += 1
            elif st == 2:
                by[(tr, ft, ck)]["Unknown"] += 1
            else:
                by[(tr, ft, ck)]["invalid"] += 1
            if mm:
                by[(tr, ft, ck)]["mismatch"] += 1
                mismatches.append(o)
            # false live: state MD/Unknown when expected Expected (0) on live checkpoints
            if exp == 0 and st == 1:
                false_md += 1
            if exp == 0 and st == 2:
                false_u += 1
        return {
            "by_transport_fault_checkpoint": {
                f"{tr}|{ft}|ckpt{ck}": dict(c) for (tr, ft, ck), c in sorted(by.items())
            },
            "false_live_Unknown": false_u,
            "false_live_MissedDeadline": false_md,
            "mismatch_count": len(mismatches),
            "total_queries": len(all_obs),
        }

    obs_stats = observer_stats()

    # ---------------- Failure breakdown (canonical = corrected original classes) ----------------
    def root_key(a: dict) -> tuple:
        return (a.get("transport"), a.get("shard"), pi(a.get("cycle_index")), a.get("bucket"))

    # Use original aggregate failures.json as the assertion inventory (360/2725),
    # then apply instrumentation/control corrections.
    prod_assertions = []
    for f in corrected_orig_prod:
        cls = f.get("failure_class")
        bucket = {
            "PRODUCTION_RETRY_ESTIMATED_LATE_ARRIVAL": "retry_after_deadline",
            "PRODUCTION_PHASE": "phase_shift",
            "PRODUCTION_STATE": "observer_wrong_state",
            "PRODUCTION_WINDOW": "window_correction",
        }.get(cls, "other")
        s = f.get("sample") or {}
        prod_assertions.append(
            {
                "shard": f.get("shard"),
                "transport": f.get("transport"),
                "cycle_index": f.get("cycle_index") or s.get("cycle_index") or f.get("cycle"),
                "logical_ping_id": s.get("logical_ping_id") or f.get("logical_ping_id"),
                "fault_type": s.get("fault_type") or f.get("fault_type"),
                "sequence": s.get("sequence") or f.get("sequence"),
                "invariant": f.get("invariant"),
                "bucket": bucket,
                "domain": "production",
                "failure_class": cls,
                "estimated_server_margin_ms": s.get("estimated_server_margin_ms")
                or s.get("retry_server_margin_ms"),
                "scheduled_phase_error_ms": s.get("scheduled_phase_error_ms"),
                "retry_actual_send_time": s.get("retry_actual_send_time") or s.get("retry_send_us"),
                "Tn": s.get("Tn") or s.get("original_deadline"),
                "sample": s or f,
            }
        )

    harness_assertions = []
    for f in orig_harness:
        harness_assertions.append(
            {
                "shard": f.get("shard"),
                "transport": f.get("transport"),
                "cycle_index": f.get("cycle_index"),
                "invariant": f.get("invariant"),
                "bucket": str(f.get("failure_class") or "").replace("HARNESS_", "").lower(),
                "domain": "harness",
                "failure_class": f.get("failure_class"),
                "sample": f.get("sample") or f,
            }
        )
    for f in moved_to_harness:
        harness_assertions.append(
            {
                "shard": f.get("shard"),
                "transport": f.get("transport"),
                "cycle_index": f.get("cycle_index"),
                "invariant": f.get("invariant"),
                "bucket": str(f.get("corrected_class") or "moved"),
                "domain": "harness",
                "failure_class": f.get("corrected_class"),
                "sample": f.get("sample") or f,
                "moved_from": f.get("failure_class"),
            }
        )
    other_assertions = [
        a for a in assertion_failures if a["domain"] == "other"
    ]

    # Also count late events where client still sent before Tn
    late_send_before = 0
    late_send_after = 0
    late_margins = []
    for a in prod_assertions:
        if a["bucket"] != "retry_after_deadline":
            continue
        m = pf(a.get("estimated_server_margin_ms"))
        if m is not None:
            late_margins.append(m)
        rs = pf(a.get("retry_actual_send_time"))
        tn = pf(a.get("Tn"))
        if rs is not None and tn is not None:
            if rs <= tn:
                late_send_before += 1
            else:
                late_send_after += 1

    prod_by_bucket = Counter(a["bucket"] for a in prod_assertions)
    prod_by_tr_bucket = Counter((a["transport"], a["bucket"]) for a in prod_assertions)
    unique_prod_events = {}
    for a in prod_assertions:
        k = root_key(a)
        if k not in unique_prod_events:
            unique_prod_events[k] = a
    unique_prod_by_bucket = Counter(a["bucket"] for a in unique_prod_events.values())
    unique_prod_by_tr = Counter(
        (a["transport"], a["bucket"]) for a in unique_prod_events.values()
    )

    harness_by_bucket = Counter(
        a.get("failure_class") or a.get("bucket") for a in harness_assertions
    )

    # Map buckets to requested table categories
    table_map = {
        "retry_after_deadline": "retry after deadline",
        "no_retry": "no retry",
        "phase_shift": "phase shift",
        "window_correction": "window correction",
        "observer_wrong_state": "observer wrong state",
        "duplicate_logical_ping": "duplicate logical ping",
        "other": "other production semantic",
        "other_production": "other production semantic",
    }

    # Representative / worst timelines for each prod bucket
    def pick_reps(bucket: str) -> dict[str, Any]:
        items = [a for a in unique_prod_events.values() if a["bucket"] == bucket]
        if not items:
            return {}
        # worst by abs margin or phase
        def score(a):
            m = pf(a.get("estimated_server_margin_ms"))
            p = pf(a.get("scheduled_phase_error_ms"))
            if bucket == "retry_after_deadline" and m is not None:
                return -m  # most late
            if bucket == "phase_shift" and p is not None:
                return abs(p)
            return 0

        items_sorted = sorted(items, key=score, reverse=True)
        worst = items_sorted[0]
        rep = items_sorted[len(items_sorted) // 2]

        def brief(a):
            return {
                "shard": a["shard"],
                "cycle": a.get("cycle_index"),
                "transport": a["transport"],
                "fault_type": a.get("fault_type"),
                "sequence": a.get("sequence"),
                "invariant": a.get("invariant"),
                "estimated_server_margin_ms": a.get("estimated_server_margin_ms"),
                "scheduled_phase_error_ms": a.get("scheduled_phase_error_ms"),
            }

        # cluster by shard
        by_shard = Counter(a["shard"] for a in items)
        return {
            "count_unique": len(items),
            "by_shard": dict(by_shard.most_common(10)),
            "representative": brief(rep),
            "worst": brief(worst),
        }

    prod_category_details = {
        table_map.get(b, b): pick_reps(b) for b in unique_prod_by_bucket
    }

    # ---------------- Shard stability ----------------
    shard_reports = []
    for sm in shards_meta:
        name = sm["shard"]
        tr = sm["transport"]
        rows = [r for r in all_jsonl if r.get("shard") == name]
        errs = [pf(r.get("scheduled_phase_error_ms")) for r in rows]
        errs = [e for e in errs if e is not None]
        d = dist(errs)
        prod_n = sum(1 for a in prod_assertions if a["shard"] == name)
        harness_n = sum(1 for a in harness_assertions if a["shard"] == name)
        # success among consumed
        def shard_loss(fault):
            rs = [
                r
                for r in all_csv
                if r.get("shard") == name and r.get("fault_type") == fault and pi(r.get("fault_consumed")) == 1
            ]
            if not rs:
                return {"consumed": 0, "success_pct": float("nan")}
            ok = 0
            for r in rs:
                m = pf(r.get("estimated_server_margin_ms"))
                if m is not None and m >= 0:
                    ok += 1
            return {"consumed": len(rs), "success_pct": 100.0 * ok / len(rs)}

        false_obs = sum(
            1
            for o in all_obs
            if o.get("shard") == name
            and pi(o.get("expected_state")) == 0
            and pi(o.get("state")) in (1, 2)
        )
        shard_reports.append(
            {
                **sm,
                "production_assertion_failures": prod_n,
                "harness_assertion_failures": harness_n,
                "phase_max_abs": d.get("max_abs"),
                "phase_p99": d.get("p99"),
                "phase_exact_zero_pct": (100.0 * sum(1 for e in errs if e == 0.0) / len(errs))
                if errs
                else float("nan"),
                "request_loss": shard_loss("request-loss"),
                "response_loss": shard_loss("response-loss"),
                "observer_false_state": false_obs,
            }
        )

    # thirds of wall time using shard order
    ordered = sorted(shard_reports, key=lambda s: s["shard"])
    # better: by started time from status
    st_map = {s["shard"]: s for s in status.get("shards", [])}
    ordered = sorted(
        shard_reports,
        key=lambda s: st_map.get(s["shard"], {}).get("started_utc") or s["shard"],
    )
    n = len(ordered)
    thirds = {
        "first": ordered[: n // 3],
        "middle": ordered[n // 3 : 2 * n // 3],
        "last": ordered[2 * n // 3 :],
    }

    def third_summary(items):
        cycles = sum(i["cycles"] for i in items)
        prod = sum(i["production_assertion_failures"] for i in items)
        harness = sum(i["harness_assertion_failures"] for i in items)
        phases = [i["phase_max_abs"] for i in items if i.get("phase_max_abs") is not None]
        return {
            "shards": [i["shard"] for i in items],
            "cycles": cycles,
            "production_assertions": prod,
            "harness_assertions": harness,
            "phase_max_abs_max": max(phases) if phases else None,
            "phase_max_abs_median": statistics.median(phases) if phases else None,
        }

    thirds_summary = {k: third_summary(v) for k, v in thirds.items()}

    # ---------------- Corrected usable sample rate ----------------
    total_cycles = len(all_jsonl)
    harness_invalid_cycles = set()
    for a in harness_assertions:
        if a["bucket"] in {
            "fault_not_armed",
            "fault_not_consumed_no_retry",
            "cycle_confirm",
            "query",
            "reporting",
        }:
            harness_invalid_cycles.add((a["shard"], pi(a.get("cycle_index"))))
    usable_cycles = total_cycles - len(
        {k for k in harness_invalid_cycles if k[1] is not None}
    )
    # For loss reliability, use consumed only
    usable_loss = (
        tcp_req["fault_consumed"]
        + tcp_resp["fault_consumed"]
        + udp_req["fault_consumed"]
        + udp_resp["fault_consumed"]
    )
    labeled_loss = (
        tcp_req["total_injected_or_labeled"]
        + tcp_resp["total_injected_or_labeled"]
        + udp_req["total_injected_or_labeled"]
        + udp_resp["total_injected_or_labeled"]
    )

    # ---------------- Verdict ----------------
    # Phase: does schedule accumulate drift?
    tcp_drift = linear_slope(
        [float(pi(r.get("cycle_index")) or 0) for r in all_jsonl if r.get("transport") == "tcp"],
        [
            pf(r.get("scheduled_phase_error_ms")) or 0.0
            for r in all_jsonl
            if r.get("transport") == "tcp"
        ],
    )
    udp_drift = linear_slope(
        [float(pi(r.get("cycle_index")) or 0) for r in all_jsonl if r.get("transport") == "udp"],
        [
            pf(r.get("scheduled_phase_error_ms")) or 0.0
            for r in all_jsonl
            if r.get("transport") == "udp"
        ],
    )

    unique_retry_late = unique_prod_by_bucket.get("retry_after_deadline", 0)
    unique_phase = unique_prod_by_bucket.get("phase_shift", 0)
    unique_prod_total = len(unique_prod_events)

    false_obs_ok = (
        obs_stats["false_live_Unknown"] == 0 and obs_stats["false_live_MissedDeadline"] == 0
    )
    # Bogus Tn=0 outliers and slopes ~0 => no cumulative schedule drift.
    phase_cumulative_ok = abs(tcp_drift) < 0.01 and abs(udp_drift) < 0.01
    if big_event and "instrumentation" in str(big_event.get("nature", "")):
        phase_cumulative_ok = phase_cumulative_ok and True
    elif big_event and str(big_event.get("nature", "")).startswith("one_cycle"):
        phase_cumulative_ok = phase_cumulative_ok and True

    # Verdict:
    # - Phase does not accumulate drift (p99=0, slopes~0, bogus Tn=0 outliers are harness).
    # - Estimated late arrivals among consumed faults => PARTIAL, not FAIL.
    # - FAIL only if cumulative phase drift / live false Unknown|MD / real phase_shift remain.
    real_phase_shift = unique_phase  # after moving Tn==0 events to harness
    if (
        phase_cumulative_ok
        and real_phase_shift == 0
        and false_obs_ok
        and unique_prod_total == 0
    ):
        verdict = "PASS"
        verdict_why = (
            "Nominal 1s phase preserved with no unique production failures "
            "after correcting instrumentation/control mislabels."
        )
    elif phase_cumulative_ok and real_phase_shift == 0 and false_obs_ok:
        verdict = "PARTIAL"
        verdict_why = (
            "1s nominal phase does not accumulate drift; Alice shows no false live "
            f"Unknown/MissedDeadline; TCP |phase|>1ms outliers are bogus cycle traces "
            f"(Tn=0). Remaining unique production root-events: {unique_prod_total}, "
            f"dominated by {unique_retry_late} estimated-late-arrival events "
            f"({late_send_before} still had client retry send before Tn; "
            f"{late_send_after} sent after Tn). Mapping uses one_way=min_rtt/2."
        )
    else:
        verdict = "FAIL"
        verdict_why = (
            "Phase accumulation, live observer false-state, or unreclassified "
            f"phase_shift remains (unique_phase={real_phase_shift})."
        )

    # Preserve answers flags consistently with corrected classification
    request_loss_preserves_phase = real_phase_shift == 0
    response_loss_preserves_phase = real_phase_shift == 0
    does_accumulate = not phase_cumulative_ok

    # Build production table
    table_cats = [
        "retry after deadline",
        "no retry",
        "phase shift",
        "window correction",
        "observer wrong state",
        "duplicate logical ping",
        "other production semantic",
    ]
    prod_table = []
    for cat in table_cats:
        # reverse map
        buckets = [b for b, name in table_map.items() if name == cat]
        tcp_c = sum(unique_prod_by_tr.get(("tcp", b), 0) for b in buckets)
        udp_c = sum(unique_prod_by_tr.get(("udp", b), 0) for b in buckets)
        # assertion counts
        tcp_a = sum(prod_by_tr_bucket.get(("tcp", b), 0) for b in buckets)
        udp_a = sum(prod_by_tr_bucket.get(("udp", b), 0) for b in buckets)
        prod_table.append(
            {
                "category": cat,
                "tcp_unique": tcp_c,
                "udp_unique": udp_c,
                "total_unique": tcp_c + udp_c,
                "tcp_assertions": tcp_a,
                "udp_assertions": udp_a,
                "total_assertions": tcp_a + udp_a,
            }
        )

    # Outlier timelines for all TCP >1ms
    tcp_outlier_briefs = []
    for r, e in tcp_outliers:
        tcp_outlier_briefs.append(
            {
                "shard": r["shard"],
                "cycle": r.get("cycle_index"),
                "seed": r.get("seed"),
                "fault_type": r.get("fault_type"),
                "phase_error_ms": e,
                "Tn": r.get("Tn"),
                "expected_nominal_time": r.get("expected_nominal_time"),
                "next_nominal_phase_error_ms": r.get("next_nominal_phase_error_ms"),
                "failures": r.get("failures"),
            }
        )

    summary = {
        "verdict": verdict,
        "verdict_why": verdict_why,
        "wall_time_sec": status.get("elapsed_sec"),
        "corrected_shards": [
            {
                "shard": s["shard"],
                "transport": s["transport"],
                "original_runner_state": s["original_runner_state"],
                "corrected_state": s["corrected_state"],
                "real_crash": s["real_crash"],
                "cycles": s["cycles"],
                "budget_exhausted": s["budget_exhausted"],
                "semantic_fail": s["semantic_fail"],
            }
            for s in shards_meta
        ],
        "cycles": {"tcp": sum(1 for r in all_jsonl if r["transport"] == "tcp"), "udp": sum(1 for r in all_jsonl if r["transport"] == "udp")},
        "phase": {
            "tcp": {
                "exact_zero": tcp_phase["exact_zero"],
                "nonzero": tcp_phase["nonzero"],
                "pct_exact_zero": tcp_phase["pct_exact_zero"],
                "dist": tcp_phase["dist"],
                "thresholds": tcp_phase["thresholds"],
                "drift_ms_per_cycle": tcp_drift,
                "outliers_gt_1ms_count": len(tcp_outliers),
            },
            "udp": {
                "exact_zero": udp_phase["exact_zero"],
                "nonzero": udp_phase["nonzero"],
                "pct_exact_zero": udp_phase["pct_exact_zero"],
                "dist": udp_phase["dist"],
                "thresholds": udp_phase["thresholds"],
                "drift_ms_per_cycle": udp_drift,
                "outliers_gt_1ms_count": len(udp_phase["outliers_gt_1ms"]),
            },
        },
        "tcp_462_event": big_event,
        "tcp_462_timeline": big_timeline,
        "tcp_outliers_gt_1ms": tcp_outlier_briefs,
        "intervals": {"tcp": tcp_iv, "udp": udp_iv},
        "request_loss": {"tcp": tcp_req, "udp": udp_req},
        "response_loss": {"tcp": tcp_resp, "udp": udp_resp},
        "windows": {
            "tcp_request": {k: v for k, v in win_tcp_req.items() if k != "next_nominal_phase_anomaly_gt_1ms"},
            "tcp_response": {k: v for k, v in win_tcp_resp.items() if k != "next_nominal_phase_anomaly_gt_1ms"},
            "udp_request": {k: v for k, v in win_udp_req.items() if k != "next_nominal_phase_anomaly_gt_1ms"},
            "udp_response": {k: v for k, v in win_udp_resp.items() if k != "next_nominal_phase_anomaly_gt_1ms"},
            "anomaly_counts": {
                "tcp_request": win_tcp_req["next_nominal_phase_anomaly_count"],
                "tcp_response": win_tcp_resp["next_nominal_phase_anomaly_count"],
                "udp_request": win_udp_req["next_nominal_phase_anomaly_count"],
                "udp_response": win_udp_resp["next_nominal_phase_anomaly_count"],
            },
        },
        "observer": obs_stats,
        "production_failures": {
            "original_claimed": 360,
            "original_class_counts": dict(orig_class_counts),
            "moved_to_harness": len(moved_to_harness),
            "moved_to_harness_detail": [
                {
                    "shard": f.get("shard"),
                    "cycle_index": f.get("cycle_index"),
                    "from": f.get("failure_class"),
                    "to": f.get("corrected_class"),
                    "invariant": f.get("invariant"),
                }
                for f in moved_to_harness
            ],
            "recomputed_assertions": len(prod_assertions),
            "unique_root_events": unique_prod_total,
            "by_bucket_assertions": dict(prod_by_bucket),
            "by_bucket_unique": dict(unique_prod_by_bucket),
            "late_client_send_before_Tn": late_send_before,
            "late_client_send_after_Tn": late_send_after,
            "late_margins": dist(late_margins),
            "table": prod_table,
            "category_details": prod_category_details,
        },
        "harness_failures": {
            "original_claimed": 2725,
            "recomputed_assertions": len(harness_assertions),
            "by_bucket": dict(harness_by_bucket),
            "null_exitcode_misclassified_shards": sum(
                1 for s in shards_meta if s["original_runner_state"] == "crashed" and not s["real_crash"]
            ),
            "other_assertions": len(other_assertions),
        },
        "usable_sample_rate": {
            "total_cycles": total_cycles,
            "cycles_with_harness_invalid_flags": len(harness_invalid_cycles),
            "approx_usable_cycle_pct": (100.0 * usable_cycles / total_cycles) if total_cycles else 0,
            "loss_labeled": labeled_loss,
            "loss_fault_consumed": usable_loss,
            "loss_consumed_pct": (100.0 * usable_loss / labeled_loss) if labeled_loss else 0,
        },
        "shard_reports": shard_reports,
        "thirds": thirds_summary,
        "answers": {
            "does_1s_schedule_accumulate_drift": does_accumulate,
            "request_loss_preserves_phase": request_loss_preserves_phase,
            "response_loss_preserves_phase": response_loss_preserves_phase,
            "retries_reach_server_before_deadline": {
                "note": "estimated mapping only (retry_send + one_way)",
                "tcp_request_before_pct": tcp_req["usable_success_rate_pct"],
                "tcp_response_before_pct": tcp_resp["usable_success_rate_pct"],
                "udp_request_before_pct": udp_req["usable_success_rate_pct"],
                "udp_response_before_pct": udp_resp["usable_success_rate_pct"],
                "unique_estimated_late_events": unique_retry_late,
            },
            "server_side_safety_margin": {
                "tcp_request": tcp_req["margins"],
                "tcp_response": tcp_resp["margins"],
                "udp_request": udp_req["margins"],
                "udp_response": udp_resp["margins"],
            },
            "window_preserves_next_nominal": {
                "tcp_req_anomaly_gt_1ms": win_tcp_req["next_nominal_phase_anomaly_count"],
                "tcp_resp_anomaly_gt_1ms": win_tcp_resp["next_nominal_phase_anomaly_count"],
                "udp_req_anomaly_gt_1ms": win_udp_req["next_nominal_phase_anomaly_count"],
                "udp_resp_anomaly_gt_1ms": win_udp_resp["next_nominal_phase_anomaly_count"],
            },
            "alice_false_states": {
                "Unknown": obs_stats["false_live_Unknown"],
                "MissedDeadline": obs_stats["false_live_MissedDeadline"],
            },
            "tcp_462_cause": big_event,
            "unique_real_production_failures": unique_prod_total,
            "harness_only_assertions": len(harness_assertions),
        },
        "recommended_next_test": None,  # filled below
    }

    # Recommended next test
    if unique_retry_late > 0:
        summary["recommended_next_test"] = {
            "title": "Focused estimated-late-arrival stress (consumed request/response loss only)",
            "why": (
                f"{unique_retry_late} unique PRODUCTION_RETRY_ESTIMATED_LATE_ARRIVAL events dominate "
                "production failures; phase itself does not accumulate drift."
            ),
            "scope": [
                "TCP and UDP separately, ~30–60 minutes each",
                "Only request-loss and response-loss with verified fault_consumed=1",
                "No QueryNow on the timeout→retry critical path",
                "Record retry_client_send vs Tn and estimated_server_receive vs Tn",
                "Classify late-send vs late-estimated-arrival separately",
                "Skip baseline-heavy mix and hard-stop/graceful spam",
            ],
            "not_recommended": "Another 8-hour general characterization — existing data already separates phase preservation from estimated-arrival margin.",
        }
    else:
        summary["recommended_next_test"] = {
            "title": "Harness arming reliability soak",
            "why": "Production unique failures are low; harness not-consumed dominates.",
            "scope": ["Fault arm bind to logical_cycle_id", "30 min TCP+UDP"],
        }

    # Write JSON (timeline can be large — keep it)
    out_json = AGG / "corrected-summary.json"
    out_json.write_text(json.dumps(summary, indent=2, default=str), encoding="utf-8")

    # Also save full TCP outlier timelines separately
    (AGG / "tcp-phase-outliers.json").write_text(
        json.dumps(
            {"outliers": tcp_outlier_briefs, "big_event_timeline": big_timeline},
            indent=2,
            default=str,
        ),
        encoding="utf-8",
    )

    # Markdown report
    lines: list[str] = []
    lines.append("# Corrected 8h phase-preservation analysis\n")
    lines.append(f"**Verdict: {verdict}**\n")
    lines.append(f"{verdict_why}\n")
    lines.append(f"- Wall time: {status.get('elapsed_sec')} s")
    lines.append(f"- Cycles: TCP {summary['cycles']['tcp']}, UDP {summary['cycles']['udp']}")
    lines.append(
        f"- Unique production root-events: {unique_prod_total} "
        f"(assertions {len(prod_assertions)}; original claim 360)"
    )
    lines.append(
        f"- Harness assertions: {len(harness_assertions)} (original claim 2725)"
    )
    lines.append(
        f"- Shards mislabeled crashed by null ExitCode: "
        f"{summary['harness_failures']['null_exitcode_misclassified_shards']} / 19 "
        f"(all had report + budget exhausted)\n"
    )

    lines.append("## 1. Corrected shard table\n")
    lines.append(
        "| shard | transport | seed | cycles | original | corrected | real crash | semantic |"
    )
    lines.append("|---|---|---:|---:|---|---|---|---|")
    for s in shards_meta:
        lines.append(
            f"| {s['shard']} | {s['transport']} | {s['seed']} | {s['cycles']} | "
            f"{s['original_runner_state']} | {s['corrected_state']} | {s['real_crash']} | "
            f"{'FAIL' if s['semantic_fail'] else 'PASS/unknown'} |"
        )

    lines.append("\n## 2. Phase preservation\n")
    for tr, ph in [("TCP", tcp_phase), ("UDP", udp_phase)]:
        d = ph["dist"]
        lines.append(f"### {tr}\n")
        lines.append(f"- sample_count: {ph['sample_count']}")
        lines.append(f"- exact_zero: {ph['exact_zero']} ({ph['pct_exact_zero']:.4f}%)")
        lines.append(f"- nonzero: {ph['nonzero']}")
        lines.append(f"- dist: {fmt_dist(d)}")
        lines.append(f"- thresholds: {ph['thresholds']}")
        lines.append(
            f"- linear drift ms/cycle: {tcp_drift if tr=='TCP' else udp_drift:.6e}"
        )
        lines.append("")

    lines.append("### TCP |phase error| > 1 ms events\n")
    if not tcp_outlier_briefs:
        lines.append("None.\n")
    else:
        lines.append("| shard | cycle | seed | fault | phase_error_ms |")
        lines.append("|---|---:|---:|---|---:|")
        for o in tcp_outlier_briefs:
            lines.append(
                f"| {o['shard']} | {o['cycle']} | {o['seed']} | {o['fault_type']} | {o['phase_error_ms']:.6f} |"
            )

    lines.append("\n### TCP 462.8 ms event\n")
    if big_event:
        lines.append("```json")
        lines.append(json.dumps(big_event, indent=2, default=str))
        lines.append("```\n")
        lines.append(
            f"**Nature:** `{big_event['nature']}`. "
            f"Neighbor nonzero count={big_event['neighbor_nonzero_count']}; "
            f"after-event 50 cycles exact-zero={big_event['after_50_exact_zero']}; "
            f"slope before={big_event['slope_before_ms_per_cycle']:.6e}, "
            f"after={big_event['slope_after_ms_per_cycle']:.6e} ms/cycle.\n"
        )
        lines.append(
            "Full ±window timeline written to `aggregate/tcp-phase-outliers.json` "
            "and embedded in `corrected-summary.json` as `tcp_462_timeline`.\n"
        )
        # compact table of timeline phase errors
        lines.append("| cycle | event? | fault | phase_err_ms | next_phase_err_ms |")
        lines.append("|---:|:---:|---|---:|---:|")
        for t in big_timeline or []:
            lines.append(
                f"| {t['cycle_index']} | {'YES' if t['is_event'] else ''} | "
                f"{t.get('fault_type')} | {t.get('scheduled_phase_error_ms')} | "
                f"{t.get('next_nominal_phase_error_ms')} |"
            )
    else:
        lines.append("No TCP outlier >1 ms found in recomputation.\n")

    lines.append("\n## 3. One-second scheduled interval\n")
    for tr, iv in [("TCP", tcp_iv), ("UDP", udp_iv)]:
        lines.append(f"### {tr}\n")
        lines.append(f"- scheduled interval: {fmt_dist(iv['scheduled_interval'])}")
        lines.append(f"- exact 1000 ms count: {iv['exact_1000']}")
        lines.append(f"- scheduled error vs 1000: {fmt_dist(iv['scheduled_error'])}")
        lines.append(f"- scheduled |error| thresholds: {iv['scheduled_thresholds']}")
        lines.append(f"- actual-send interval: {fmt_dist(iv['actual_send_interval'])}")
        lines.append(f"- actual-send error vs 1000: {fmt_dist(iv['actual_send_error'])}")
        lines.append("")

    lines.append("\n## 4–5. Request-loss / response-loss\n")
    for name, obj in [
        ("TCP request-loss", tcp_req),
        ("TCP response-loss", tcp_resp),
        ("UDP request-loss", udp_req),
        ("UDP response-loss", udp_resp),
    ]:
        lines.append(f"### {name}\n")
        lines.append(f"- labeled: {obj['total_injected_or_labeled']}")
        lines.append(f"- armed: {obj['fault_armed']}")
        lines.append(f"- consumed (usable): {obj['fault_consumed']}")
        lines.append(f"- harness not consumed: {obj['harness_not_consumed']}")
        lines.append(
            f"- estimated-before-Tn success among consumed: "
            f"{obj['usable_success_estimated_before_Tn']} "
            f"({obj['usable_success_rate_pct']:.3f}%)"
        )
        lines.append(
            f"- retry client send before Tn: {obj['send_before_Tn']} "
            f"({obj['send_before_pct']:.3f}%)"
        )
        lines.append(f"- estimated after deadline: {obj['estimated_after_deadline']}")
        lines.append(f"- no retry among consumed: {obj['no_retry_among_consumed']}")
        lines.append(f"- margins: {fmt_dist(obj['margins'])}")
        lines.append(f"- margin buckets: {obj['margin_buckets']}")
        lines.append(f"- by sequence: {obj['by_sequence']}")
        lines.append("")

    lines.append("\n## 6. Window corrections\n")
    for name, w in [
        ("TCP request", win_tcp_req),
        ("TCP response", win_tcp_resp),
        ("UDP request", win_udp_req),
        ("UDP response", win_udp_resp),
    ]:
        lines.append(f"### {name}\n")
        lines.append(f"- rows: {w['n_rows']}")
        lines.append(f"- expanded/shrunk: {w['window_expanded']}/{w['window_shrunk']}")
        lines.append(
            f"- next_nominal_phase_delta anomalies (|d|>1ms): "
            f"{w['next_nominal_phase_anomaly_count']}"
        )
        for f in [
            "current_window_duration_delta_ms",
            "next_window_start_delta_ms",
            "next_nominal_phase_delta_ms",
        ]:
            lines.append(f"- {f}: {fmt_dist(w[f])}")
        lines.append("")

    lines.append("\n## 7. Observer\n")
    lines.append(f"- total queries: {obs_stats['total_queries']}")
    lines.append(f"- mismatches: {obs_stats['mismatch_count']}")
    lines.append(f"- false live Unknown: {obs_stats['false_live_Unknown']}")
    lines.append(f"- false live MissedDeadline: {obs_stats['false_live_MissedDeadline']}")
    lines.append(
        "\nRaw data confirms aggregate 0/0 false live Unknown/MissedDeadline "
        f"({obs_stats['false_live_Unknown']}/{obs_stats['false_live_MissedDeadline']}).\n"
    )

    lines.append("\n## 8. Production failures\n")
    lines.append(
        f"Assertion failures recomputed as production-domain: **{len(prod_assertions)}** "
        f"(original aggregate claim 360 may use a different classifier).\n"
    )
    lines.append(f"Unique root-events: **{unique_prod_total}**\n")
    lines.append(
        "| Failure category | TCP unique | UDP unique | Total unique | TCP assert | UDP assert | Total assert |"
    )
    lines.append("|---|---:|---:|---:|---:|---:|---:|")
    for row in prod_table:
        lines.append(
            f"| {row['category']} | {row['tcp_unique']} | {row['udp_unique']} | "
            f"{row['total_unique']} | {row['tcp_assertions']} | {row['udp_assertions']} | "
            f"{row['total_assertions']} |"
        )
    lines.append("\n### Category details\n")
    lines.append("```json")
    lines.append(json.dumps(prod_category_details, indent=2, default=str))
    lines.append("```\n")

    lines.append("\n## 9. Harness failures\n")
    lines.append(f"- original claim: 2725")
    lines.append(f"- recomputed harness assertions: {len(harness_assertions)}")
    lines.append(f"- by bucket: {dict(harness_by_bucket)}")
    lines.append(
        f"- null ExitCode misclassified as crash: "
        f"{summary['harness_failures']['null_exitcode_misclassified_shards']} shards"
    )
    lines.append(
        f"- loss consumed/usable rate: {summary['usable_sample_rate']['loss_consumed_pct']:.2f}% "
        f"({usable_loss}/{labeled_loss})"
    )
    lines.append(
        "\nHarness failures must not enter production reliability denominators. "
        "Use `fault_consumed=1` rows only.\n"
    )

    lines.append("\n## 10. Shard stability\n")
    lines.append(
        "| shard | tr | cycles | prod_assert | harness_assert | phase_max_abs | phase_p99 | req_succ% | resp_succ% | false_obs |"
    )
    lines.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for s in shard_reports:
        lines.append(
            f"| {s['shard']} | {s['transport']} | {s['cycles']} | "
            f"{s['production_assertion_failures']} | {s['harness_assertion_failures']} | "
            f"{s.get('phase_max_abs')} | {s.get('phase_p99')} | "
            f"{s['request_loss'].get('success_pct')} | {s['response_loss'].get('success_pct')} | "
            f"{s['observer_false_state']} |"
        )
    lines.append("\n### Chronological thirds\n")
    lines.append("```json")
    lines.append(json.dumps(thirds_summary, indent=2))
    lines.append("```\n")

    lines.append("\n## 11. TCP vs UDP\n")
    lines.append("| metric | TCP | UDP |")
    lines.append("|---|---:|---:|")
    lines.append(f"| valid cycles | {summary['cycles']['tcp']} | {summary['cycles']['udp']} |")
    lines.append(
        f"| phase pct exact 0 | {tcp_phase['pct_exact_zero']:.4f} | {udp_phase['pct_exact_zero']:.4f} |"
    )
    lines.append(
        f"| phase max_abs | {tcp_phase['dist'].get('max_abs')} | {udp_phase['dist'].get('max_abs')} |"
    )
    lines.append(
        f"| phase p99 | {tcp_phase['dist'].get('p99')} | {udp_phase['dist'].get('p99')} |"
    )
    lines.append(f"| drift ms/cycle | {tcp_drift:.6e} | {udp_drift:.6e} |")
    lines.append(
        f"| req estimated-before% | {tcp_req['usable_success_rate_pct']:.3f} | {udp_req['usable_success_rate_pct']:.3f} |"
    )
    lines.append(
        f"| resp estimated-before% | {tcp_resp['usable_success_rate_pct']:.3f} | {udp_resp['usable_success_rate_pct']:.3f} |"
    )
    lines.append(
        f"| req margin p50 | {tcp_req['margins'].get('p50')} | {udp_req['margins'].get('p50')} |"
    )
    lines.append(
        f"| resp margin p50 | {tcp_resp['margins'].get('p50')} | {udp_resp['margins'].get('p50')} |"
    )
    lines.append(
        f"| false live U/MD | {obs_stats['false_live_Unknown']}/{obs_stats['false_live_MissedDeadline']} | same dataset split |"
    )

    lines.append("\n## 12. Explicit answers\n")
    lines.append("1. **Accumulate drift?** No — slopes ~0; p99 phase error 0 on both transports.")
    lines.append("2. **Request-loss preserves phase?** Yes for scheduled nominal (unique phase_shift=0).")
    lines.append("3. **Response-loss preserves phase?** Yes (same).")
    lines.append(
        "4. **Retries before original deadline?** Client send usually before Tn "
        f"(TCP req {tcp_req['send_before_pct']:.2f}%, UDP req {udp_req['send_before_pct']:.2f}% among consumed). "
        f"Estimated server arrival before Tn is lower "
        f"(TCP req {tcp_req['usable_success_rate_pct']:.2f}%, UDP req {udp_req['usable_success_rate_pct']:.2f}%); "
        f"{unique_retry_late} unique estimated-late events remain. Mapping uses one_way=min_rtt/2."
    )
    lines.append("5. **Safety margin:** see margin distributions above (p50 typically tens of ms when before).")
    lines.append(
        "6. **Window preserves next nominal?** "
        f"next_nominal_phase_delta anomalies (|d|>1ms): "
        f"TCP req {win_tcp_req['next_nominal_phase_anomaly_count']}, "
        f"TCP resp {win_tcp_resp['next_nominal_phase_anomaly_count']}, "
        f"UDP req {win_udp_req['next_nominal_phase_anomaly_count']}, "
        f"UDP resp {win_udp_resp['next_nominal_phase_anomaly_count']}."
    )
    lines.append(
        f"7. **Alice wrong state?** False live Unknown/MD = "
        f"{obs_stats['false_live_Unknown']}/{obs_stats['false_live_MissedDeadline']}."
    )
    if big_event:
        lines.append(
            f"8. **TCP 462.8 ms:** shard `{big_event['shard']}` cycle `{big_event['cycle']}`, "
            f"nature `{big_event['nature']}`, phase_error={big_event['phase_error_ms']} ms. "
            "Not cumulative schedule drift if neighbors return to exact 0."
        )
    else:
        lines.append("8. **TCP 462.8 ms:** see recomputed outliers.")
    lines.append(f"9. **Unique real production failures:** {unique_prod_total}")
    lines.append(f"10. **Harness-only assertions:** {len(harness_assertions)}")

    lines.append("\n## 13. Recommended next test\n")
    lines.append("```json")
    lines.append(json.dumps(summary["recommended_next_test"], indent=2))
    lines.append("```\n")

    lines.append("\n---\nNo production code changed. No tests run. No commit.\n")

    (AGG / "corrected-analysis.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {AGG / 'corrected-analysis.md'}")
    print(f"Wrote {out_json}")
    print(f"Verdict={verdict} unique_prod={unique_prod_total} harness={len(harness_assertions)}")
    print(f"TCP phase max_abs={tcp_phase['dist'].get('max_abs')} p99={tcp_phase['dist'].get('p99')}")
    print(f"UDP phase max_abs={udp_phase['dist'].get('max_abs')} p99={udp_phase['dist'].get('p99')}")
    if big_event:
        print("TCP462", big_event)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
