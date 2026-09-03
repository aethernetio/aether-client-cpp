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

"""TEST-ONLY opaque browser↔TCP gateway. Not production transport support."""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
import secrets
import ssl
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple
from urllib.parse import parse_qs, urlparse

try:
    from websockets.asyncio.server import serve as ws_serve
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "Missing dependency: pip install -r tools/browser_transport_gateway/"
        "requirements.txt"
    ) from exc

LOG = logging.getLogger("ae.browser_gw")


@dataclass
class Config:
    listen_host: str = "127.0.0.1"
    listen_port: int = 18080
    ws_host: str = "127.0.0.1"
    ws_port: int = 18081
    origins: Set[str] = field(default_factory=set)
    targets: Dict[str, Tuple[str, int]] = field(default_factory=dict)
    tls_cert: Optional[str] = None
    tls_key: Optional[str] = None
    idle_seconds: float = 60.0
    max_body: int = 65536
    max_queue: int = 262144
    max_receives: int = 2
    default_target: Optional[str] = None


@dataclass
class Session:
    session_id: str
    target_name: str
    reader: asyncio.StreamReader
    writer: asyncio.StreamWriter
    inbound: asyncio.Queue
    queued_bytes: int = 0
    receives: int = 0
    last_active: float = field(default_factory=time.monotonic)
    closed: bool = False
    pump_task: Optional[asyncio.Task] = None


class Gateway:
    def __init__(self, config: Config) -> None:
        self.config = config
        self.sessions: Dict[str, Session] = {}
        self._lock = asyncio.Lock()
        self._reaper: Optional[asyncio.Task] = None

    def touch(self, session: Session) -> None:
        session.last_active = time.monotonic()

    def origin_ok(self, origin: Optional[str]) -> bool:
        return bool(origin) and origin in self.config.origins

    def cors_lines(self, origin: Optional[str]) -> List[str]:
        lines = [
            "Vary: Origin",
            "Access-Control-Allow-Methods: GET, POST, OPTIONS",
            "Access-Control-Allow-Headers: Content-Type",
            "Access-Control-Max-Age: 600",
        ]
        if origin and self.origin_ok(origin):
            lines.append(f"Access-Control-Allow-Origin: {origin}")
        return lines

    async def start(self) -> None:
        self._reaper = asyncio.create_task(self._reap_loop())

    async def stop(self) -> None:
        if self._reaper:
            self._reaper.cancel()
        async with self._lock:
            ids = list(self.sessions.keys())
        for sid in ids:
            await self.close_session(sid)

    async def _reap_loop(self) -> None:
        while True:
            await asyncio.sleep(5.0)
            now = time.monotonic()
            stale: List[str] = []
            async with self._lock:
                for sid, sess in self.sessions.items():
                    if now - sess.last_active > self.config.idle_seconds:
                        stale.append(sid)
            for sid in stale:
                LOG.info("idle expiry session_id=%s", sid)
                await self.close_session(sid)

    async def connect(self, target_name: Optional[str]) -> Tuple[int, bytes, str]:
        name = target_name or self.config.default_target
        if name is None or name not in self.config.targets:
            return 403, b'{"error":"destination_not_allowlisted"}', "application/json"
        host, port = self.config.targets[name]
        try:
            reader, writer = await asyncio.open_connection(host, port)
        except OSError:
            LOG.warning("tcp connect failed target=%s", name)
            return 502, b'{"error":"upstream_unavailable"}', "application/json"

        sid = secrets.token_urlsafe(24)
        session = Session(
            session_id=sid,
            target_name=name,
            reader=reader,
            writer=writer,
            inbound=asyncio.Queue(),
        )
        session.pump_task = asyncio.create_task(self._pump_tcp(session))
        async with self._lock:
            self.sessions[sid] = session
        LOG.info("session open id=%s target=%s", sid, name)
        return 200, json.dumps({"session": sid}).encode("utf-8"), "application/json"

    async def _pump_tcp(self, session: Session) -> None:
        try:
            while not session.closed:
                data = await session.reader.read(65536)
                if not data:
                    break
                self.touch(session)
                if session.queued_bytes + len(data) > self.config.max_queue:
                    LOG.warning("queue overflow session_id=%s", session.session_id)
                    break
                session.queued_bytes += len(data)
                await session.inbound.put(data)
        except Exception:
            LOG.info("tcp pump ended session_id=%s", session.session_id)
        finally:
            await self.close_session(session.session_id)

    async def send(self, sid: str, payload: bytes) -> Tuple[int, bytes, str]:
        async with self._lock:
            session = self.sessions.get(sid)
        if session is None or session.closed:
            return 404, b'{"error":"unknown_session"}', "application/json"
        if len(payload) > self.config.max_body:
            return 413, b'{"error":"body_too_large"}', "application/json"
        try:
            session.writer.write(payload)
            await session.writer.drain()
            self.touch(session)
        except Exception:
            await self.close_session(sid)
            return 502, b'{"error":"upstream_write_failed"}', "application/json"
        return 204, b"", "application/octet-stream"

    async def receive(
        self, sid: str, timeout: float = 25.0
    ) -> Tuple[int, bytes, str]:
        async with self._lock:
            session = self.sessions.get(sid)
        if session is None or session.closed:
            return 404, b'{"error":"unknown_session"}', "application/json"
        if session.receives >= self.config.max_receives:
            return 429, b'{"error":"too_many_receives"}', "application/json"
        session.receives += 1
        try:
            try:
                chunk = await asyncio.wait_for(session.inbound.get(), timeout=timeout)
            except asyncio.TimeoutError:
                return 204, b"", "application/octet-stream"
            session.queued_bytes = max(0, session.queued_bytes - len(chunk))
            self.touch(session)
            return 200, chunk, "application/octet-stream"
        finally:
            session.receives -= 1

    async def close_session(self, sid: str) -> Tuple[int, bytes, str]:
        async with self._lock:
            session = self.sessions.pop(sid, None)
        if session is None:
            return 404, b'{"error":"unknown_session"}', "application/json"
        session.closed = True
        if session.pump_task and not session.pump_task.done():
            session.pump_task.cancel()
        try:
            session.writer.close()
            await session.writer.wait_closed()
        except Exception:
            pass
        LOG.info("session close id=%s", sid)
        return 204, b"", "application/octet-stream"


def _parse_host_port(value: str) -> Tuple[str, int]:
    if ":" not in value:
        raise argparse.ArgumentTypeError("expected host:port")
    host, _, port_s = value.rpartition(":")
    return host or "127.0.0.1", int(port_s)


def _parse_target(value: str) -> Tuple[str, str, int]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("target must be name=host:port")
    name, _, addr = value.partition("=")
    host, port = _parse_host_port(addr)
    return name, host, port


def build_config(argv: Optional[List[str]] = None) -> Config:
    env_listen = os.environ.get("AE_GW_LISTEN", "127.0.0.1:18080")
    env_ws = os.environ.get("AE_GW_WS_LISTEN", "")
    env_origins = [
        o.strip() for o in os.environ.get("AE_GW_ORIGINS", "").split(",") if o.strip()
    ]
    env_targets = [
        t.strip() for t in os.environ.get("AE_GW_TARGETS", "").split(",") if t.strip()
    ]

    p = argparse.ArgumentParser(
        description="TEST-ONLY opaque Aether browser transport gateway"
    )
    p.add_argument("--listen", default=env_listen, help="HTTP(S) host:port")
    p.add_argument(
        "--ws-listen",
        default=env_ws,
        help="WS(S) host:port (default: HTTP host, HTTP port+1)",
    )
    p.add_argument("--origin", action="append", default=[], help="Allowlisted Origin")
    p.add_argument(
        "--target",
        action="append",
        default=[],
        help="Allowlisted destination name=host:port",
    )
    p.add_argument("--tls-cert", default=os.environ.get("AE_GW_TLS_CERT"))
    p.add_argument("--tls-key", default=os.environ.get("AE_GW_TLS_KEY"))
    p.add_argument(
        "--idle-seconds",
        type=float,
        default=float(os.environ.get("AE_GW_IDLE_SECONDS", "60")),
    )
    p.add_argument(
        "--max-body", type=int, default=int(os.environ.get("AE_GW_MAX_BODY", "65536"))
    )
    p.add_argument(
        "--max-queue",
        type=int,
        default=int(os.environ.get("AE_GW_MAX_QUEUE", "262144")),
    )
    p.add_argument(
        "--max-receives",
        type=int,
        default=int(os.environ.get("AE_GW_MAX_RECEIVES", "2")),
    )
    args = p.parse_args(argv)

    host, port = _parse_host_port(args.listen)
    if args.ws_listen:
        ws_host, ws_port = _parse_host_port(args.ws_listen)
    else:
        ws_host, ws_port = host, port + 1

    origins = set(env_origins)
    origins.update(args.origin)
    targets: Dict[str, Tuple[str, int]] = {}
    for t in env_targets + list(args.target):
        name, th, tp = _parse_target(t)
        targets[name] = (th, tp)

    cfg = Config(
        listen_host=host,
        listen_port=port,
        ws_host=ws_host,
        ws_port=ws_port,
        origins=origins,
        targets=targets,
        tls_cert=args.tls_cert,
        tls_key=args.tls_key,
        idle_seconds=args.idle_seconds,
        max_body=args.max_body,
        max_queue=args.max_queue,
        max_receives=args.max_receives,
    )
    if targets:
        cfg.default_target = next(iter(targets))
    return cfg


async def _read_http_request(
    reader: asyncio.StreamReader, max_body: int
) -> Tuple[str, str, Dict[str, str], bytes]:
    header_blob = await reader.readuntil(b"\r\n\r\n")
    header_text = header_blob.decode("latin1")
    lines = header_text.split("\r\n")
    request_line = lines[0]
    method, path, _ = request_line.split(" ", 2)
    headers: Dict[str, str] = {}
    for line in lines[1:]:
        if not line or ":" not in line:
            continue
        k, _, v = line.partition(":")
        headers[k.strip().lower()] = v.strip()
    length = int(headers.get("content-length", "0") or "0")
    if length > max_body * 2:
        # Hard cap before reading (allow slight headroom for JSON connect).
        raise ValueError("body_too_large")
    body = b""
    if length > 0:
        body = await reader.readexactly(length)
    return method.upper(), path, headers, body


def _http_response(
    status: int,
    reason: str,
    headers: List[str],
    body: bytes,
) -> bytes:
    lines = [f"HTTP/1.1 {status} {reason}"] + headers + [f"Content-Length: {len(body)}", "", ""]
    return ("\r\n".join(lines)).encode("latin1") + body


async def handle_http_client(
    gw: Gateway, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
) -> None:
    try:
        method, raw_path, headers, body = await _read_http_request(
            reader, gw.config.max_body
        )
    except Exception:
        writer.write(_http_response(400, "Bad Request", [], b"bad request\n"))
        await writer.drain()
        writer.close()
        return

    parsed = urlparse(raw_path)
    path = parsed.path or "/"
    origin = headers.get("origin")
    cors = gw.cors_lines(origin)

    if method == "OPTIONS":
        writer.write(_http_response(204, "No Content", cors, b""))
        await writer.drain()
        writer.close()
        return

    if not gw.origin_ok(origin):
        LOG.info("http rejected origin=%s path=%s", origin, path)
        writer.write(
            _http_response(
                403,
                "Forbidden",
                cors + ["Content-Type: application/json"],
                b'{"error":"origin_not_allowlisted"}',
            )
        )
        await writer.drain()
        writer.close()
        return

    status, resp_body, ctype = 404, b'{"error":"not_found"}', "application/json"

    if method == "POST" and path == "/aether/v1/connect":
        target = None
        if body:
            try:
                target = json.loads(body.decode("utf-8")).get("target")
            except Exception:
                status, resp_body, ctype = (
                    400,
                    b'{"error":"invalid_json"}',
                    "application/json",
                )
                target = False  # sentinel
        if target is not False:
            status, resp_body, ctype = await gw.connect(target)
    elif (
        method == "POST"
        and path.startswith("/aether/v1/session/")
        and path.endswith("/send")
    ):
        sid = path[len("/aether/v1/session/") : -len("/send")]
        if len(body) > gw.config.max_body:
            status, resp_body, ctype = (
                413,
                b'{"error":"body_too_large"}',
                "application/json",
            )
        else:
            status, resp_body, ctype = await gw.send(sid, body)
    elif (
        method == "GET"
        and path.startswith("/aether/v1/session/")
        and path.endswith("/receive")
    ):
        sid = path[len("/aether/v1/session/") : -len("/receive")]
        status, resp_body, ctype = await gw.receive(sid)
    elif (
        method == "POST"
        and path.startswith("/aether/v1/session/")
        and path.endswith("/close")
    ):
        sid = path[len("/aether/v1/session/") : -len("/close")]
        status, resp_body, ctype = await gw.close_session(sid)

    reason = {
        200: "OK",
        204: "No Content",
        400: "Bad Request",
        403: "Forbidden",
        404: "Not Found",
        413: "Payload Too Large",
        429: "Too Many Requests",
        502: "Bad Gateway",
    }.get(status, "OK")
    out_headers = cors + [f"Content-Type: {ctype}"]
    writer.write(_http_response(status, reason, out_headers, resp_body))
    await writer.drain()
    writer.close()
    try:
        await writer.wait_closed()
    except Exception:
        pass


async def ws_handler(gw: Gateway, connection) -> None:
    origin = connection.request.headers.get("Origin")
    if not gw.origin_ok(origin):
        LOG.info("ws rejected origin=%s", origin)
        await connection.close(1008, "origin_not_allowlisted")
        return

    parsed = urlparse(connection.request.path)
    if parsed.path != "/aether/v1/ws":
        await connection.close(1008, "unsupported path")
        return

    qs = parse_qs(parsed.query)
    target = (qs.get("target") or [gw.config.default_target])[0]
    if target is None or target not in gw.config.targets:
        await connection.close(1008, "destination_not_allowlisted")
        return

    host, port = gw.config.targets[target]
    try:
        reader, writer = await asyncio.open_connection(host, port)
    except OSError:
        await connection.close(1011, "upstream_unavailable")
        return

    LOG.info("ws open target=%s", target)

    async def tcp_to_ws() -> None:
        try:
            while True:
                data = await reader.read(65536)
                if not data:
                    break
                await connection.send(data)
        except Exception:
            pass
        finally:
            try:
                await connection.close()
            except Exception:
                pass

    async def ws_to_tcp() -> None:
        try:
            async for message in connection:
                if isinstance(message, str):
                    await connection.close(1003, "binary_only")
                    return
                if len(message) > gw.config.max_body:
                    await connection.close(1009, "message_too_big")
                    return
                writer.write(message)
                await writer.drain()
        except Exception:
            pass
        finally:
            writer.close()

    t1 = asyncio.create_task(tcp_to_ws())
    t2 = asyncio.create_task(ws_to_tcp())
    _, pending = await asyncio.wait({t1, t2}, return_when=asyncio.FIRST_COMPLETED)
    for t in pending:
        t.cancel()
    try:
        writer.close()
        await writer.wait_closed()
    except Exception:
        pass
    LOG.info("ws close target=%s", target)


def _make_ssl(cfg: Config) -> Optional[ssl.SSLContext]:
    if not (cfg.tls_cert and cfg.tls_key):
        return None
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cfg.tls_cert, cfg.tls_key)
    return ctx


async def amain(argv: Optional[List[str]] = None) -> None:
    logging.basicConfig(
        level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s"
    )
    cfg = build_config(argv)
    if not cfg.origins:
        LOG.error("No --origin / AE_GW_ORIGINS configured; refusing to start")
        raise SystemExit(2)
    if not cfg.targets:
        LOG.error("No --target / AE_GW_TARGETS configured; refusing to start")
        raise SystemExit(2)

    gw = Gateway(cfg)
    await gw.start()
    ssl_ctx = _make_ssl(cfg)

    async def http_client(reader, writer):
        await handle_http_client(gw, reader, writer)

    http_server = await asyncio.start_server(
        http_client, cfg.listen_host, cfg.listen_port, ssl=ssl_ctx
    )

    async def ws_client(connection):
        await ws_handler(gw, connection)

    LOG.info(
        "TEST-ONLY gateway HTTP %s:%s WS %s:%s tls=%s targets=%s origins=%d",
        cfg.listen_host,
        cfg.listen_port,
        cfg.ws_host,
        cfg.ws_port,
        bool(ssl_ctx),
        list(cfg.targets.keys()),
        len(cfg.origins),
    )

    async with http_server, ws_serve(
        ws_client,
        cfg.ws_host,
        cfg.ws_port,
        ssl=ssl_ctx,
        max_size=cfg.max_body,
        # Origin checked in handler; process_request not required.
    ):
        await asyncio.Future()


def main() -> None:
    try:
        asyncio.run(amain())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
