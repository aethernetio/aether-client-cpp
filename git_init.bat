@echo off
REM Copyright 2024 Aethernet Inc.
REM
REM Licensed under the Apache License, Version 2.0 (the "License");
REM you may not use this file except in compliance with the License.
REM You may obtain a copy of the License at
REM
REM     http://www.apache.org/licenses/LICENSE-2.0
REM
REM Unless required by applicable law or agreed to in writing, software
REM distributed under the License is distributed on an "AS IS" BASIS,
REM WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
REM See the License for the specific language governing permissions and
REM limitations under the License.
@echo on

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
copy "CMakeLists.libbcrypt" "libbcrypt/CMakeLists.txt"
copy "CMakeLists.libhydrogen" "libhydrogen/CMakeLists.txt"
copy "CMakeLists.libsodium" "libsodium/CMakeLists.txt"
cd ../
