#!/usr/bin/env python3
"""Aggregate 8h UAP phase-preservation shard outputs into final reports."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


def wilson(success: int, n: int, z: float = 1.96) -> tuple[float, float]:
    if n <= 0:
        return (float("nan"), float("nan"))
    p = success / n
    den = 1 + z * z / n
    centre = p + z * z / (2 * n)
    margin = z * math.sqrt((p * (1 - p) + z * z / (4 * n)) / n)
    return ((centre - margin) / den, (centre + margin) / den)


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


def dist(vals: list[float]) -> dict[str, Any]:
    clean = [float(v) for v in vals if v is not None and not (isinstance(v, float) and math.isnan(v))]
    clean = [v for v in clean if math.isfinite(v)]
    if not clean:
        return {"n": 0}
    s = sorted(clean)
    mean = statistics.fmean(s)
    stdev = statistics.pstdev(s) if len(s) > 1 else 0.0
    out = {
        "n": len(s),
        "min": s[0],
        "mean": mean,
        "stddev": stdev,
        "p1": percentile(s, 1),
        "p5": percentile(s, 5),
        "p10": percentile(s, 10),
        "p25": percentile(s, 25),
        "p50": percentile(s, 50),
        "p75": percentile(s, 75),
        "p90": percentile(s, 90),
        "p95": percentile(s, 95),
        "p99": percentile(s, 99),
        "p99_5": percentile(s, 99.5),
        "p99_9": percentile(s, 99.9),
        "max": s[-1],
        "max_abs": max(abs(x) for x in s),
        "final": s[-1],
    }
    return out


def fmt_dist(d: dict[str, Any], indent: str = "") -> str:
    if d.get("n", 0) == 0:
        return f"{indent}- n: 0\n"
    lines = [f"{indent}- n: {d['n']}"]
    for k in (
        "min",
        "mean",
        "stddev",
        "p1",
        "p5",
        "p10",
        "p25",
        "p50",
        "p75",
        "p90",
        "p95",
        "p99",
        "p99_5",
        "p99_9",
        "max",
    ):
        if k in d:
            lines.append(f"{indent}- {k}: {d[k]:.6f}")
    if "max_abs" in d:
        lines.append(f"{indent}- max_abs: {d['max_abs']:.6f}")
    return "\n".join(lines) + "\n"


def parse_float(x: str) -> float | None:
    if x is None:
        return None
    s = str(x).strip()
    if s == "" or s.lower() in {"null", "nan", "none"}:
        return None
    try:
        v = float(s)
    except ValueError:
        return None
    if not math.isfinite(v):
        return None
    return v


def parse_int(x: str) -> int | None:
    v = parse_float(x)
    return None if v is None else int(v)


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


def linear_drift(xs: list[float], ys: list[float]) -> tuple[float, float]:
    n = min(len(xs), len(ys))
    if n < 2:
        return (0.0, 0.0)
    x = xs[:n]
    y = ys[:n]
    mx = statistics.fmean(x)
    my = statistics.fmean(y)
    num = sum((a - mx) * (b - my) for a, b in zip(x, y))
    den = sum((a - mx) ** 2 for a in x)
    if den == 0:
        return (0.0, 0.0)
    slope = num / den  # per cycle index unit
    return (slope, slope * 3600.0)  # ms/cycle, ms/hour if cycle~1s


def margin_buckets(margins: list[float]) -> dict[str, int]:
    edges = [
        (">+200", lambda m: m > 200),
        ("+100..+200", lambda m: 100 < m <= 200),
        ("+50..+100", lambda m: 50 < m <= 100),
        ("+20..+50", lambda m: 20 < m <= 50),
        ("+10..+20", lambda m: 10 < m <= 20),
        ("+5..+10", lambda m: 5 < m <= 10),
        ("+2..+5", lambda m: 2 < m <= 5),
        ("+1..+2", lambda m: 1 < m <= 2),
        ("0..+1", lambda m: 0 <= m <= 1),
        ("-1..0", lambda m: -1 <= m < 0),
        ("-2..-1", lambda m: -2 <= m < -1),
        ("-5..-2", lambda m: -5 <= m < -2),
        ("-10..-5", lambda m: -10 <= m < -5),
        ("-20..-10", lambda m: -20 <= m < -10),
        ("<-20", lambda m: m < -20),
    ]
    out = {name: 0 for name, _ in edges}
    for m in margins:
        for name, pred in edges:
            if pred(m):
                out[name] += 1
                break
    return out


def classify(inv: str) -> str:
    s = inv.lower()
    if "schedule shifted" in s or "phase" in s and "shifted" in s:
        return "PRODUCTION_PHASE"
    if "after original deadline" in s:
        return "PRODUCTION_RETRY_ESTIMATED_LATE_ARRIVAL"
    if "window" in s:
        return "PRODUCTION_WINDOW"
    if "misseddeadline" in s or "unknown" in s and "false" in s:
        return "PRODUCTION_STATE"
    if "first request was sent" in s or "did not reach send" in s:
        return "HARNESS_FAULT_NOT_ARMED"
    if "retry did not reach" in s:
        return "HARNESS_FAULT_WRONG_CYCLE"
    if "querypeerreceive" in s or "observer" in s or "checkpoint" in s:
        return "HARNESS_QUERY"
    if "cycle not confirmed" in s or "no cycle start" in s:
        return "HARNESS_CYCLE_CONFIRM"
    if "reporting" in s:
        return "HARNESS_REPORTING"
    if "crash" in s:
        return "PROCESS_CRASH"
    if "timeout" in s:
        return "TIMEOUT"
    return "OTHER"


PROD_CLASSES = {
    "PRODUCTION_PHASE",
    "PRODUCTION_RETRY_LATE_SEND",
    "PRODUCTION_RETRY_ESTIMATED_LATE_ARRIVAL",
    "PRODUCTION_WINDOW",
    "PRODUCTION_STATE",
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    args = ap.parse_args()
    root = Path(args.root)
    runs = root / "runs"
    agg = root / "aggregate"
    fail_dir = root / "failure-cases"
    agg.mkdir(parents=True, exist_ok=True)
    fail_dir.mkdir(parents=True, exist_ok=True)

    all_rows: list[dict] = []
    shard_meta: list[dict] = []
    failures: list[dict] = []

    for shard_dir in sorted(runs.glob("*")):
        if not shard_dir.is_dir():
            continue
        transport = "tcp" if shard_dir.name.startswith("tcp") else "udp"
        meta_path = shard_dir / "shard-status.json"
        meta = {}
        if meta_path.exists():
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        sm = shard_dir / "shard-meta.json"
        if sm.exists():
            try:
                meta.update(json.loads(sm.read_text(encoding="utf-8")))
            except json.JSONDecodeError:
                pass
        meta["shard"] = shard_dir.name
        meta["transport"] = transport
        shard_meta.append(meta)

        samples = load_jsonl(shard_dir / "samples.jsonl")
        for r in samples:
            r["shard"] = shard_dir.name
            r.setdefault("transport", transport)
            all_rows.append(r)
            for inv in r.get("failures") or []:
                fc = classify(str(inv))
                failures.append(
                    {
                        "shard": shard_dir.name,
                        "transport": transport,
                        "cycle_index": r.get("cycle_index"),
                        "logical_ping_id": r.get("logical_ping_id"),
                        "fault_type": r.get("fault_type"),
                        "invariant": inv,
                        "failure_class": r.get("failure_class") or fc,
                        "sample": r,
                    }
                )

        # Also harvest failed-cases.json if present
        fc_path = shard_dir / "failed-cases.json"
        if fc_path.exists():
            try:
                for item in json.loads(fc_path.read_text(encoding="utf-8")):
                    inv = item.get("invariant") or ""
                    failures.append(
                        {
                            "shard": shard_dir.name,
                            "transport": transport,
                            "cycle_index": item.get("cycle"),
                            "invariant": inv,
                            "failure_class": classify(str(inv)),
                            "sample": item,
                        }
                    )
            except Exception:
                pass

    # Deduplicate failures loosely
    seen = set()
    uniq_fail = []
    for f in failures:
        key = (
            f.get("shard"),
            f.get("cycle_index"),
            f.get("invariant"),
            f.get("failure_class"),
        )
        if key in seen:
            continue
        seen.add(key)
        uniq_fail.append(f)
    failures = uniq_fail

    by_tr: dict[str, list[dict]] = {"tcp": [], "udp": []}
    for r in all_rows:
        by_tr.setdefault(r.get("transport", "?"), []).append(r)

    def subset(rows: list[dict], pred) -> list[dict]:
        return [r for r in rows if pred(r)]

    def phase_series(rows: list[dict]) -> tuple[dict, float, float]:
        errs = []
        xs = []
        for i, r in enumerate(rows):
            e = r.get("scheduled_phase_error_ms")
            if e is None:
                continue
            try:
                ev = float(e)
            except (TypeError, ValueError):
                continue
            if math.isfinite(ev):
                errs.append(ev)
                xs.append(float(r.get("cycle_index", i)))
        d = dist(errs)
        slope_c, slope_h = linear_drift(xs, errs)
        return d, slope_c, slope_h

    def report_transport(tr: str) -> dict[str, Any]:
        rows = by_tr.get(tr, [])
        base = subset(rows, lambda r: (r.get("fault_type") or "none") == "none")
        req = subset(rows, lambda r: r.get("fault_type") == "request-loss")
        resp = subset(rows, lambda r: r.get("fault_type") == "response-loss")
        mixed = subset(
            rows,
            lambda r: (r.get("fault_type") in {"request-loss", "response-loss"})
            and str(r.get("sequence", "")).startswith(("alternat", "periodic", "random")),
        )
        consec = subset(
            rows, lambda r: str(r.get("sequence", "")).startswith("consecutive")
        )

        sched_all, drift_c, drift_h = phase_series(rows)
        next_errs = [
            float(r["next_nominal_phase_error_ms"])
            for r in rows
            if r.get("next_nominal_phase_error_ms") is not None
            and math.isfinite(float(r["next_nominal_phase_error_ms"]))
        ]

        contig = [
            float(r["actual_interval_error_us"]) / 1000.0
            for r in rows
            if r.get("contiguous_cycle") == 1
            and r.get("actual_interval_error_us") is not None
        ]
        # samples.jsonl may not have interval; use skipped_slots
        contig2 = []
        skipped = []
        for r in rows:
            if r.get("contiguous_cycle") == 1 and r.get("skipped_slots") == 0:
                # approximate from consecutive scheduled if available
                pass
            if r.get("skipped_slots") not in (None, 0):
                try:
                    skipped.append(float(r["skipped_slots"]))
                except (TypeError, ValueError):
                    pass

        def loss_stats(loss_rows: list[dict]) -> dict[str, Any]:
            requested = len(loss_rows)
            armed = sum(1 for r in loss_rows if r.get("fault_armed") == 1)
            consumed = sum(1 for r in loss_rows if r.get("fault_consumed") == 1)
            margins = []
            before = after = none = unmeas = 0
            send_before = send_after = 0
            for r in loss_rows:
                if r.get("fault_consumed") != 1:
                    continue
                m = r.get("estimated_server_margin_ms")
                if m is None:
                    m = r.get("retry_server_margin_ms")
                rs = r.get("retry_actual_send_time")
                tn = r.get("Tn") or r.get("original_deadline")
                if rs is not None and tn is not None:
                    try:
                        if float(rs) <= float(tn):
                            send_before += 1
                        else:
                            send_after += 1
                    except (TypeError, ValueError):
                        pass
                if m is None:
                    # no retry
                    if r.get("retry_actual_send_time") in (None,):
                        none += 1
                    else:
                        unmeas += 1
                    continue
                try:
                    mv = float(m)
                except (TypeError, ValueError):
                    unmeas += 1
                    continue
                if not math.isfinite(mv):
                    unmeas += 1
                    continue
                margins.append(mv)
                if mv >= 0:
                    before += 1
                else:
                    after += 1
            valid = consumed  # production proportions exclude harness-invalid
            harness_invalid = requested - consumed
            recovered = before  # estimated before Tn among consumed
            def pct(a, b):
                return (100.0 * a / b) if b else float("nan")

            lo, hi = wilson(before, max(consumed, 1))
            return {
                "requested": requested,
                "fault_armed": armed,
                "fault_consumed": consumed,
                "harness_invalid": harness_invalid,
                "retry_send_before_Tn": send_before,
                "retry_send_after_Tn": send_after,
                "estimated_before_Tn": before,
                "estimated_after_Tn": after,
                "no_retry_or_unmeas": none + unmeas,
                "estimated_before_pct": pct(before, consumed),
                "estimated_before_wilson95": [lo, hi],
                "send_before_pct": pct(send_before, consumed),
                "margins": dist(margins),
                "margin_buckets": margin_buckets(margins),
            }

        guards = [
            float(r["computed_guard_us"]) / 1000.0
            for r in rows
            if r.get("computed_guard_us") not in (None,)
        ]
        # from CSV if present later; jsonl may lack guard - try samples.csv
        return {
            "transport": tr,
            "valid_cycles": len(rows),
            "baseline_cases": len(base),
            "request_loss_cases": len(req),
            "response_loss_cases": len(resp),
            "scheduled_phase": sched_all,
            "next_nominal_phase": dist(next_errs),
            "drift_ms_per_cycle": drift_c,
            "drift_ms_per_hour": drift_h,
            "phase_baseline": phase_series(base)[0],
            "phase_request": phase_series(req)[0],
            "phase_response": phase_series(resp)[0],
            "phase_mixed": phase_series(mixed)[0],
            "phase_consecutive": phase_series(consec)[0],
            "request_loss": loss_stats(req),
            "response_loss": loss_stats(resp),
            "skipped_slots": dist(skipped),
            "false_live_unknown": sum(
                1
                for f in failures
                if f.get("transport") == tr
                and "false Unknown" in str(f.get("invariant", ""))
            ),
            "false_live_missed": sum(
                1
                for f in failures
                if f.get("transport") == tr
                and "false MissedDeadline" in str(f.get("invariant", ""))
            ),
            "duplicates": sum(
                1
                for f in failures
                if f.get("transport") == tr and "duplicate" in str(f.get("invariant", "")).lower()
            ),
        }

    # Enrich from samples.csv where available
    csv_rows: list[dict] = []
    for shard_dir in sorted(runs.glob("*")):
        p = shard_dir / "samples.csv"
        if not p.exists():
            continue
        with p.open("r", encoding="utf-8", errors="replace", newline="") as f:
            reader = csv.DictReader(f)
            for row in reader:
                row["shard"] = shard_dir.name
                row["transport"] = "tcp" if shard_dir.name.startswith("tcp") else "udp"
                csv_rows.append(row)

    phase_csv_rows = []
    for shard_dir in sorted(runs.glob("*")):
        p = shard_dir / "phase-error-by-cycle.csv"
        if not p.exists():
            continue
        with p.open("r", encoding="utf-8", errors="replace", newline="") as f:
            reader = csv.DictReader(f)
            for row in reader:
                row["shard"] = shard_dir.name
                row["transport"] = "tcp" if shard_dir.name.startswith("tcp") else "udp"
                phase_csv_rows.append(row)

    def enrich_tr(rep: dict[str, Any], tr: str) -> dict[str, Any]:
        rows = [r for r in csv_rows if r.get("transport") == tr]
        phase_rows = [r for r in phase_csv_rows if r.get("transport") == tr]

        def col(name: str) -> list[float]:
            out = []
            for r in rows:
                v = parse_float(r.get(name, ""))
                if v is not None:
                    out.append(v)
            return out

        def phase_col(name: str, only_contig: bool | None = None) -> list[float]:
            out = []
            for r in phase_rows:
                if only_contig is True and parse_int(r.get("contiguous_cycle", "")) != 1:
                    continue
                if only_contig is False and parse_int(r.get("contiguous_cycle", "")) == 1:
                    continue
                v = parse_float(r.get(name, ""))
                if v is None:
                    continue
                if name.endswith("_us"):
                    out.append(v / 1000.0)
                else:
                    out.append(v)
            return out

        rep["guard_ms"] = dist(col("guard_ms"))
        rep["attempt_lead_ms"] = dist(col("attempt_lead_ms"))
        rep["first_attempt_offset_from_Tn_ms"] = dist(col("first_attempt_offset_from_Tn_ms"))
        rep["timeout_to_retry_send_ms"] = dist(col("timeout_to_retry_send_ms"))
        rep["first_attempt_to_retry_ms"] = dist(col("first_attempt_to_retry_ms"))
        rep["retry_client_margin_to_Tn_ms"] = dist(col("retry_client_margin_to_Tn_ms"))
        rep["estimated_server_margin_ms"] = dist(col("estimated_server_margin_ms"))
        rep["alice_query"] = {
            # observer csv if present
        }
        obs_vals = []
        for shard_dir in sorted(runs.glob(f"{tr}-*")):
            op = shard_dir / "observer-query-results.csv"
            if not op.exists():
                continue
            with op.open("r", encoding="utf-8", errors="replace", newline="") as f:
                for row in csv.DictReader(f):
                    # no rtt column filled yet; skip
                    pass
        contig_err = phase_col("actual_interval_error_us", only_contig=True)
        skipped_err = phase_col("actual_interval_error_us", only_contig=False)
        rep["contiguous_interval_error_ms"] = dist(contig_err)
        rep["skipped_cycle_interval_error_ms"] = dist(skipped_err)
        # long-term thirds
        if rows:
            n = len(rows)
            a = rows[: max(1, n // 10)]
            c = rows[max(1, n // 10) : max(1, n - n // 10)]
            b = rows[max(1, n - n // 10) :]

            def part(rs):
                return {
                    "n": len(rs),
                    "guard_p50": dist([parse_float(r.get("guard_ms")) or float("nan") for r in rs]).get("p50"),
                    "lead_p50": dist([parse_float(r.get("attempt_lead_ms")) or float("nan") for r in rs]).get("p50"),
                    "phase_p50": dist([parse_float(r.get("scheduled_phase_error_ms")) or float("nan") for r in rs]).get("p50"),
                    "margin_p50": dist([parse_float(r.get("estimated_server_margin_ms")) or float("nan") for r in rs]).get("p50"),
                }

            rep["stability"] = {"first_10pct": part(a), "middle_80pct": part(c), "last_10pct": part(b)}
        return rep

    tcp = enrich_tr(report_transport("tcp"), "tcp")
    udp = enrich_tr(report_transport("udp"), "udp")

    prod_fail = [f for f in failures if f.get("failure_class") in PROD_CLASSES]
    harness_fail = [f for f in failures if str(f.get("failure_class", "")).startswith("HARNESS")]
    other_fail = [f for f in failures if f not in prod_fail and f not in harness_fail]

    # Save failure timelines with neighbors
    by_shard_cycle = defaultdict(dict)
    for r in all_rows:
        by_shard_cycle[r.get("shard")][r.get("cycle_index")] = r
    saved = []
    for f in prod_fail:
        shard = f.get("shard")
        ci = f.get("cycle_index")
        if ci is None:
            continue
        try:
            ci = int(ci)
        except (TypeError, ValueError):
            continue
        window = []
        for j in range(ci - 3, ci + 4):
            if j in by_shard_cycle.get(shard, {}):
                window.append(by_shard_cycle[shard][j])
        item = {**f, "timeline_window": window}
        saved.append(item)
        outp = fail_dir / f"{shard}_cycle{ci}_{f.get('failure_class')}.json"
        outp.write_text(json.dumps(item, indent=2), encoding="utf-8")

    summary = {
        "shards": shard_meta,
        "tcp": tcp,
        "udp": udp,
        "failures": {
            "production": len(prod_fail),
            "harness": len(harness_fail),
            "other": len(other_fail),
            "total": len(failures),
        },
        "overall": "PASS"
        if len(prod_fail) == 0 and len(all_rows) > 0
        else ("PARTIAL" if len(all_rows) > 0 else "FAIL"),
    }
    (agg / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    (agg / "failures.json").write_text(json.dumps(failures, indent=2), encoding="utf-8")

    def write_tr_md(path: Path, rep: dict[str, Any]) -> None:
        lines = [f"# {rep['transport'].upper()} 8h phase-preservation report\n"]
        lines.append(f"- valid_cycles: {rep.get('valid_cycles')}")
        lines.append(f"- baseline_cases: {rep.get('baseline_cases')}")
        lines.append(f"- request_loss_cases: {rep.get('request_loss_cases')}")
        lines.append(f"- response_loss_cases: {rep.get('response_loss_cases')}")
        lines.append("\n## Scheduled phase error (ms)\n")
        lines.append(fmt_dist(rep.get("scheduled_phase") or {}))
        lines.append(f"- drift_ms_per_cycle: {rep.get('drift_ms_per_cycle')}")
        lines.append(f"- drift_ms_per_hour: {rep.get('drift_ms_per_hour')}")
        lines.append("\n## Next nominal phase error (ms)\n")
        lines.append(fmt_dist(rep.get("next_nominal_phase") or {}))
        lines.append("\n## Guard / attempt lead\n")
        lines.append("### guard_ms\n" + fmt_dist(rep.get("guard_ms") or {}))
        lines.append("### attempt_lead_ms\n" + fmt_dist(rep.get("attempt_lead_ms") or {}))
        lines.append("### first_attempt_offset_from_Tn_ms\n" + fmt_dist(rep.get("first_attempt_offset_from_Tn_ms") or {}))
        lines.append("\n## Retry timing\n")
        lines.append("### timeout_to_retry_send_ms\n" + fmt_dist(rep.get("timeout_to_retry_send_ms") or {}))
        lines.append("### first_attempt_to_retry_ms\n" + fmt_dist(rep.get("first_attempt_to_retry_ms") or {}))
        for kind in ("request_loss", "response_loss"):
            ls = rep.get(kind) or {}
            lines.append(f"\n## {kind}\n")
            for k, v in ls.items():
                if k in {"margins", "margin_buckets"}:
                    continue
                lines.append(f"- {k}: {v}")
            lines.append("\n### estimated_server_margins\n" + fmt_dist(ls.get("margins") or {}))
            lines.append("\n### margin buckets (positive = before Tn)\n")
            for bk, bv in (ls.get("margin_buckets") or {}).items():
                lines.append(f"- {bk}: {bv}")
        lines.append("\n## Contiguous interval error (ms from 1000)\n")
        lines.append(fmt_dist(rep.get("contiguous_interval_error_ms") or {}))
        lines.append("\n## Skipped-cycle interval error (ms)\n")
        lines.append(fmt_dist(rep.get("skipped_cycle_interval_error_ms") or {}))
        lines.append("\n## Stability thirds\n")
        lines.append("```json\n" + json.dumps(rep.get("stability") or {}, indent=2) + "\n```\n")
        lines.append(f"\n- false_live_Unknown: {rep.get('false_live_unknown')}")
        lines.append(f"- false_live_MissedDeadline: {rep.get('false_live_missed')}")
        lines.append(f"- duplicates: {rep.get('duplicates')}")
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    write_tr_md(agg / "tcp-report.md", tcp)
    write_tr_md(agg / "udp-report.md", udp)

    # CSV extracts
    def write_csv(name: str, fieldnames: list[str], rows: list[dict]) -> None:
        with (agg / name).open("w", encoding="utf-8", newline="") as f:
            w = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
            w.writeheader()
            for r in rows:
                w.writerow(r)

    write_csv(
        "phase.csv",
        [
            "transport",
            "shard",
            "cycle_index",
            "fault_type",
            "scheduled_phase_error_ms",
            "next_nominal_phase_error_ms",
            "contiguous_cycle",
            "skipped_slots",
        ],
        all_rows,
    )
    write_csv(
        "retry-margins.csv",
        [
            "transport",
            "shard",
            "cycle_index",
            "fault_type",
            "fault_consumed",
            "estimated_server_margin_ms",
            "one_way_estimate_us",
        ],
        all_rows,
    )
    write_csv(
        "retry-timing.csv",
        [
            "transport",
            "shard",
            "cycle_index",
            "fault_type",
            "timeout_to_retry_send_ms",
            "first_attempt_to_retry_ms",
            "retry_client_margin_to_Tn_ms",
        ],
        csv_rows,
    )
    write_csv(
        "latency.csv",
        [
            "transport",
            "shard",
            "cycle_index",
            "guard_ms",
            "attempt_lead_ms",
            "first_attempt_offset_from_Tn_ms",
        ],
        csv_rows,
    )
    write_csv(
        "windows.csv",
        ["transport", "shard", "cycle_index", "fault_type", "next_scheduled_nominal", "next_nominal_phase_error_ms"],
        csv_rows,
    )
    write_csv(
        "alice-query.csv",
        ["transport", "shard", "cycle_index", "alice_state", "alice_next_ping_delta_ms"],
        csv_rows,
    )

    comparison = f"""# 8h TCP vs UDP comparison

- overall: {summary['overall']}
- production failures: {summary['failures']['production']}
- harness failures: {summary['failures']['harness']}
- other/process failures: {summary['failures']['other']}

## TCP
- cycles: {tcp.get('valid_cycles')}
- scheduled phase max_abs: {(tcp.get('scheduled_phase') or {}).get('max_abs')}
- drift ms/cycle: {tcp.get('drift_ms_per_cycle')}
- drift ms/hour: {tcp.get('drift_ms_per_hour')}
- request estimated-before-Tn %: {(tcp.get('request_loss') or {}).get('estimated_before_pct')}
- response estimated-before-Tn %: {(tcp.get('response_loss') or {}).get('estimated_before_pct')}

## UDP
- cycles: {udp.get('valid_cycles')}
- scheduled phase max_abs: {(udp.get('scheduled_phase') or {}).get('max_abs')}
- drift ms/cycle: {udp.get('drift_ms_per_cycle')}
- drift ms/hour: {udp.get('drift_ms_per_hour')}
- request estimated-before-Tn %: {(udp.get('request_loss') or {}).get('estimated_before_pct')}
- response estimated-before-Tn %: {(udp.get('response_loss') or {}).get('estimated_before_pct')}

See tcp-report.md / udp-report.md for full percentile tables.
"""
    (agg / "comparison.md").write_text(comparison, encoding="utf-8")

    def brief(tr: str, rep: dict) -> str:
        sp = rep.get("scheduled_phase") or {}
        g = rep.get("guard_ms") or {}
        lead = rep.get("attempt_lead_ms") or {}
        ttr = rep.get("timeout_to_retry_send_ms") or {}
        contig = rep.get("contiguous_interval_error_ms") or {}
        rq = rep.get("request_loss") or {}
        rs = rep.get("response_loss") or {}
        rm = (rq.get("margins") or {})
        sm = (rs.get("margins") or {})
        return f"""{tr.upper()}:
valid cycles: {rep.get('valid_cycles')}
baseline cases: {rep.get('baseline_cases')}
request-loss cases: {rep.get('request_loss_cases')}
response-loss cases: {rep.get('response_loss_cases')}
guard p50/p95/p99/max: {g.get('p50')}/{g.get('p95')}/{g.get('p99')}/{g.get('max')}
attempt lead p50/p95/p99/max: {lead.get('p50')}/{lead.get('p95')}/{lead.get('p99')}/{lead.get('max')}
timeout->retry p50/p95/p99/max: {ttr.get('p50')}/{ttr.get('p95')}/{ttr.get('p99')}/{ttr.get('max')}
request-loss:
  recovery/estimated-before-Tn %: {rq.get('estimated_before_pct')}
  retry-send-before-Tn %: {rq.get('send_before_pct')}
  estimated server margin min/p1/p5/p50/p95: {rm.get('min')}/{rm.get('p1')}/{rm.get('p5')}/{rm.get('p50')}/{rm.get('p95')}
response-loss:
  recovery/estimated-before-Tn %: {rs.get('estimated_before_pct')}
  retry-send-before-Tn %: {rs.get('send_before_pct')}
  estimated server margin min/p1/p5/p50/p95: {sm.get('min')}/{sm.get('p1')}/{sm.get('p5')}/{sm.get('p50')}/{sm.get('p95')}
scheduled phase max abs / p99 / final: {sp.get('max_abs')}/{sp.get('p99')}/{sp.get('final')}
drift ms/cycle: {rep.get('drift_ms_per_cycle')}
drift ms/hour: {rep.get('drift_ms_per_hour')}
contiguous interval p50/p95/p99/max: {contig.get('p50')}/{contig.get('p95')}/{contig.get('p99')}/{contig.get('max')}
duplicates: {rep.get('duplicates')}
false live Unknown: {rep.get('false_live_unknown')}
false live MissedDeadline: {rep.get('false_live_missed')}
"""

    console = []
    console.append("Actual wall time: see status.json elapsed_sec")
    console.append(brief("tcp", tcp))
    console.append(brief("udp", udp))
    console.append(
        f"Failures:\n  production: {summary['failures']['production']}\n  harness: {summary['failures']['harness']}\n  process/other: {summary['failures']['other']}"
    )
    console.append(f"Overall: {summary['overall']}")
    (agg / "console-summary.txt").write_text("\n".join(console) + "\n", encoding="utf-8")
    print("\n".join(console))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
