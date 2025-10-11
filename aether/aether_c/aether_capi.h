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

#ifndef AETHER_AETHER_C_AETHER_CAPI_H_
#define AETHER_AETHER_C_AETHER_CAPI_H_

#include <stddef.h>
#include <stdint.h>

#include "aether/aether_c/c_uid.h"
#include "aether/aether_c/c_conf.h"
#include "aether/aether_c/c_types.h"
#include "aether/aether_c/extern_c.h"

AE_EXTERN_C_BEGIN
/**
 * \brief Init aether library
 * \param config The configuration for aether library.
 * \return 0 on success, -1 on failure.
 */
int AetherInit(AetherConfig const* config);

/**
 * \brief Deinit aether library.
 * \return 0 on success or exit code if AetherExit was called.
 */
int AetherEnd();

/**
 * \brief Run aether update.
 * Call in loop to run internal aether async operations.
 * \return The epoch time in milliseconds.
 */
uint64_t AetherUpdate();
/**
 * \brief Wait until specified timeout or internal aether event.
 * \param timeout The epoch time in milliseconds.
 * \return 0 on success, -1 on failure.
 */
int AetherWait(uint64_t timeout);
/**
 * \brief Mark Aether library to exit.
 */
void AetherExit(int exit_code);
/**
 * \brief Check if aether already excited.
 * \return -1 if not excited and exit_code in otherwise.
 */
int AetherExcited();

/**
 * \brief Send message using default client.
 * Default client should be configured using AetherConfig.default_client
 * \param destination The destination client to send the message to.
 * \param data The data to send.
 * \param size The size of the data to send.
 * \param message The message - null terminated string.
 * \param status_cb (may be NULL) The callback to notify about the sending
 * status.
 * \param user_data (may be NULL) The user data to pass to the callback.
 */
void Send(CUid destination, void const* data, size_t size,
          ActionStatusCb status_cb, void* user_data);
void SendStr(CUid destination, char const* message, ActionStatusCb status_cb,
             void* user_data);

/**
 * \brief Select the client for sending messages.
 * Client either loaded from saved state or registered in Aethernet.
 * Client selection is async operation, notification will be sent via callback.
 * \param config The configuration for client selection.
 * \return The selected client or NULL on failure or blocking operation.
 */
AetherClient* SelectClient(ClientConfig const* config);
/**
 * \brief Free client's resources.
 */
void FreeClient(AetherClient* client);

/**
 * \brief Send a message from selected client to destination client.
 * \param client The client to send the message from.
 * \param destination The destination client to send the message to.
 * \param data The data to send.
 * \param size The size of the data to send.
 * \param message The message - null terminated string.
 * \param status_cb (may be NULL) The callback to notify about the sending
 * status.
 * \param user_data (may be NULL) The user data to pass to the callback.
 */
void ClientSendMessage(AetherClient* client, CUid destination, void const* data,
                       size_t size, ActionStatusCb status_cb, void* user_data);
void ClientSendMessageStr(AetherClient* client, CUid destination,
                          char const* message, ActionStatusCb status_cb,
                          void* user_data);

AE_EXTERN_C_END
#endif  // AETHER_AETHER_C_AETHER_CAPI_H_
