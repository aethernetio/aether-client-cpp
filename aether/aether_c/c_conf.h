/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_AETHER_C_C_CONF_H_
#define AETHER_AETHER_C_C_CONF_H_

#include <stdint.h>
#include <stddef.h>

#include "aether/aether_c/c_uid.h"
#include "aether/aether_c/extern_c.h"

AE_EXTERN_C_BEGIN

// TODO: add configuration for all modules
typedef int AeConfType;

// configuration structure types
#define AeDomainStorage 001
#define AeEthernetAdapter 101
#define AeWifiAdapter 102
#define AeModemAdapter 103

typedef struct AetherClient AetherClient;

typedef struct AeDomainStorageConf AeDomainStorageConf;

typedef struct Adapters Adapters;
typedef struct AdapterBase {
  AeConfType type;
} AdapterBase;

typedef struct ClientConfig ClientConfig;

/**
 * \brief Aether library config.
 * It contains configurations for several modules.
 * NULL means default configuration will be used.
 */
typedef struct AetherConfig {
  AeDomainStorageConf* domain_storage_conf;
  Adapters* adapters;
  ClientConfig* default_client;
} AetherConfig;

struct Adapters {
  size_t count;
  AdapterBase** adapters;
};

typedef enum AeDomainFacilityVariant {
  kRam,         //< All the states will be stored in RAM.
  kFileSystem,  //< All the states will be stored in a file system.
  kSpifs,       //< All the states will be stored in a SPIFFS file system.
} AeDomainStorageVariant;

struct AeDomainStorageConf {
  AeConfType type;  // should be AeDomainStorage
  AeDomainStorageVariant variant;
};

/**
 * \brief Configuration for EthernetAdapter.
 */
typedef struct AeEthernetAdapterConf {
  AeConfType type;  // should be AeEthernetAdapter
} AeEthernetAdapterConf;

/**
 * \brief Configuration for WifiAdapter.
 */
typedef struct AeWifiAdapterConf {
  AeConfType type;  // should be AeWifiAdapter
  char const* ssid;
  char const* password;
} AeWifiAdapterConf;

/**
 * \brief Client selection callback
 * \param client The selected client or NULL on failure.
 * \param user_data User data set in ClientConfig.
 */
typedef void (*ClientSelectedCb)(AetherClient* client, void* user_data);

/**
 * \brief Message receive callback
 */
typedef void (*MessageReceivedCb)(AetherClient* client, CUid sender,
                                  void const* data, size_t size,
                                  void* user_data);

/**
 * \brief Client configuration
 */
struct ClientConfig {
  char const* id;   //< the local id of client
  CUid parent_uid;  //< the parent uid, required for client registration
  ClientSelectedCb client_selected_cb;    //< callback for client selection,
                                          // called when client is selected
  MessageReceivedCb message_received_cb;  //< callback for message reception,
                                          // called when message is received
  void* user_data;  //< user data passed to client_selected_cb
};

AE_EXTERN_C_END

#endif  // AETHER_AETHER_C_C_CONF_H_
