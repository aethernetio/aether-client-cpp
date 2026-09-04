# Æther Python client (Windows x64)

Windows 10/11 **x86-64** only. Supported CPython: **3.11**, **3.12**, **3.13**,
and **3.14** when the build toolchain supports it.

## Install

```text
py -m pip install aethernetio-client
```

Local wheel (before PyPI publication):

```text
py -m pip install dist\aethernetio_client-*.whl
```

## Minimal A/B example

```python
from aethernetio import Runtime

with Runtime() as runtime:
    alice = runtime.select_client(
        local_id="python-alice",
        parent_uid="3ac93165-3d37-4970-87a6-fa4ee27744e4",
        timeout=180,
    )
    bob = runtime.select_client(
        local_id="python-bob",
        parent_uid="3ac93165-3d37-4970-87a6-fa4ee27744e4",
        timeout=180,
    )
    alice.send(bob.uid, b"hello", timeout=60)
    message = bob.receive(timeout=60)
    assert message.sender == alice.uid
    assert message.data == b"hello"
```

## v0.1 limits

- Storage is **RAM-only**. Client UIDs are **not** persisted across process
  restarts; each run registers fresh clients.
- Only **one** `Runtime` may be active per process.
- Linux, macOS, ARM64, asyncio, presence, and profile persistence are not
  included in this release.
