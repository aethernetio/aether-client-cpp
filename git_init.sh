#!/bin/bash
# Copyright 2024 Aethernet Inc.
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

# This is the GIT initialization script.\


git submodule update --force --recursive --init --remote
git submodule foreach --recursive git reset --hard
git submodule update --init --recursive
cd third_party/libbcrypt
git apply "../libbcrypt.patch"
cd ../libhydrogen
git apply "../libhydrogen.patch"
cd ../libsodium
git apply "../libsodium.patch"
cd ../etl
git apply "../etl.patch"
cd ../
cp "CMakeLists.libbcrypt" "libbcrypt/CMakeLists.txt"
cp "CMakeLists.libhydrogen" "libhydrogen/CMakeLists.txt"
cp "CMakeLists.libsodium" "libsodium/CMakeLists.txt"
cp "CMakeLists.gcem" "gcem/CMakeLists.txt"
cd ../
