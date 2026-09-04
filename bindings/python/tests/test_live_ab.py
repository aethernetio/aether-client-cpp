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

"""Live cloud A/B validation for the Windows Python SDK.

Requires network access to the Æther registration cloud. Not proof of delivery
by itself until Write message / NewMessage paths succeed end-to-end.
"""

from __future__ import annotations

import time
import uuid

import pytest

from aethernetio import Runtime

PARENT_UID = "3ac93165-3d37-4970-87a6-fa4ee27744e4"
MESSAGE_COUNT = 10
# Cloud RX windows are scheduled around ping intervals; give delivery time.
INTER_MESSAGE_PAUSE_SEC = 7.0
SEND_TIMEOUT_SEC = 120.0
RECV_TIMEOUT_SEC = 120.0


def _payload(i: int, tag: bytes) -> bytes:
    # Includes NULs, non-UTF-8 bytes, and length beyond a short text string.
    return (
        tag
        + bytes([0x00, 0xFF, 0x80, 0xFE])
        + f"-{i:04d}-".encode("ascii")
        + (b"\x00BIN" * 32)
    )


@pytest.mark.live
def test_live_ab_bidirectional():
    zero = uuid.UUID(int=0)
    sent_ab = 0
    sent_ba = 0
    recv_ab = 0
    recv_ba = 0
    send_success = 0

    with Runtime() as runtime:
        alice = runtime.select_client(
            local_id=f"python-alice-{uuid.uuid4().hex[:8]}",
            parent_uid=PARENT_UID,
            timeout=180,
        )
        bob = runtime.select_client(
            local_id=f"python-bob-{uuid.uuid4().hex[:8]}",
            parent_uid=PARENT_UID,
            timeout=180,
        )

        assert alice.uid != zero
        assert bob.uid != zero
        assert alice.uid != bob.uid

        for i in range(MESSAGE_COUNT):
            data = _payload(i, b"A2B")
            alice.send(bob.uid, data, timeout=SEND_TIMEOUT_SEC)
            send_success += 1
            sent_ab += 1
            message = bob.receive(timeout=RECV_TIMEOUT_SEC)
            assert message.sender == alice.uid
            assert message.data == data
            recv_ab += 1
            time.sleep(INTER_MESSAGE_PAUSE_SEC)

        for i in range(MESSAGE_COUNT):
            data = _payload(i, b"B2A")
            bob.send(alice.uid, data, timeout=SEND_TIMEOUT_SEC)
            send_success += 1
            sent_ba += 1
            message = alice.receive(timeout=RECV_TIMEOUT_SEC)
            assert message.sender == bob.uid
            assert message.data == data
            recv_ba += 1
            time.sleep(INTER_MESSAGE_PAUSE_SEC)

    assert sent_ab == MESSAGE_COUNT
    assert sent_ba == MESSAGE_COUNT
    assert recv_ab == MESSAGE_COUNT
    assert recv_ba == MESSAGE_COUNT
    assert send_success == MESSAGE_COUNT * 2

    print(
        f"LIVE_PASS sent_ab={sent_ab} recv_ab={recv_ab} "
        f"sent_ba={sent_ba} recv_ba={recv_ba} send_success={send_success}"
    )
