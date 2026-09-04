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

"""Public Python API for the Æther Windows x64 client SDK."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Union
from uuid import UUID

from aethernetio import _native

__all__ = ["Client", "Message", "Runtime", "__version__"]

__version__ = str(_native.__version__)

ParentUid = Union[UUID, str]
Destination = Union[UUID, str]


@dataclass(frozen=True)
class Message:
    sender: UUID
    data: bytes


class Client:
    """Selected Æther client bound to a Runtime."""

    def __init__(self, runtime: "Runtime", state: "_native.ClientState") -> None:
        self._runtime = runtime
        self._state = state

    @property
    def uid(self) -> UUID:
        return self._state.uid

    @property
    def local_id(self) -> str:
        return self._state.local_id

    def send(
        self,
        destination: Destination,
        data: bytes,
        timeout: float = 60.0,
    ) -> None:
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise TypeError("data must be a bytes-like object")
        payload = bytes(data)
        self._runtime._ensure_open()
        self._runtime._native.send(self._state, destination, payload, timeout)

    def receive(self, timeout: Optional[float] = None) -> Message:
        self._runtime._ensure_open()
        result = self._runtime._native.receive(self._state, timeout)
        return Message(sender=result["sender"], data=result["data"])


class Runtime:
    """Process-wide Æther runtime (one active instance allowed)."""

    def __init__(self) -> None:
        self._native = _native.NativeRuntime()
        self._closed = False

    def __enter__(self) -> "Runtime":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def _ensure_open(self) -> None:
        if self._closed:
            raise RuntimeError("Runtime is closed")

    def select_client(
        self,
        local_id: str,
        parent_uid: ParentUid,
        timeout: float = 180.0,
    ) -> Client:
        self._ensure_open()
        if not isinstance(local_id, str) or not local_id:
            raise ValueError("local_id must be a non-empty string")
        state = self._native.select_client(local_id, parent_uid, timeout)
        return Client(self, state)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._native.close()
