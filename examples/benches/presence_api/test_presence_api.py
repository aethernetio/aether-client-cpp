import argparse
import csv
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import AsyncMock, Mock, patch

from analyze import PresenceTimeline, analyze, measure, query_diagnostics
from run_campaign import HealthTracker, TraceReader, Workers, campaign


def event(kind, time, a=0, b=0, c=0):
    return dict(event=kind, t=time, a=str(a), b=str(b), c=str(c))


class DetectionTest(unittest.TestCase):
    def test_unknown_and_error_do_not_count_as_offline(self):
        rows = [event("remote_result", 3500000, 1, 1, 100000),
                event("remote_result", 4200000, 2, -1, 100000),
                event("remote_error", 4500000, 3, 17, 200000),
                event("remote_result", 6000000, 4, 0, 100000),
                event("remote_result", 9500000, 5, 0, 100000),
                event("remote_query_start", 9900000, 6),
                event("remote_result", 10200000, 6, 1, 300000),
                event("remote_query_start", 10900000, 7),
                event("remote_result", 11200000, 7, 1, 300000)]
        result = measure(rows, 4000000, 10000000, 12000000, remote=True)
        self.assertEqual(result["offline_ms"], 2000)
        self.assertEqual(result["online_ms"], 200)
        self.assertEqual(result["fresh_query_online_ms"], 1200)
        self.assertTrue(result["online_query_started_before_restore"])
        counts = query_diagnostics(rows, 4000000, 10000000, 12000000)
        self.assertEqual(counts["unknown_results"], 1)
        self.assertEqual(counts["error_results"], 1)
        self.assertEqual(counts["error_codes"], {"17": 1})

    def test_pending_and_missed_callback_are_availability_not_offline(self):
        rows = [event("remote_result", 3500000, 1, 1),
                event("remote_result", 5000000, 2, -1),
                event("remote_query_start", 8000000, 3),
                event("remote_query_busy", 9000000, 3, 1000000),
                event("remote_overdue", 10000000, 3),
                event("remote_unobserved", 10100000, 3, 0, 2100000),
                event("remote_query_start", 10900000, 4),
                event("remote_result", 11000000, 4, 1, 100000)]
        result = measure(rows, 4000000, 10000000, 12000000, remote=True)
        self.assertFalse(result["offline_observed"])
        self.assertFalse(result["recovery_required"])
        self.assertIsNone(result["offline_ms"])
        self.assertIsNone(result["online_ms"])
        self.assertEqual(result["first_online_after_restore_ms"], 1000)
        counts = query_diagnostics(rows, 4000000, 10000000, 12000000)
        self.assertEqual(counts["pending_at_restore"], [3])
        self.assertEqual(counts["pending_at_end"], [])
        self.assertEqual(counts["busy_ticks"], 1)
        self.assertEqual(counts["overdue_queries"], 1)
        self.assertEqual(counts["unobserved_results"], 1)

    def test_old_online_expires_to_stale_without_fabricating_offline(self):
        rows = [event("remote_result", 1000000, 1, 1),
                event("remote_query_start", 2000000, 2),
                event("remote_query_busy", 12000000, 2, 10000000)]
        timeline = PresenceTimeline(rows, remote=True)
        self.assertEqual(timeline.at(4000000), "online")
        self.assertEqual(timeline.at(4000001), "stale")
        result = measure(rows, 2000000, 10000000, 20000000, remote=True)
        self.assertEqual(result["state_at_restore"], "stale")
        self.assertFalse(result["offline_observed"])
        self.assertIsNone(result["stable_online_5s_ms"])

    def test_first_stable_recovery_and_terminal_recovery_are_distinct(self):
        rows = [event("remote_result", 3500000, 1, 1),
                event("remote_result", 5000000, 2, 0),
                event("remote_result", 9500000, 3, 0)]
        rows += [event("remote_result", t, t, 1) for t in range(10100000, 15200000, 1000000)]
        rows += [event("remote_error", 15300000, 99, 5)]
        rows += [event("remote_result", t, t, 1) for t in range(17000000, 24000000, 1000000)]
        result = measure(rows, 4000000, 10000000, 24000000, remote=True)
        self.assertEqual(result["online_ms"], 100)
        self.assertEqual(result["stable_online_5s_ms"], 100)
        self.assertEqual(result["stable_confirmation_ms"], 5100)
        self.assertEqual(result["terminal_online_5s_ms"], 7000)

    def test_short_outage_and_legacy_echo_do_not_invent_native_transitions(self):
        result = measure([event("local_state", 1000000, 1),
                          event("peer_reply", 6000000, 4, 5900000, 100000),
                          event("cloud_ack", 7000000, 1, 9, 6900000)],
                         2000000, 4000000, 8000000)
        self.assertIsNone(result["offline_ms"])
        self.assertIsNone(result["online_ms"])
        self.assertEqual(PresenceTimeline([event("cloud_ack", 7000000)]).at(8000000), "unobserved")


class HealthTest(unittest.TestCase):
    def test_health_requires_two_distinct_fresh_successful_api_results(self):
        health = HealthTracker()
        health.feed([event("local_state", 1, 1), event("remote_result", 1000000, 1, 1)])
        self.assertFalse(health.healthy(2000000))
        health.feed([event("remote_result", 1500000, 1, 1)])  # Duplicate qid.
        self.assertFalse(health.healthy(2000000))
        health.feed([event("remote_result", 2000000, 2, 1)])
        self.assertTrue(health.healthy(2000000))
        self.assertFalse(health.healthy(5000001))

    def test_unknown_error_unobserved_and_local_false_invalidate_health(self):
        for failure in (event("remote_result", 2500000, 3, -1),
                        event("remote_error", 2500000, 3, 2),
                        event("remote_unobserved", 2500000, 3)):
            with self.subTest(failure=failure["event"]):
                health = HealthTracker()
                health.feed([event("local_state", 1, 1),
                             event("remote_result", 1000000, 1, 1),
                             event("remote_result", 2000000, 2, 1), failure])
                self.assertFalse(health.healthy(2600000))
                health.feed([event("remote_result", 3000000, 4, 1)])
                self.assertFalse(health.healthy(3000000))
                health.feed([event("local_sample", 4000000, 1),
                             event("remote_result", 4000000, 5, 1)])
                self.assertTrue(health.healthy(4000000))
                health.feed([event("local_state", 4100000, 0)])
                self.assertFalse(health.healthy(4100000))

    def test_incremental_reader_handles_partial_lines_and_retains_local_state(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "events.csv"
            path.write_bytes(b"event,mono_us,a,b,c\nlocal_state,1,1,0,0\nremote_result,1000000,1,1,")
            reader, health = TraceReader(path), HealthTracker()
            health.feed(reader.read())
            self.assertFalse(health.healthy(2000000))
            with path.open("ab") as file:
                file.write(b"100000\nremote_result,2000000,2,1,100000\n")
            health.feed(reader.read())
            self.assertTrue(health.healthy(2000000))
            self.assertEqual(reader.read(), [])

    def test_local_only_gate_does_not_invent_remote_online(self):
        health = HealthTracker()
        health.feed([dict(event="local_state", t=100, a="1", b="0", c="0"),
                     dict(event="remote_result", t=100, a="1", b="-1", c="0")])
        self.assertFalse(health.healthy(100))
        self.assertTrue(health.healthy(100, require_remote=False))
        self.assertEqual("-1", health.outcomes[1]["b"])
        worker = Workers(Path("unused"), Path("unused"), {}, Mock(), require_remote=False)
        worker.processes = {"A": Mock()}
        worker.health = {"A": health}
        worker.readers = {"A": Mock(read=Mock(return_value=[]))}
        worker.check_running = Mock()
        with patch("run_campaign.us", return_value=100):
            self.assertTrue(worker.healthy())
        worker.require_remote = True
        self.assertFalse(worker.healthy())

    def test_memory_guard_immediately_kills_only_the_owned_live_pair(self):
        worker = Workers(Path("unused"), Path("unused"), {}, Mock(), 256)
        worker.processes = {side: Mock(poll=Mock(return_value=None), pid=pid)
                            for side, pid in (("A", 101), ("B", 102))}
        worker.peaks = {"A": 0, "B": 0}
        with patch("run_campaign.working_set_bytes", return_value=300 * 1024 * 1024):
            with self.assertRaisesRegex(RuntimeError, "memory limit"):
                worker.check_running()
        for process in worker.processes.values():
            process.kill.assert_called_once_with()
        self.assertEqual([call.args[0] for call in worker.journal.log.call_args_list],
                         ["memory_limit", "worker_memory_killed", "worker_memory_killed"])

    def test_normal_worker_exit_does_not_force_kill_the_peer(self):
        worker = Workers(Path("unused"), Path("unused"), {}, Mock(), 256)
        worker.processes = {"A": Mock(poll=Mock(return_value=2)),
                            "B": Mock(poll=Mock(return_value=None))}
        with self.assertRaisesRegex(RuntimeError, "worker A exited"):
            worker.check_running()
        for process in worker.processes.values():
            process.kill.assert_not_called()


class ControllerTest(unittest.IsolatedAsyncioTestCase):
    async def test_stable_health_restarts_confirmation_after_bad_sample(self):
        worker = Workers(Path("unused"), Path("unused"), {}, Mock())
        worker.healthy = Mock(side_effect=[True, False, True, True, True])
        clock = [0]
        async def advance(_):
            clock[0] += 1
        with patch("run_campaign.time.monotonic", side_effect=lambda: clock[0]), \
             patch("run_campaign.asyncio.sleep", side_effect=advance):
            self.assertTrue(await worker.wait_healthy(10, stable_seconds=2))
        self.assertEqual(clock[0], 4)

    async def test_startup_failure_is_recorded_without_injecting_any_cut(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / "probe.exe"
            exe.write_bytes(b"test fixture")
            out = Path(folder) / "campaign"
            worker = Mock(start=AsyncMock(), stop=AsyncMock(),
                          wait_healthy=AsyncMock(return_value=False), segment=1)
            proxies = []
            def proxy_factory(*_):
                proxy = Mock(close=AsyncMock())
                proxy.gate.is_set.return_value = True
                proxy.start = AsyncMock(return_value=proxy)
                proxies.append(proxy)
                return proxy
            args = argparse.Namespace(exe=exe, out=out, max_worker_mb=256, seed=1,
                                      startup_budget=0, baseline=0, repeats=1,
                                      off=[3], recovery=0, recovery_budget=0,
                                      stable_recovery=5)
            with patch("run_campaign.Workers", return_value=worker), \
                 patch("run_campaign.FaultProxy", side_effect=proxy_factory):
                with self.assertRaisesRegex(RuntimeError, "No genuine"):
                    await campaign(args)
            controller = [json.loads(line) for line in (out / "controller.jsonl").read_text().splitlines()]
            self.assertTrue(any(row["event"] == "startup_failed" for row in controller))
            self.assertFalse(any(row["event"] == "cut" for row in controller))
            self.assertTrue(all(not proxy.block.called for proxy in proxies))


class ArtifactTest(unittest.TestCase):
    def test_end_to_end_artifacts_use_api_events_and_keep_boundary_provenance(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            controller = [dict(event="cut", mono_us=4000000, trial=1, side="A", duration=6, segment=1),
                          dict(event="restore", mono_us=10000000, trial=1),
                          dict(event="trial_end", mono_us=20000000, trial=1, recovered=True),
                          dict(event="complete", mono_us=20000001)]
            (root / "controller.jsonl").write_text("\n".join(map(json.dumps, controller)))
            for side in ("A", "B"):
                (root / side).mkdir()
                rows = ([event("local_state", 1000000, 1), event("local_state", 5000000, 0),
                         event("local_state", 10400000, 1)] if side == "A" else
                        [event("remote_result", 3500000, 1, 1), event("remote_result", 6000000, 2, 0),
                         event("remote_query_start", 9900000, 3), event("remote_result", 10200000, 3, 1, 300000)])
                with (root / side / "events-1.csv").open("w", newline="") as file:
                    writer = csv.writer(file)
                    writer.writerow(("event", "mono_us", "a", "b", "c"))
                    writer.writerows((r["event"], r["t"], r["a"], r["b"], r["c"]) for r in rows)
            result = analyze(root)
            self.assertEqual(result["completed_trials"], 1)
            self.assertEqual(result["trials"][0]["online_ms"], 400)
            self.assertEqual(result["trials"][1]["online_ms"], 200)
            self.assertTrue(result["trials"][1]["online_query_started_before_restore"])
            self.assertIsNone(result["trials"][1]["terminal_online_5s_ms"])
            self.assertTrue((root / "trials.csv").exists())
            self.assertEqual(result["remote_presence_source"], "Client::QueryPeerPresence")


if __name__ == "__main__":
    unittest.main()
