"""Loopback RFC 6455 fixture for the real Apple backend; only stdlib + openssl.

No certificates are installed or trusted. The TLS listener deliberately uses
an untrusted certificate to check that system verification rejects it.
"""

import base64
import hashlib
from pathlib import Path
import socket
import socketserver
import ssl
import struct
import subprocess
import sys
import tempfile
import threading


def read_exact(stream, count):
    data = stream.read(count)
    if len(data) != count:
        raise EOFError
    return data


def read_frame(stream):
    first, second = read_exact(stream, 2)
    size = second & 127
    if size == 126:
        size = struct.unpack("!H", read_exact(stream, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", read_exact(stream, 8))[0]
    assert second & 128, "Client WebSocket frames must be masked"
    assert size <= 8 * 1024 * 1024
    mask = read_exact(stream, 4)
    data = read_exact(stream, size)
    return first & 15, bytes(value ^ mask[i % 4] for i, value in enumerate(data))


def frame(opcode, payload=b"", final=True):
    size = len(payload)
    header = bytes([(128 if final else 0) | opcode])
    if size < 126:
        header += bytes([size])
    elif size < 65536:
        header += b"\x7e" + struct.pack("!H", size)
    else:
        header += b"\x7f" + struct.pack("!Q", size)
    return header + payload


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        self.connection.settimeout(10)
        try:
            self.websocket()
        except (EOFError, OSError):
            pass  # Deliberate cancellation/disconnect is part of these tests.

    def websocket(self):
        request = self.rfile.readline().decode("ascii").split()
        if not request:
            return
        path = request[1]
        headers = {}
        while True:
            line = self.rfile.readline()
            if line == b"\r\n":
                break
            if not line:
                raise EOFError
            key, value = line.decode("ascii").split(":", 1)
            headers[key.lower()] = value.strip()
        if path == "/reject":
            self.wfile.write(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")
            return
        accept = base64.b64encode(hashlib.sha1(
            (headers["sec-websocket-key"] +
             "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
        ).digest())
        self.wfile.write(b"HTTP/1.1 101 Switching Protocols\r\n"
                         b"Upgrade: websocket\r\nConnection: Upgrade\r\n"
                         b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")
        opcode, data = read_frame(self.rfile)
        if opcode == 8:
            return
        assert opcode == 2, "Aether must send binary messages"
        if path == "/fragment":
            self.wfile.write(frame(2, b"\x02", False) +
                             frame(0, b"\x03\x01\x02", False) +
                             frame(0, b"\x04\x05"))
        elif path == "/ping-close":
            self.wfile.write(frame(9, b"ping"))
            assert read_frame(self.rfile) == (10, b"ping")
            self.wfile.write(frame(2, b"\x02\x03\x01") +
                             frame(8, struct.pack("!H", 1000)))
            try:
                opcode, _ = read_frame(self.rfile)
            except (EOFError, OSError) as error:
                raise AssertionError("Peer close was not acknowledged") from error
            assert opcode == 8, "Expected a Close response"
            self.server.close_reply.set()
            return
        elif path == "/text":
            self.wfile.write(frame(1, b"not a binary Aether packet"))
        elif path == "/oversize":
            self.wfile.write(frame(2, b"x" * (8 * 1024 * 1024 + 1)))
        else:
            self.wfile.write(frame(2, data))
        # Keep the connection alive until the client closes/cancels it.
        while True:
            opcode, data = read_frame(self.rfile)
            if opcode == 8:
                self.wfile.write(frame(8, data))
                return


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, handler):
        self.failed = threading.Event()
        self.close_reply = threading.Event()
        super().__init__(address, handler)

    def handle_error(self, request, client_address):
        self.failed.set()
        super().handle_error(request, client_address)


class UntrustedTlsServer(Server):
    def get_request(self):
        connection, address = super().get_request()
        connection.settimeout(10)
        try:
            return self.tls.wrap_socket(connection, server_side=True), address
        except OSError:
            connection.close()
            raise


def main():
    with tempfile.TemporaryDirectory(prefix="aether-ws-test-") as directory:
        root = Path(directory)
        config = root / "openssl.cnf"
        config.write_text("[req]\ndistinguished_name=dn\nx509_extensions=ext\n"
                          "prompt=no\n[dn]\nCN=localhost\n[ext]\n"
                          "subjectAltName=DNS:localhost,IP:127.0.0.1\n")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048",
                        "-nodes", "-days", "1", "-config", str(config),
                        "-keyout", str(root / "key.pem"),
                        "-out", str(root / "cert.pem")], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with socket.socket() as refused, \
                Server(("127.0.0.1", 0), Handler) as plain, \
                UntrustedTlsServer(("127.0.0.1", 0), Handler) as tls:
            # Use a closed port. On Darwin a bound socket that never listens
            # silently drops SYNs instead of refusing the connection.
            refused.bind(("127.0.0.1", 0))
            refused_address = refused.getsockname()
            refused.close()
            tls.tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            tls.tls.load_cert_chain(root / "cert.pem", root / "key.pem")
            for server in (plain, tls):
                threading.Thread(target=server.serve_forever, daemon=True).start()
            try:
                result = subprocess.run(
                    [sys.argv[1], f"ws://127.0.0.1:{plain.server_address[1]}",
                     f"wss://127.0.0.1:{tls.server_address[1]}/",
                     f"ws://127.0.0.1:{refused_address[1]}/"], timeout=75)
                if result.returncode == 0:
                    if not plain.close_reply.wait(2):
                        print("Missing WebSocket Close response", file=sys.stderr)
                        return 1
                    if plain.failed.is_set() or tls.failed.is_set():
                        return 1
                return result.returncode
            finally:
                plain.shutdown()
                tls.shutdown()


if __name__ == "__main__":
    sys.exit(main())
