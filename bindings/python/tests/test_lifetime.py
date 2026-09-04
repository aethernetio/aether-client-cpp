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

"""Lifetime regression coverage for the Python wrapper and C API ownership."""

from aethernetio import Runtime
from aethernetio import _native


def test_close_is_idempotent():
    runtime = Runtime()
    runtime.close()
    runtime.close()
    runtime.close()


def test_operations_fail_after_close():
    runtime = Runtime()
    runtime.close()
    try:
        runtime.select_client(
            local_id="closed-client",
            parent_uid="3ac93165-3d37-4970-87a6-fa4ee27744e4",
            timeout=1,
        )
        raised = False
    except RuntimeError:
        raised = True
    assert raised


def test_client_config_id_owned_after_select_client():
    """Native probe: external ClientConfig/id may be destroyed after SelectClient."""
    with Runtime() as runtime:
        runtime._native.probe_config_lifetime()


def test_callback_after_close_does_not_resurrect_runtime():
    runtime = Runtime()
    runtime.close()
    assert _native.is_runtime_active() is False
    # Creating a new runtime after close must succeed.
    with Runtime() as runtime2:
        assert _native.is_runtime_active() is True
    assert _native.is_runtime_active() is False
