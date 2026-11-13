"""Analyze SDK API observations. Missing/unknown results never imply OFFLINE."""
import argparse
import bisect
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path


TERMINAL = {"remote_result", "remote_error", "remote_unobserved"}


def distribution(values):
    ordered = sorted(values)
    def percentile(p):
        if not ordered:
            return None
        at = (len(ordered) - 1) * p
        low, high = int(at), min(int(at) + 1, len(ordered) - 1)
        return ordered[low] + (ordered[high] - ordered[low]) * (at - low)
    return dict(n=len(values), p50=percentile(.5), p95=percentile(.95),
                p99=percentile(.99), max=max(values) if values else None)


def load_events(path):
    with path.open(newline="", encoding="utf-8") as file:
        return [dict(event=r["event"], t=int(r["mono_us"]),
                     a=r["a"], b=r["b"], c=r["c"])
                for r in csv.DictReader(file) if r.get("c") not in (None, "")]


class PresenceTimeline:
    def __init__(self, events, remote=False, freshness_us=3000000):
        self.remote, self.freshness_us = remote, freshness_us
        self.rows = []
        for event in sorted(events, key=lambda row: row["t"]):
            kind = event["event"]
            if not remote and kind == "local_state":
                state = "online" if int(event["a"]) == 1 else "offline"
            elif remote and kind in TERMINAL:
                state = ({1: "online", 0: "offline", -1: "unknown"}.get(int(event["b"]), "unknown")
                         if kind == "remote_result" else kind.removeprefix("remote_"))
            else:
                continue
            if not remote and self.rows and self.rows[-1]["state"] == state:
                continue
            self.rows.append(dict(event, state=state))
        self.times = [row["t"] for row in self.rows]

    def at(self, time):
        index = bisect.bisect_right(self.times, time) - 1
        if index < 0:
            return "unobserved"
        row = self.rows[index]
        if self.remote and row["state"] in ("online", "offline") and time - row["t"] > self.freshness_us:
            return "stale"
        return row["state"]

    def intervals(self, start, end):
        if end <= start:
            return []
        boundaries = {start, end}
        for row in self.rows[bisect.bisect_left(self.times, start - self.freshness_us):]:
            if row["t"] > end:
                break
            if start < row["t"] < end:
                boundaries.add(row["t"])
            expires = row["t"] + self.freshness_us + 1
            if self.remote and row["state"] in ("online", "offline") and start < expires < end:
                boundaries.add(expires)
        result = []
        times = sorted(boundaries)
        for begin, finish in zip(times, times[1:]):
            state = self.at(begin)
            if result and result[-1][2] == state:
                result[-1] = result[-1][0], finish, state
            else:
                result.append((begin, finish, state))
        return result


def query_diagnostics(events, cut, restore, end):
    starts = {int(row["a"]): row["t"] for row in events if row["event"] == "remote_query_start"}
    terminals = [row for row in events if row["event"] in TERMINAL]
    window = [row for row in events if cut <= row["t"] <= end]
    def pending_at(at):
        completed = {int(row["a"]) for row in terminals if row["t"] <= at}
        return sorted(q for q, sent in starts.items() if sent <= at and q not in completed)
    return dict(
        unknown_results=sum(row["event"] == "remote_result" and int(row["b"]) == -1 for row in window),
        error_results=sum(row["event"] == "remote_error" for row in window),
        error_codes=dict(Counter(row["b"] for row in window if row["event"] == "remote_error")),
        unobserved_results=sum(row["event"] == "remote_unobserved" for row in window),
        overdue_queries=len({row["a"] for row in window if row["event"] == "remote_overdue"}),
        busy_ticks=sum(row["event"] == "remote_query_busy" for row in window),
        pending_at_restore=pending_at(restore), pending_at_end=pending_at(end),
        result_duration_ms=distribution([int(row["c"]) / 1000 for row in window if row["event"] in TERMINAL]),
        results_started_before_cut=sum(row["event"] in TERMINAL and starts.get(int(row["a"]), cut) < cut for row in window),
        post_restore_results_started_before_restore=sum(row["event"] in TERMINAL and row["t"] >= restore and starts.get(int(row["a"]), restore) < restore for row in window))


def measure(events, cut, restore, end, remote=False, freshness_us=3000000):
    timeline = PresenceTimeline(events, remote, freshness_us)
    before = timeline.at(cut)
    offline = next((row for row in timeline.rows if cut <= row["t"] < restore and row["state"] == "offline"), None)
    online = next((row for row in timeline.rows if restore <= row["t"] <= end and row["state"] == "online"), None)
    # Recovery from OFFLINE requires an actual SDK OFFLINE observation. The
    # first positive observation after restoration is also retained separately.
    required = offline is not None and before == "online" and timeline.at(restore) != "online"
    spans = timeline.intervals(restore, end)
    stable = next((begin for begin, finish, state in spans if state == "online" and finish - begin >= 5000000), None)
    terminal = (spans[-1][0] if spans and spans[-1][2] == "online" and
                spans[-1][1] - spans[-1][0] >= 5000000 else None)
    starts = {int(row["a"]): row["t"] for row in events if row["event"] == "remote_query_start"}
    fresh = next((row for row in timeline.rows if restore <= row["t"] <= end and row["state"] == "online" and
                  (not remote or starts.get(int(row["a"]), -1) >= restore)), None)
    latency = lambda row: (row["t"] - restore) / 1000 if row else None
    return dict(state_before_cut=before, state_at_restore=timeline.at(restore),
                state_at_end=timeline.at(end), offline_observed=offline is not None,
                offline_ms=(offline["t"] - cut) / 1000 if offline and before == "online" else None,
                recovery_required=required,
                online_ms=latency(online) if required else None,
                first_online_after_restore_ms=latency(online),
                fresh_query_online_ms=latency(fresh) if required else None,
                stable_online_5s_ms=(stable - restore) / 1000 if required and stable is not None else None,
                stable_confirmation_ms=(stable - restore) / 1000 + 5000 if required and stable is not None else None,
                terminal_online_5s_ms=(terminal - restore) / 1000 if required and terminal is not None else None,
                offline_query_started_before_cut=(starts.get(int(offline["a"]), cut) < cut) if remote and offline else None,
                online_query_started_before_restore=(starts.get(int(online["a"]), restore) < restore) if remote and online else None,
                observation_ms=(end - restore) / 1000)


def analyze(root):
    config_path = root / "config.json"
    config = json.loads(config_path.read_text(encoding="utf-8")) if config_path.exists() else {}
    freshness_us = int(config.get("healthy_result_max_age_ms", 3000) * 1000)
    controller = [json.loads(line) for line in (root / "controller.jsonl").read_text().splitlines()]
    files = {(side, int(path.stem.split("-")[-1])): load_events(path)
             for side in ("A", "B") for path in (root / side).glob("events-*.csv")}
    trials = []
    cuts = [row for row in controller if row["event"] == "cut"]
    for cut in cuts:
        restore = next((r for r in controller if r["event"] == "restore" and r.get("trial") == cut["trial"]), None)
        if restore is None:
            continue
        finished = next((r for r in controller if r["event"] == "trial_end" and r.get("trial") == cut["trial"]), None)
        end = finished["mono_us"] if finished else controller[-1]["mono_us"]
        for detector, remote in (("local_api", False), ("remote_api", True)):
            observer = ("B" if cut["side"] == "A" else "A") if remote else cut["side"]
            events = files.get((observer, cut["segment"]), [])
            row = dict(trial=cut["trial"], fault_side=cut["side"], observer=observer,
                       detector=detector, segment=cut["segment"], off_seconds=cut["duration"],
                       pair_recovered=finished.get("recovered", False) if finished else False,
                       source="Client::QueryPeerPresence" if remote else "Client::IsLocallyOnline",
                       query_diagnostics={},
                       **measure(events, cut["mono_us"], restore["mono_us"], end, remote, freshness_us))
            if remote:
                row["query_diagnostics"] = query_diagnostics(events, cut["mono_us"], restore["mono_us"], end)
            trials.append(row)
    summary = []
    for detector in ("local_api", "remote_api"):
        rows = [r for r in trials if r["detector"] == detector]
        summary.append(dict(detector=detector, trials=len(rows),
                            recovery_required=sum(r["recovery_required"] for r in rows),
                            offline_not_observed=sum(not r["offline_observed"] for r in rows),
                            preexisting_non_online=sum(r["state_before_cut"] != "online" for r in rows),
                            online_censored=sum(r["recovery_required"] and r["online_ms"] is None for r in rows),
                            stable_censored=sum(r["recovery_required"] and r["stable_online_5s_ms"] is None for r in rows),
                            terminal_censored=sum(r["recovery_required"] and r["terminal_online_5s_ms"] is None for r in rows),
                            **{key: distribution([r[key] for r in rows if r[key] is not None])
                               for key in ("offline_ms", "online_ms", "fresh_query_online_ms", "stable_online_5s_ms", "terminal_online_5s_ms")}))
    typed_omissions = []
    for arm in (r for r in controller if r["event"] == "ping_fault_arm"):
        finish = next((r for r in controller if r["event"] == "ping_fault_end" and
                       r.get("token") == arm["token"] and r.get("segment") == arm.get("segment")), None)
        end = finish["mono_us"] if finish else controller[-1]["mono_us"]
        for side in ("A", "B"):
            events = files.get((side, arm.get("segment", 1)), [])
            for detector, remote in (("local_api", False), ("remote_api", True)):
                timeline = PresenceTimeline(events, remote, freshness_us)
                spans = timeline.intervals(arm["mono_us"], end)
                offline = [(a, b) for a, b, state in spans if state == "offline"]
                occupancy = defaultdict(int)
                for a, b, state in spans: occupancy[state] += b - a
                recovery = next((a for a, b, state in spans if state == "online" and
                                 offline and a >= offline[0][1]), None)
                typed_omissions.append(dict(token=arm["token"], fault_side=arm["side"],
                    mode=arm["mode"], requested_count=arm["count"],
                    injected_count=finish.get("count") if finish else None,
                    recovered=finish.get("recovered", False) if finish else False,
                    observer=side, detector=detector,
                    timing_origin="control armed; actual packet omission occurs on a later SDK ping/ack",
                    state_before=timeline.at(arm["mono_us"]),
                    first_offline_from_arm_ms=(offline[0][0] - arm["mono_us"]) / 1000 if offline else None,
                    first_recovered_from_arm_ms=(recovery - arm["mono_us"]) / 1000 if recovery is not None else None,
                    offline_episodes=len(offline),
                    longest_offline_ms=max((b - a for a, b in offline), default=0) / 1000,
                    state_seconds={state: duration / 1000000 for state, duration in occupancy.items()}))
    baseline = []
    for begin in (r for r in controller if r["event"] == "baseline_start"):
        finish = next((r for r in controller if r["event"] == "baseline_end" and r.get("segment") == begin.get("segment")), None)
        if not finish:
            continue
        for side in ("A", "B"):
            events = files.get((side, begin.get("segment", 1)), [])
            for detector, remote in (("local_api", False), ("remote_api", True)):
                spans = PresenceTimeline(events, remote, freshness_us).intervals(begin["mono_us"], finish["mono_us"])
                occupancy = defaultdict(int)
                for start, end, state in spans:
                    occupancy[state] += end - start
                baseline.append(dict(side=side, detector=detector,
                                     seconds=(finish["mono_us"] - begin["mono_us"]) / 1000000,
                                     state_seconds={s: value / 1000000 for s, value in occupancy.items()},
                                     offline_episodes=sum(state == "offline" for _, _, state in spans)))
    results = dict(baseline_requirement=json.loads((root / "config.json").read_text()).get("baseline_requirement", "local_and_remote") if (root / "config.json").exists() else "local_and_remote",
                   status="complete" if any(r["event"] == "complete" for r in controller) else "incomplete",
                   local_presence_source="Client::IsLocallyOnline",
                   remote_presence_source="Client::QueryPeerPresence",
                   remote_result_freshness_ms=freshness_us / 1000,
                   started_trials=len(cuts), analyzed_trials=len(trials) // 2,
                   completed_trials=sum(r["event"] == "trial_end" for r in controller),
                   failure_trials=[r["trial"] for r in controller if r["event"] == "trial_end" and not r["recovered"]],
                   worker_segments=sum(r["event"] == "workers_start" for r in controller),
                   failures=[r for r in controller if r["event"] in ("failed", "startup_failed", "precondition_failed", "memory_limit", "worker_killed")],
                   baseline=baseline, summary=summary, trials=trials, typed_omissions=typed_omissions,
                   queries=[dict(side=side, segment=segment, **query_diagnostics(rows, 0, 0, max((r["t"] for r in rows), default=0)))
                            for (side, segment), rows in sorted(files.items())])
    (root / "summary.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    if typed_omissions:
        with (root / "typed-omissions.csv").open("w", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, fieldnames=list(typed_omissions[0]))
            writer.writeheader()
            writer.writerows(dict(row, state_seconds=json.dumps(row["state_seconds"])) for row in typed_omissions)
    if trials:
        with (root / "trials.csv").open("w", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, fieldnames=list(trials[0]))
            writer.writeheader()
            writer.writerows(dict(row, query_diagnostics=json.dumps(row.get("query_diagnostics", {}))) for row in trials)
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.root), indent=2))
