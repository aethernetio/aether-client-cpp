import asyncio
import socket
import struct
import unittest
from udp_fault_proxy import UdpFaultProxy


class Journal:
    def log(self, event, **values):
        return dict(event=event, **values)


class RelayTest(unittest.IsolatedAsyncioTestCase):
    async def test_bytes_and_bidirectional_loss_recover_without_reconnect(self):
        loop = asyncio.get_running_loop()
        inbox = asyncio.Queue()
        class Echo(asyncio.DatagramProtocol):
            def connection_made(self, transport): self.transport = transport
            def datagram_received(self, data, addr):
                self.peer = addr
                self.transport.sendto(data, addr)
        class Client(asyncio.DatagramProtocol):
            def datagram_received(self, data, addr): inbox.put_nowait(data)
        server, echo = await loop.create_datagram_endpoint(Echo, local_addr=('127.0.0.1', 0))
        proxy = await UdpFaultProxy('test', Journal()).start()
        client, _ = await loop.create_datagram_endpoint(Client, local_addr=('127.0.0.1', 0))
        try:
            header = socket.inet_aton('127.0.0.1') + struct.pack('!H', server.get_extra_info('sockname')[1])
            client.sendto(header, ('127.0.0.1', proxy.port))
            # Payload immediately after the header exercises asynchronous bind buffering.
            payload = bytes(range(256)) * 4
            client.sendto(payload, ('127.0.0.1', proxy.port))
            self.assertEqual(await asyncio.wait_for(inbox.get(), 2), payload)
            proxy.block(True)
            client.sendto(b'outbound loss', ('127.0.0.1', proxy.port))
            server.sendto(b'inbound loss', echo.peer)
            with self.assertRaises(asyncio.TimeoutError):
                await asyncio.wait_for(inbox.get(), .1)
            self.assertEqual(proxy.dropped, {'out': 1, 'in': 1})
            proxy.block(False)
            client.sendto(b'recovered', ('127.0.0.1', proxy.port))
            self.assertEqual(await asyncio.wait_for(inbox.get(), 2), b'recovered')
            self.assertEqual(proxy.connections, 1)
        finally:
            client.close(); server.close(); await proxy.close()
