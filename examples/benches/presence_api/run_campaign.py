"""Application-scoped silence injection. Never changes firewall/NIC/routes.

Each SDK process connects only to its own loopback listener. ConnectEx sends
IPv4+port in a six-byte preamble. The proxy pauses BOTH relay directions, with
backpressure and byte preservation (no corruption of the TCP/framing stream).
"""
import argparse
import asyncio
import csv
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import socket
import struct
import subprocess
import time
from udp_fault_proxy import UdpFaultProxy


def us():
    return time.perf_counter_ns() // 1000


def working_set_bytes(process):
    handle = getattr(process, "_handle", None)
    if os.name != "nt" or not isinstance(handle, int):
        return None
    from ctypes import wintypes

    class Counters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("faults", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in (
                "peak", "working_set", "peak_paged", "paged",
                "peak_nonpaged", "nonpaged", "pagefile", "peak_pagefile", "private")]

    info = Counters()
    info.cb = ctypes.sizeof(info)
    get_info = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
    get_info.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
    get_info.restype = wintypes.BOOL
    if not get_info(handle, ctypes.byref(info), info.cb):
        raise ctypes.WinError(ctypes.get_last_error())
    return info.working_set


class Journal:
    def __init__(self, path):
        self.file = Path(path).open("w", encoding="utf-8", buffering=1)

    def log(self, event, **values):
        row = dict(event=event, mono_us=us(), utc=time.time(), **values)
        self.file.write(json.dumps(row) + "\n")
        return row


class FaultProxy:
    def __init__(self, side, journal):
        self.side = side
        self.journal = journal
        self.gate = asyncio.Event()
        self.gate.set()
        self.server = None
        self.tasks = set()
        self.bytes = {"out": 0, "in": 0}
        self.connections = 0

    async def start(self):
        self.server = await asyncio.start_server(self.accept, "127.0.0.1", 0)
        self.port = self.server.sockets[0].getsockname()[1]
        self.journal.log("proxy_start", side=self.side, port=self.port)
        return self

    def block(self, blocked, **metadata):
        if blocked:
            self.gate.clear()
        else:
            self.gate.set()
        # No awaits between changing the gate and recording its effective time.
        return self.journal.log("cut" if blocked else "restore", side=self.side,
                                forwarded=dict(self.bytes), **metadata)

    async def accept(self, client_r, client_w):
        task = asyncio.current_task()
        self.tasks.add(task)
        remote_w = None
        pumps = []
        connection = self.connections
        self.connections += 1
        try:
            header = await asyncio.wait_for(client_r.readexactly(6), 15)
            host = socket.inet_ntoa(header[:4])
            port = struct.unpack("!H", header[4:])[0]
            self.journal.log("connect", side=self.side, connection=connection,
                             host=host, port=port)
            # New connections are also gated. DNS and application event loops
            # remain alive; cloud TCP traffic cannot bypass this listener.
            await self.gate.wait()
            remote_r, remote_w = await asyncio.wait_for(
                asyncio.open_connection(host, port), 15)

            async def relay(reader, writer, direction):
                while True:
                    await self.gate.wait()
                    data = await reader.read(65536)
                    if not data:
                        return
                    await self.gate.wait()
                    writer.write(data)
                    self.bytes[direction] += len(data)
                    await writer.drain()

            pumps = [asyncio.create_task(relay(client_r, remote_w, "out")),
                     asyncio.create_task(relay(remote_r, client_w, "in"))]
            done, _ = await asyncio.wait(pumps, return_when=asyncio.FIRST_COMPLETED)
            for future in done:
                future.result()
        except (OSError, asyncio.TimeoutError, asyncio.IncompleteReadError) as error:
            self.journal.log("proxy_error", side=self.side, connection=connection,
                             error=repr(error))
        finally:
            for pump in pumps:
                pump.cancel()
            await asyncio.gather(*pumps, return_exceptions=True)
            for writer in (client_w, remote_w):
                if writer:
                    writer.close()
                    try:
                        await writer.wait_closed()
                    except OSError:
                        pass
            self.tasks.discard(task)
            self.journal.log("closed", side=self.side, connection=connection,
                             forwarded=dict(self.bytes))

    async def close(self):
        self.server.close()
        await self.server.wait_closed()
        for task in list(self.tasks):
            task.cancel()
        await asyncio.gather(*list(self.tasks), return_exceptions=True)


def require_proxy_traffic(proxies):
    for side, proxy in proxies.items():
        if proxy.connections == 0 or not all(proxy.bytes.values()):
            raise RuntimeError(f"No two-way tunnel traffic for {side}; cannot validate application-scoped cuts")


class HealthTracker:
    """Only real API observations; errors and UNKNOWN never become OFFLINE."""
    def __init__(self):
        self.local = None
        self.local_at = -1
        self.outcomes = {}

    def feed(self, rows):
        for row in rows:
            if row["event"] in ("local_state", "local_sample") and row["t"] >= self.local_at:
                self.local, self.local_at = int(row["a"]), row["t"]
            if row["event"] in ("remote_result", "remote_error", "remote_unobserved"):
                self.outcomes[int(row["a"])] = row
        if len(self.outcomes) > 128:
            newest = sorted(self.outcomes, key=lambda q: self.outcomes[q]["t"])[-64:]
            self.outcomes = {q: self.outcomes[q] for q in newest}

    def healthy(self, now, require_remote=True):
        local_online = self.local == 1 and 0 <= now - self.local_at <= 3000000
        if not require_remote:
            return local_online
        recent = sorted(self.outcomes.values(), key=lambda row: row["t"])[-2:]
        return local_online and len(recent) == 2 and all(
            row["event"] == "remote_result" and int(row["b"]) == 1 and
            0 <= now - row["t"] <= 3000000 for row in recent)


class TraceReader:
    """Incremental parsing preserves the last local state in long campaigns."""
    def __init__(self, path):
        self.path, self.offset, self.partial = path, 0, b""

    def read(self):
        if not self.path.exists():
            return []
        with self.path.open("rb") as file:
            file.seek(self.offset)
            data = file.read(1048576)
            self.offset = file.tell()
        chunks = (self.partial + data).split(b"\n")
        self.partial = chunks.pop()
        rows = []
        for cells in csv.reader(line.decode("utf-8") for line in chunks):
            if len(cells) != 5 or cells[0] == "event":
                continue
            try:
                rows.append(dict(event=cells[0], t=int(cells[1]),
                                 a=cells[2], b=cells[3], c=cells[4]))
            except ValueError:
                continue
        return rows


class Workers:
    def __init__(self, exe, out, proxies, journal, max_worker_mb=256, require_remote=True,
                 identity_name=None):
        self.exe, self.out, self.proxies, self.journal = exe, out, proxies, journal
        self.max_bytes = int(max_worker_mb * 1024 * 1024)
        self.require_remote = require_remote
        self.identity_name = identity_name or out.name
        self.processes, self.handles = {}, []
        self.segment = 0

    async def start(self):
        self.segment += 1
        self.readers, self.health, self.peaks = {}, {}, {}
        for side in ("A", "B"):
            folder = self.out / side
            folder.mkdir(exist_ok=True)
            stop = folder / "stop"
            stop.unlink(missing_ok=True)
            trace = folder / f"events-{self.segment}.csv"
            stdout = (folder / f"stdout-{self.segment}.log").open("w")
            stderr = (folder / f"stderr-{self.segment}.log").open("w")
            self.handles += [stdout, stderr]
            peer = self.out / ("B" if side == "A" else "A") / "uid.txt"
            command = [str(self.exe), "--side", side,
                       "--name", f"presence_api_{self.identity_name}_{side}",
                       "--uid-file", str(folder / "uid.txt"),
                       "--peer-file", str(peer), "--stop-file", str(stop),
                       "--trace", str(trace), "--seconds", "43200"]
            env = dict(os.environ, AE_BENCH_TCP_PROXY_PORT=str(self.proxies[side].port),
                       AE_BENCH_UDP_PROXY_PORT=str(self.proxies[side].port),
                       AE_BENCH_PING_CONTROL=str(folder / "ping-control.txt"))
            self.processes[side] = subprocess.Popen(
                command, cwd=folder, env=env, stdout=stdout, stderr=stderr,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            self.readers[side], self.health[side] = TraceReader(trace), HealthTracker()
            self.peaks[side] = 0
        self.journal.log("workers_start", segment=self.segment,
                         pids={s: p.pid for s, p in self.processes.items()})

    def check_running(self):
        for side, process in self.processes.items():
            if process.poll() is not None:
                raise RuntimeError(f"worker {side} exited {process.returncode}; see {self.out / side}")
            size = working_set_bytes(process)
            if size is not None:
                self.peaks[side] = max(self.peaks[side], size)
                if size > self.max_bytes:
                    self.journal.log("memory_limit", side=side, segment=self.segment,
                                     working_set_bytes=size, limit_bytes=self.max_bytes)
                    # A runaway allocator must not keep growing through the
                    # cooperative shutdown grace. Kill only our owned pair.
                    for owned_side, owned_process in self.processes.items():
                        if owned_process.poll() is None:
                            owned_process.kill()
                            self.journal.log("worker_memory_killed", side=owned_side,
                                             segment=self.segment, pid=owned_process.pid)
                    raise RuntimeError(f"worker {side} exceeded its memory limit")

    def healthy(self):
        self.check_running()
        for side in self.processes:
            self.health[side].feed(self.readers[side].read())
        now = us()
        return bool(self.processes) and all(h.healthy(now, self.require_remote) for h in self.health.values())

    async def wait_healthy(self, budget, stable_seconds=0):
        deadline, since = time.monotonic() + budget, None
        while True:
            now = time.monotonic()
            if self.healthy():
                since = now if since is None else since
                if now - since >= stable_seconds:
                    return True
            else:
                since = None
            if now >= deadline:
                return False
            await asyncio.sleep(min(.25, deadline - now))

    async def sleep_checked(self, duration):
        deadline = time.monotonic() + duration
        while True:
            self.check_running()
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            await asyncio.sleep(min(.25, remaining))

    async def stop(self):
        for side in self.processes:
            (self.out / side / "stop").touch()
        deadline = time.monotonic() + 12
        while any(p.poll() is None for p in self.processes.values()) and time.monotonic() < deadline:
            await asyncio.sleep(.2)
        for side, process in self.processes.items():
            if process.poll() is None:
                self.journal.log("worker_killed", side=side, segment=self.segment)
                process.kill()
            await asyncio.to_thread(process.wait)
            self.journal.log("worker_exit", side=side, segment=self.segment,
                             code=process.returncode, sampled_peak_bytes=self.peaks.get(side, 0))
        for handle in self.handles:
            handle.close()
        self.handles.clear()
        self.processes.clear()


async def campaign(args):
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    identity_source = getattr(args, "identity_source", None)
    if identity_source:
        identity_source = identity_source.resolve()
        for side in ("A", "B"):
            shutil.copytree(identity_source / side / "state", out / side / "state")
    config = dict(vars(args), executable_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                  runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  local_presence_source="Client::IsLocallyOnline",
                  remote_presence_source="Client::QueryPeerPresence",
                  query_period_ms=1000, healthy_result_max_age_ms=3000,
                  fault_model="per_process_udp_drop" if getattr(args, "transport", "tcp") == "udp" else "per_process_tcp_silence",
                  baseline_requirement="local_only" if getattr(args, "local_only", False) else "local_and_remote")
    (out / "config.json").write_text(json.dumps(config, default=str, indent=2))
    journal = Journal(out / "controller.jsonl")
    proxies = {}
    workers = Workers(args.exe.resolve(), out, proxies, journal, args.max_worker_mb,
                      require_remote=not getattr(args, "local_only", False),
                      identity_name=identity_source.name if identity_source else None)
    rng = random.Random(args.seed)
    try:
        for side in ("A", "B"):
            proxy_type = UdpFaultProxy if getattr(args, "transport", "tcp") == "udp" else FaultProxy
            proxies[side] = await proxy_type(side, journal).start()
        await workers.start()
        if not await workers.wait_healthy(args.startup_budget, args.stable_recovery):
            journal.log("startup_failed", segment=workers.segment)
            raise RuntimeError("No genuine local/remote API ONLINE baseline; no faults injected")
        require_proxy_traffic(proxies)
        journal.log("baseline_start", segment=workers.segment)
        await workers.sleep_checked(args.baseline)
        journal.log("baseline_end", segment=workers.segment)
        if getattr(args, "ping_faults", False):
            for mode in ("ping", "ack"):
                for count in args.ping_counts:
                    for side in ("A", "B"):
                        if not await workers.wait_healthy(30, args.stable_recovery):
                            raise RuntimeError("No healthy API baseline before typed Ping omission")
                        token = f"{mode}-{count}-{side}"
                        control = out / side / "ping-control.txt"
                        control.write_text(f"{token} {mode} {count}\n")
                        journal.log("ping_fault_arm", side=side, token=token,
                                    mode=mode, count=count, segment=workers.segment)
                        await workers.sleep_checked(max(12, count * 4))
                        stderr = (out / side / f"stderr-{workers.segment}.log").read_text(errors="replace")
                        injected = stderr.count(f"PING_FAULT,{token},{mode},")
                        control.write_text(f"{token}-clear none 0\n")
                        if injected != count:
                            raise RuntimeError(f"Requested {count} {mode} omissions; observed {injected}")
                        recovered = await workers.wait_healthy(args.recovery_budget, args.stable_recovery)
                        journal.log("ping_fault_end", side=side, token=token,
                                    count=injected, recovered=recovered, segment=workers.segment)
                        print("typed omission", token, "recovered", recovered, flush=True)
                        if not recovered:
                            raise RuntimeError("API did not recover after typed Ping omission")
        trial = 0
        for repeat in range(args.repeats):
            for duration in args.off:
                for side in ("A", "B"):
                    await workers.sleep_checked(rng.uniform(.05, .95))
                    if not await workers.wait_healthy(30, args.stable_recovery):
                        journal.log("precondition_failed", segment=workers.segment,
                                    next_trial=trial + 1)
                        raise RuntimeError("Both API observers must be healthy before a fault")
                    trial += 1
                    proxies[side].block(True, trial=trial, duration=duration,
                                        repeat=repeat, segment=workers.segment)
                    before = dict(proxies[side].bytes)
                    print("cut", trial, side, duration, flush=True)
                    await workers.sleep_checked(duration)
                    if before != proxies[side].bytes:
                        raise AssertionError("Relay bytes moved while the process gate was closed")
                    proxies[side].block(False, trial=trial, segment=workers.segment)
                    await workers.sleep_checked(args.recovery)
                    recovered = await workers.wait_healthy(args.recovery_budget, args.stable_recovery)
                    journal.log("trial_end", trial=trial, side=side, recovered=recovered,
                                segment=workers.segment)
                    print("restored", trial, "API healthy", recovered, flush=True)
                    if not recovered:
                        await workers.stop()
                        await asyncio.sleep(7)
                        await workers.start()
                        if not await workers.wait_healthy(args.startup_budget, args.stable_recovery):
                            journal.log("startup_failed", segment=workers.segment)
                            raise RuntimeError("API baseline did not return after restarting owned workers")
        journal.log("complete", trials=trial)
    except BaseException as error:
        journal.log("failed", error=repr(error))
        raise
    finally:
        for proxy in proxies.values():
            if not proxy.gate.is_set():
                proxy.block(False, cleanup=True)
        await workers.stop()
        for proxy in proxies.values():
            await proxy.close()
        journal.file.close()


def main():
    parser = argparse.ArgumentParser(description="Benchmark real SDK presence APIs; default is a two-cut pilot.")
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--transport", choices=("tcp", "udp"), default="tcp")
    parser.add_argument("--identity-source", type=Path,
                        help="Reuse only a previous campaign's private SDK state and identity names")
    parser.add_argument("--ping-faults", action="store_true")
    parser.add_argument("--ping-counts", type=int, nargs="+", default=[1, 3])
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--local-only", action="store_true",
                        help="Measure local API when remote API replies are unavailable; remote results remain recorded, never synthesized")
    parser.add_argument("--baseline", type=float, default=20)
    parser.add_argument("--startup-budget", type=float, default=150)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--off", type=float, nargs="+", default=[3])
    parser.add_argument("--recovery", type=float, default=20)
    parser.add_argument("--recovery-budget", type=float, default=90)
    parser.add_argument("--stable-recovery", type=float, default=5)
    parser.add_argument("--max-worker-mb", type=float, default=256)
    parser.add_argument("--seed", type=int, default=20261112)
    args = parser.parse_args()
    durations = [args.baseline, args.startup_budget, args.recovery,
                 args.recovery_budget, args.stable_recovery, *args.off]
    if min(durations) < 0 or args.repeats < 1 or args.max_worker_mb <= 0:
        parser.error("Durations must be nonnegative, repeats and memory limit positive")
    if not args.exe.is_file():
        parser.error("--exe must be an existing probe executable")
    asyncio.run(campaign(args))


if __name__ == "__main__":
    main()
