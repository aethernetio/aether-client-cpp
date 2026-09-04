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

from aethernetio import Runtime, __version__


def test_import_version():
    assert isinstance(__version__, str)
    assert __version__


def test_runtime_context_manager():
    with Runtime() as runtime:
        assert runtime is not None
    # Idempotent close
    runtime.close()
    runtime.close()


def test_single_runtime_enforced():
    with Runtime() as runtime:
        try:
            Runtime()
            raised = False
        except RuntimeError:
            raised = True
        assert raised
    # After close, a new runtime may be created.
    with Runtime():
        pass
