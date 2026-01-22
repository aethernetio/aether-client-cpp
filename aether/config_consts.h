/*
 * Copyright 2024 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AETHER_CONFIG_CONSTS_H_
#define AETHER_CONFIG_CONSTS_H_

#define AE_NONE 0

// AE_POW, Proof of work methods
#define AE_BCRYPT_CRC32 1

// AE_SIGNATURE, Signature
#define AE_ED25519 1
#define AE_HYDRO_SIGNATURE 2

// AE_CRYPTO_ASYNC, Asynchronous key cryptography
#define AE_SODIUM_BOX_SEAL 1
#define AE_HYDRO_CRYPTO_PK 2

// AE_CRYPTO_SYNC, Synchronous key cryptography
#define AE_CHACHA20_POLY1305 1
#define AE_HYDRO_CRYPTO_SK 2

// AE_KDF, Key derivation function
#define AE_SODIUM_KDF 1
#define AE_HYDRO_KDF 2

// AE_CRYPTO_HASH, Cryptographic hash
#define AE_BLAKE2B 1
#define AE_HYDRO_HASH 2

#define AE_LITTLE_ENDIAN 1
#define AE_BIG_ENDIAN 2

#define LWIP_CB_SOCKETS 1
#define LWIP_BSD_SOCKETS 2

#define AE_ALL 0xffffffff
#define AE_TELE_LEVELS_ALL AE_ALL
#define AE_EMPTY_LIST \
  {                   \
  }

#define AE_LOG_MODULE 0xfffffffe

// ESP32 WiFi power save constants
#define AE_WIFI_PS_NONE 0  // No power save
#define AE_WIFI_PS_MIN_MODEM \
  1  // Minimum modem power saving. In this mode, station wakes up to receive
     // beacon every DTIM period
#define AE_WIFI_PS_MAX_MODEM \
  2  // Maximum modem power saving. In this mode, interval to receive beacons
     // is determined by the listen_interval parameter in wifi_sta_config_t

// ESP32 WiFi protocol constants
#define AE_WIFI_PROTOCOL_11B 0x1    // 802.11b protocol
#define AE_WIFI_PROTOCOL_11G 0x2    // 802.11g protocol
#define AE_WIFI_PROTOCOL_11N 0x4    // 802.11n protocol
#define AE_WIFI_PROTOCOL_LR 0x8     // Low Rate protocol
#define AE_WIFI_PROTOCOL_11A 0x10   // 802.11a protocol
#define AE_WIFI_PROTOCOL_11AC 0x20  // 802.11ac protocol
#define AE_WIFI_PROTOCOL_11AX 0x40  // 802.11ax protocol

#endif  // AETHER_CONFIG_CONSTS_H_
