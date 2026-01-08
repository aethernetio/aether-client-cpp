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

// IWYU pragma: begin_exports
#include <limits>
#include <cstdint>

#include "aether/config_consts.h"
#if defined USER_CONFIG
#  include USER_CONFIG
#endif
// IWYU pragma: end_exports

#ifndef AE_SUPPORT_IPV4
#  define AE_SUPPORT_IPV4 1
#endif  // AE_SUPPORT_IPV4

#ifndef AE_SUPPORT_IPV6
#  define AE_SUPPORT_IPV6 0
#endif  // AE_SUPPORT_IPV6

#ifndef AE_SUPPORT_UDP
#  define AE_SUPPORT_UDP 1
#endif  // AE_SUPPORT_UDP

#ifndef AE_SUPPORT_TCP
#  define AE_SUPPORT_TCP 1
#endif  // AE_SUPPORT_TCP

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

#ifndef AE_LWIP_SOCKET_TYPES
#  define AE_LWIP_SOCKET_TYPES LWIP_CB_SOCKETS
#endif

/**
 * Is mobile modems supported.
 * Also choose one of the supported modem implementations.
 */
#ifndef AE_SUPPORT_MODEMS
#  define AE_SUPPORT_MODEMS 0
#endif

// Thingy91x modem implementation is enabled.
#ifndef AE_ENABLE_THINGY91X
#  define AE_ENABLE_THINGY91X 0
#endif

// SIM7070 modem implementation is enabled.
#ifndef AE_ENABLE_SIM7070
#  define AE_ENABLE_SIM7070 0
#endif

// BG95 modem implementation is enabled.
#ifndef AE_ENABLE_BG95
#  define AE_ENABLE_BG95 0
#endif

/**
 * \brief Is lora protocol supported.
 */
#ifndef AE_SUPPORT_LORA
#  define AE_SUPPORT_LORA 0
#endif

#ifndef AE_ENABLE_EBYTE_E22_LM
#  define AE_ENABLE_EBYTE_E22_LM 0
#endif

#ifndef AE_ENABLE_DX_SMART_LR02_LM
#  define AE_ENABLE_DX_SMART_LR02_LM 0
#endif

/**
 * \brief Is work with gateway supported.
 */
#ifndef AE_SUPPORT_GATEWAY
#  define AE_SUPPORT_GATEWAY 0
#endif

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

// Send ping interval, ms
#ifndef AE_PING_INTERVAL_MS
#  define AE_PING_INTERVAL_MS 5000
#endif

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

#ifndef AE_SUPPORT_SPIFS_FS
#  define AE_SUPPORT_SPIFS_FS 0
#endif  // AE_SUPPORT_SPIFS_FS

// safe stream sender repeat timeout grow factor
#ifndef AE_SAFE_STREAM_RTO_GROW_FACTOR
#  define AE_SAFE_STREAM_RTO_GROW_FACTOR 1.5
#endif  // AE_SAFE_STREAM_RTO_GROW_FACTOR

// window size for connection statistics
#ifndef AE_STATISTICS_CONNECTION_WINDOW_SIZE
#  define AE_STATISTICS_CONNECTION_WINDOW_SIZE 100
#endif
// default value used for connection timeout, until statistics are available
#ifndef AE_DEFAULT_CONNECTION_TIMEOUT_MS
#  define AE_DEFAULT_CONNECTION_TIMEOUT_MS 50000
#endif

// window size for server answear to ping statistics
#ifndef AE_STATISTICS_RESPONSE_WINDOW_SIZE
#  define AE_STATISTICS_RESPONSE_WINDOW_SIZE 100
#endif

// default value used for ping timeout, until statistics are available
#ifndef AE_DEFAULT_RESPONSE_TIMEOUT_MS
#  define AE_DEFAULT_RESPONSE_TIMEOUT_MS 10000
#endif

// window size for safe stream response time statistics
#ifndef AE_STATISTICS_SAFE_STREAM_WINDOW_SIZE
#  define AE_STATISTICS_SAFE_STREAM_WINDOW_SIZE 100
#endif

// the default timeout for establishing a wifi connection
#ifndef AE_WIFI_CONNECTION_TIMEOUT_MS
#  define AE_WIFI_CONNECTION_TIMEOUT_MS 20000
#endif

// default value for connection modem
#ifndef AE_MODEM_CONNECTION_TIMEOUT_MS
#  define AE_MODEM_CONNECTION_TIMEOUT_MS 15000
#endif

// Maximal number of server connections
#ifndef AE_CLOUD_MAX_SERVER_CONNECTIONS
#  define AE_CLOUD_MAX_SERVER_CONNECTIONS 3
#endif

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
#  define AE_TELE_METRICS_MODULES AE_ALL
#endif  // AE_TELE_METRICS_MODULES

#ifndef AE_TELE_METRICS_MODULES_EXCLUDE
#  define AE_TELE_METRICS_MODULES_EXCLUDE AE_EMPTY_LIST
#endif

#ifndef AE_TELE_METRICS_DURATION
#  define AE_TELE_METRICS_DURATION AE_TELE_METRICS_MODULES
#endif  // AE_TELE_METRICS_DURATION

#ifndef AE_TELE_METRICS_DURATION_EXCLUDE
#  define AE_TELE_METRICS_DURATION_EXCLUDE AE_EMPTY_LIST
#endif

/**
 * \brief Mask for enabling generate telemetry for modules.
 * This must be either AE_ALL or uint32_t array of enabled modules
 */
#ifndef AE_TELE_LOG_MODULES
#  define AE_TELE_LOG_MODULES AE_ALL
#endif  // AE_TELE_LOG_MODULES

/**
 * \brief List of excluded modules
 */
#ifndef AE_TELE_LOG_MODULES_EXCLUDE
#  define AE_TELE_LOG_MODULES_EXCLUDE AE_EMPTY_LIST
#endif  // AE_TELE_LOG_MODULES_EXCLUDE

/**
 * \brief Enabled telemetry levels {Debug, Info, Warning, Error} for each module
 * \see tele/levels.h .
 */
#ifndef AE_TELE_DEBUG_MODULES
#  define AE_TELE_DEBUG_MODULES AE_ALL
#endif  // AE_TELE_DEBUG_MODULES

#ifndef AE_TELE_DEBUG_MODULES_EXCLUDE
#  define AE_TELE_DEBUG_MODULES_EXCLUDE AE_EMPTY_LIST
#endif  // AE_TELE_DEBUG_MODULES_EXCLUDE

#ifndef AE_TELE_INFO_MODULES
#  define AE_TELE_INFO_MODULES AE_ALL
#endif  // AE_TELE_INFO_MODULES

#ifndef AE_TELE_INFO_MODULES_EXCLUDE
#  define AE_TELE_INFO_MODULES_EXCLUDE AE_EMPTY_LIST
#endif  // AE_TELE_INFO_MODULES_EXCLUDE

#ifndef AE_TELE_WARN_MODULES
#  define AE_TELE_WARN_MODULES AE_ALL
#endif  // AE_TELE_WARN_MODULES

#ifndef AE_TELE_WARN_MODULES_EXCLUDE
#  define AE_TELE_WARN_MODULES_EXCLUDE AE_EMPTY_LIST
#endif  // AE_TELE_WARN_MODULES_EXCLUDE

#ifndef AE_TELE_ERROR_MODULES
#  define AE_TELE_ERROR_MODULES AE_ALL
#endif  // AE_TELE_ERROR_MODULES

#ifndef AE_TELE_ERROR_MODULES_EXCLUDE
#  define AE_TELE_ERROR_MODULES_EXCLUDE AE_EMPTY_LIST
#endif  // AE_TELE_ERROR_MODULES_EXCLUDE

// enable to log telemetry time point
#ifndef AE_TELE_LOG_TIME_POINT
#  define AE_TELE_LOG_TIME_POINT AE_TELE_LOG_MODULES
#endif  // AE_TELE_LOG_TIME_POINT

#ifndef AE_TELE_LOG_TIME_POINT_EXCLUDE
#  define AE_TELE_LOG_TIME_POINT_EXCLUDE AE_EMPTY_LIST
#endif  // AE_TELE_LOG_TIME_POINT_EXCLUDE

// enable to log telemetry file location
#ifndef AE_TELE_LOG_LOCATION
#  define AE_TELE_LOG_LOCATION AE_TELE_LOG_MODULES
#endif  // AE_TELE_LOG_LOCATION

#ifndef AE_TELE_LOG_LOCATION_EXCLUDE
#  define AE_TELE_LOG_LOCATION_EXCLUDE AE_EMPTY_LIST
#endif  // AE_TELE_LOG_LOCATION_EXCLUDE

// enable to log telemetry tag name
#ifndef AE_TELE_LOG_NAME
#  define AE_TELE_LOG_NAME AE_TELE_LOG_MODULES
#endif  // AE_TELE_LOG_NAME

#ifndef AE_TELE_LOG_NAME_EXCLUDE
#  define AE_TELE_LOG_NAME_EXCLUDE AE_EMPTY_LIST
#endif  // AE_TELE_LOG_NAME_EXCLUDE

// enable to log telemetry module and level
#ifndef AE_TELE_LOG_LEVEL_MODULE
#  define AE_TELE_LOG_LEVEL_MODULE AE_TELE_LOG_MODULES
#endif  // AE_TELE_LOG_LEVEL_MODULE

#ifndef AE_TELE_LOG_LEVEL_MODULE_EXCLUDE
#  define AE_TELE_LOG_LEVEL_MODULE_EXCLUDE AE_EMPTY_LIST
#endif  // AE_TELE_LOG_LEVEL_MODULE_EXCLUDE

// enable to log telemetry message or additional data
#ifndef AE_TELE_LOG_BLOB
#  define AE_TELE_LOG_BLOB AE_TELE_LOG_MODULES
#endif  // AE_TELE_LOG_BLOB

#ifndef AE_TELE_LOG_BLOB_EXCLUDE
#  define AE_TELE_LOG_BLOB_EXCLUDE AE_EMPTY_LIST
#endif  // AE_TELE_LOG_BLOB_EXCLUDE

// enable to log telemetry to console
#ifndef AE_TELE_LOG_CONSOLE
#  define AE_TELE_LOG_CONSOLE 1
#endif  // AE_TELE_LOG_CONSOLE

// the maximum size of the telemetry statistics buffer
#ifndef AE_STATISTICS_MAX_SIZE
#  define AE_STATISTICS_MAX_SIZE (10 * 1024)  // 10 KB
#endif

#ifndef AE_EVENT_HANDLER_MAX_SIZE
#  define AE_EVENT_HANDLER_MAX_SIZE (sizeof(void*) * 6)
#endif
#ifndef AE_EVENT_HANDLER_ALIGN
#  define AE_EVENT_HANDLER_ALIGN (alignof(void*))
#endif

#ifndef NDEBUG
#  define DEBUG 1
#endif
#endif  // AETHER_CONFIG_H_
