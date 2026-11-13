"""Private per-process UDP relay. Closed gates discard real datagrams."""
import asyncio
import socket
import struct


class UdpFaultProxy(asyncio.DatagramProtocol):
    def __init__(self, side, journal):
        self.side, self.journal = side, journal
        self.gate = asyncio.Event()
        self.gate.set()
        self.bytes = {"out": 0, "in": 0}
        self.dropped = {"out": 0, "in": 0}
        self.connections = 0
        self.clients = {}
        self.tasks = set()
        self.transport = None

    async def start(self):
        loop = asyncio.get_running_loop()
        self.transport, _ = await loop.create_datagram_endpoint(
            lambda: self, local_addr=("127.0.0.1", 0))
        self.port = self.transport.get_extra_info("sockname")[1]
        self.journal.log("proxy_start", side=self.side, port=self.port, transport="udp")
        return self

    def block(self, blocked, **metadata):
        self.gate.clear() if blocked else self.gate.set()
        return self.journal.log("cut" if blocked else "restore", side=self.side,
                                forwarded=dict(self.bytes), dropped=dict(self.dropped), **metadata)

    def datagram_received(self, data, address):
        if address not in self.clients:
            if len(data) != 6:
                self.journal.log("invalid_relay_header", side=self.side, size=len(data))
                return
            endpoint = socket.inet_ntoa(data[:4]), struct.unpack("!H", data[4:])[0]
            if endpoint[1] == 0:
                return
            self.clients[address] = {"transport": None, "pending": []}
            self.connections += 1
            task = asyncio.create_task(self.connect(address, endpoint))
            self.tasks.add(task)
            task.add_done_callback(self.tasks.discard)
            return
        client = self.clients[address]
        if not self.gate.is_set():
            self.dropped["out"] += 1
        elif client["transport"] is not None:
            client["transport"].sendto(data)
            self.bytes["out"] += len(data)
        elif len(client["pending"]) < 256:
            client["pending"].append(data)

    async def connect(self, address, endpoint):
        owner = self
        class Remote(asyncio.DatagramProtocol):
            def datagram_received(self, data, _):
                if owner.gate.is_set():
                    owner.transport.sendto(data, address)
                    owner.bytes["in"] += len(data)
                else:
                    owner.dropped["in"] += 1
            def error_received(self, error):
                owner.journal.log("proxy_error", side=owner.side, error=repr(error))
        try:
            remote, _ = await asyncio.get_running_loop().create_datagram_endpoint(
                Remote, remote_addr=endpoint)
            client = self.clients[address]
            client["transport"] = remote
            self.journal.log("connect", side=self.side, host=endpoint[0], port=endpoint[1], transport="udp")
            for data in client["pending"]:
                if self.gate.is_set():
                    remote.sendto(data); self.bytes["out"] += len(data)
                else:
                    self.dropped["out"] += 1
            client["pending"].clear()
        except OSError as error:
            self.journal.log("proxy_error", side=self.side, error=repr(error))

    async def close(self):
        for task in list(self.tasks):
            task.cancel()
        await asyncio.gather(*list(self.tasks), return_exceptions=True)
        for client in self.clients.values():
            if client["transport"]:
                client["transport"].close()
        if self.transport:
            self.transport.close()
