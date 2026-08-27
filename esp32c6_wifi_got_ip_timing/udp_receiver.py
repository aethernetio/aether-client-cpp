#!/usr/bin/env python3
"""Minimal UDP receiver for ESP32-C6 Wi-Fi GOT_IP timing probe."""

from __future__ import annotations

import socket
import time
from datetime import datetime, timezone

HOST = "0.0.0.0"
PORT = 3333


def main() -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((HOST, PORT))
    print(f"UDP listening on {HOST}:{PORT}", flush=True)

    while True:
        data, addr = sock.recvfrom(2048)
        now = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        text = data.decode("utf-8", errors="replace").strip()
        print(f"{now} from={addr[0]}:{addr[1]} {text}", flush=True)


if __name__ == "__main__":
    main()
