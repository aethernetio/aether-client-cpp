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

#ifndef AETHER_CONFIG_H_
#define AETHER_CONFIG_H_

#include "aether/config_consts.h"
#if defined USER_CONFIG
#  include USER_CONFIG
#endif

#ifndef CM_PLATFORM

#  ifndef AE_SUPPORT_IPV4
#    define AE_SUPPORT_IPV4 1
#  endif  // AE_SUPPORT_IPV4

#  ifndef AE_SUPPORT_IPV6
#    define AE_SUPPORT_IPV6 1
#  endif  // AE_SUPPORT_IPV6

#  ifndef AE_SUPPORT_UDP
#    define AE_SUPPORT_UDP 1
#  endif  // AE_SUPPORT_UDP

#  ifndef AE_SUPPORT_TCP
#    define AE_SUPPORT_TCP 1
#  endif  // AE_SUPPORT_TCP

#endif /* CM_PLATFORM */

#ifndef AE_SUPPORT_WEBSOCKET
#  define AE_SUPPORT_WEBSOCKET 1
#endif  // AE_SUPPORT_WEBSOCKET

// Default
#ifndef AE_SUPPORT_HTTP
#  define AE_SUPPORT_HTTP 1
#  ifndef AE_SUPPORT_HTTP_OVER_TCP
#    define AE_SUPPORT_HTTP_OVER_TCP 1
#  endif  // AE_SUPPORT_HTTP_OVER_TCP
#  ifndef AE_SUPPORT_HTTP_WINHTTP
#    define AE_SUPPORT_HTTP_WINHTTP 1
#  endif  // AE_SUPPORT_HTTP_WINHTTP
#endif    // AE_SUPPORT_HTTP

#ifndef AE_SUPPORT_HTTPS
#  define AE_SUPPORT_HTTPS 1
#endif  // AE_SUPPORT_HTTPS

#ifndef AE_SUPPORT_PROXY
#  define AE_SUPPORT_PROXY 1
// A proxy can be constructed and initialized with any values at run-time
#  ifndef AE_SUPPORT_DYNAMIC_PROXY
#    define AE_SUPPORT_DYNAMIC_PROXY 1
#  endif  // AE_SUPPORT_DYNAMIC_PROXY
#endif    // AE_SUPPORT_PROXY

// Domain names are specified for finding a cloud server if all stored IPs are
// refusing connection.
#ifndef AE_SUPPORT_CLOUD_DNS
#  define AE_SUPPORT_CLOUD_DNS 1
// A domain name can be added and initialized with any values at run-time
#  ifndef AE_SUPPORT_DYNAMIC_CLOUD_DNS
#    define AE_SUPPORT_DYNAMIC_CLOUD_DNS 1
#  endif  // AE_SUPPORT_DYNAMIC_CLOUD_DNS
#endif    // AE_SUPPORT_CLOUD_DNS

// Cloud IPs are specified and store.
#ifndef AE_SUPPORT_CLOUD_IPS
#  define AE_SUPPORT_CLOUD_IPS 1
// IPs of aether servers' cloud are stored in state.
#  ifndef AE_SUPPORT_DYNAMIC_CLOUD_IPS
#    define AE_SUPPORT_DYNAMIC_CLOUD_IPS 1
#  endif  // AE_SUPPORT_DYNAMIC_CLOUD_IPS
#endif    // AE_SUPPORT_CLOUD_IPS

// Registration functionality can be stripped-out for pre-registered clients.
#ifndef AE_SUPPORT_REGISTRATION
#  define AE_SUPPORT_REGISTRATION 1
#endif  // AE_SUPPORT_REGISTRATION

#if AE_SUPPORT_REGISTRATION == 1
// TODO: this options are not used
// IP of registration servers can be added at run-time
#  ifndef AE_SUPPORT_REGISTRATION_DYNAMIC_IP
#    define AE_SUPPORT_REGISTRATION_DYNAMIC_IP 1
#  endif  // AE_SUPPORT_REGISTRATION_DYNAMIC_IP

// Proof of work methods
#  ifndef AE_POW
#    define AE_POW AE_BCRYPT_CRC32
#  endif

// Signature
#  ifndef AE_SIGNATURE
#    define AE_SIGNATURE AE_ED25519
#  endif  // AE_SIGNATURE

#else  // AE_SUPPORT_REGISTRATION == 1
#  undef AE_POW
#  define AE_POW AE_NONE
#  undef AE_SIGNATURE
#  define AE_SIGNATURE AE_NONE
#endif  // AE_SUPPORT_REGISTRATION == 1

// Public key cryptography
#ifndef AE_CRYPTO_ASYNC
#  define AE_CRYPTO_ASYNC AE_SODIUM_BOX_SEAL
#endif  // AE_CRYPTO_ASYNC

// Secret key cryptography
#ifndef AE_CRYPTO_SYNC
#  define AE_CRYPTO_SYNC AE_CHACHA20_POLY1305
#endif  // AE_CRYPTO_SYNC

// Key derivation function
#ifndef AE_KDF
#  define AE_KDF AE_SODIUM_KDF
#endif  // AE_KDF

// Cryptographic hash
#ifndef AE_CRYPTO_HASH
#  define AE_CRYPTO_HASH AE_BLAKE2B
#endif  // AE_CRYPTO_HASH

#ifndef AE_TARGET_ENDIANNESS
#  define AE_TARGET_ENDIANNESS AE_LITTLE_ENDIAN
#endif  // AE_TARGET_ENDIANNESS

// Telemetry configuration
// Compilation info
// Environment info
// Metrics (invocations count is a minimum)
//  - duration min/avg/max
// Logs (entry_id is a minimum)
//  - start time
//  - location
//  - name
//  - level and module
//  - blob || formatted message
// Where and how to store and send

#ifndef AE_TELE_ENABLED
#  define AE_TELE_ENABLED 1
#endif  // AE_TELE_ENABLED

// Environment info
#ifndef AE_TELE_COMPILATION_INFO
#  define AE_TELE_COMPILATION_INFO 1
#endif  // AE_TELE_COMPILATION_INFO

#ifndef AE_TELE_RUNTIME_INFO
#  define AE_TELE_RUNTIME_INFO AE_NONE
#endif  // AE_TELE_RUNTIME_INFO

// Logs, telemetry, trace
#ifndef AE_TELE_METRICS_MODULES
#  define AE_TELE_METRICS_MODULES AE_TELE_MODULES_ALL
#endif  // AE_TELE_METRICS_MODULES

#ifndef AE_TELE_METRICS_DURATION
#  define AE_TELE_METRICS_DURATION AE_TELE_METRICS_MODULES
#endif  // AE_TELE_METRICS_DURATION

/**
 * \brief Mask for enabling generate telemetry for modules.
 * \see aether/tele/modules.h for a full list module list
 * \example #define AE_TELE_LOG_MODULES 0x00000005 //< enables only
 * kApp and kRegister module
 */
#ifndef AE_TELE_LOG_MODULES
#  define AE_TELE_LOG_MODULES AE_TELE_MODULES_ALL
#endif  // AE_TELE_LOG_MODULES

/**
 * \brief Mask for enabling generate telemetry for levels.
 * \see aether/tele/levels.h for a full list level list
 * \example #define AE_TELE_LOG_LEVELS 0x00000007 //< enables only
 * info, warning and error levels
 */
#ifndef AE_TELE_LOG_LEVELS
#  define AE_TELE_LOG_LEVELS AE_TELE_LEVELS_ALL
#endif  // AE_TELE_LOG_LEVELS

// enable to log telemetry time point
#ifndef AE_TELE_LOG_TIME_POINT
#  define AE_TELE_LOG_TIME_POINT AE_TELE_LOG_MODULES
#endif  // AE_TELE_LOG_TIME_POINT

// enable to log telemetry file location
#ifndef AE_TELE_LOG_LOCATION
#  define AE_TELE_LOG_LOCATION AE_TELE_LOG_MODULES
#endif  // AE_TELE_LOG_LOCATION

// enable to log telemetry tag name
#ifndef AE_TELE_LOG_NAME
#  define AE_TELE_LOG_NAME AE_TELE_LOG_MODULES
#endif  // AE_TELE_LOG_NAME

// enable to log telemetry module and level
#ifndef AE_TELE_LOG_LEVEL_MODULE
#  define AE_TELE_LOG_LEVEL_MODULE AE_TELE_LOG_MODULES
#endif  // AE_TELE_LOG_LEVEL_MODULE

// enable to log telemetry message or additional data
#ifndef AE_TELE_LOG_BLOB
#  define AE_TELE_LOG_BLOB AE_TELE_LOG_MODULES
#endif  // AE_TELE_LOG_BLOB

// enable to log telemetry to console
#ifndef AE_TELE_LOG_CONSOLE
#  define AE_TELE_LOG_CONSOLE 1
#endif  // AE_TELE_LOG_CONSOLE

#endif  // AETHER_CONFIG_H_