#!/usr/bin/env python3
# Copyright 2026 Aethernet Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Smoke tests for the TEST-ONLY opaque browser transport gateway."""

from __future__ import annotations

import asyncio
import json
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
GATEWAY = ROOT / "gateway.py"


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


async def _echo_tcp(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    try:
        while True:
            data = await reader.read(65536)
            if not data:
                break
            writer.write(data)
            await writer.drain()
    finally:
        writer.close()


async def _run_echo(port: int, stop: asyncio.Event) -> None:
    server = await asyncio.start_server(_echo_tcp, "127.0.0.1", port)
    async with server:
        await stop.wait()


def _http(
    method: str,
    url: str,
    origin: str | None,
    body: bytes | None = None,
    content_type: str | None = None,
    timeout: float = 5.0,
) -> tuple[int, dict, bytes]:
    headers = {}
    if origin is not None:
        headers["Origin"] = origin
    if content_type:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, dict(resp.headers), resp.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def main() -> int:
    origin_ok = "http://127.0.0.1:8080"
    origin_bad = "http://evil.example"
    echo_port = _free_port()
    http_port = _free_port()
    ws_port = _free_port()

    stop = asyncio.Event()
    loop = asyncio.new_event_loop()

    def _echo_thread():
        asyncio.set_event_loop(loop)
        loop.run_until_complete(_run_echo(echo_port, stop))

    import threading

    t = threading.Thread(target=_echo_thread, daemon=True)
    t.start()
    time.sleep(0.2)

    proc = subprocess.Popen(
        [
            sys.executable,
            str(GATEWAY),
            "--listen",
            f"127.0.0.1:{http_port}",
            "--ws-listen",
            f"127.0.0.1:{ws_port}",
            "--origin",
            origin_ok,
            "--target",
            f"local=127.0.0.1:{echo_port}",
            "--max-body",
            "64",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    time.sleep(0.6)
    base = f"http://127.0.0.1:{http_port}"
    failures = 0

    def check(name: str, cond: bool) -> None:
        nonlocal failures
        if cond:
            print(f"PASS {name}")
        else:
            print(f"FAIL {name}")
            failures += 1

    # CORS preflight
    status, headers, _ = _http("OPTIONS", f"{base}/aether/v1/connect", origin_ok)
    check("cors_preflight_status", status == 204)
    check(
        "cors_preflight_aco",
        headers.get("Access-Control-Allow-Origin") == origin_ok
        or headers.get("access-control-allow-origin") == origin_ok,
    )

    # Invalid origin
    status, _, body = _http(
        "POST",
        f"{base}/aether/v1/connect",
        origin_bad,
        b'{"target":"local"}',
        "application/json",
    )
    check("invalid_origin", status == 403)

    # Invalid destination
    status, _, body = _http(
        "POST",
        f"{base}/aether/v1/connect",
        origin_ok,
        b'{"target":"not-allowlisted"}',
        "application/json",
    )
    check("invalid_dest", status == 403)

    # Connect + binary preservation
    status, _, body = _http(
        "POST",
        f"{base}/aether/v1/connect",
        origin_ok,
        b'{"target":"local"}',
        "application/json",
    )
    check("connect_ok", status == 200)
    session = json.loads(body.decode("utf-8"))["session"]
    check("session_unpredictable", len(session) >= 16)

    payload = bytes([0x00, 0x01, 0xFF, 0x7E, 0x80])
    status, _, _ = _http(
        "POST",
        f"{base}/aether/v1/session/{session}/send",
        origin_ok,
        payload,
        "application/octet-stream",
    )
    check("send_ok", status == 204)

    status, headers, recv = _http(
        "GET",
        f"{base}/aether/v1/session/{session}/receive",
        origin_ok,
        timeout=5.0,
    )
    check("receive_ok", status == 200)
    check("binary_preservation", recv == payload)

    # Oversized body
    big = b"x" * 65
    status, _, _ = _http(
        "POST",
        f"{base}/aether/v1/session/{session}/send",
        origin_ok,
        big,
        "application/octet-stream",
    )
    check("oversized", status == 413)

    # Unknown session
    status, _, _ = _http(
        "POST",
        f"{base}/aether/v1/session/does-not-exist/send",
        origin_ok,
        b"hi",
        "application/octet-stream",
    )
    check("unknown_session", status == 404)

    status, _, _ = _http(
        "POST",
        f"{base}/aether/v1/session/{session}/close",
        origin_ok,
    )
    check("close_ok", status == 204)

    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
    stop.set()
    time.sleep(0.1)

    if failures:
        print(f"{failures} failure(s)")
        return 1
    print("All gateway tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
