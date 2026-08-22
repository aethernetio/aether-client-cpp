/*
 * Copyright 2026 Aethernet Inc.
 */

#include "aether/work_cloud_api/work_server_api/server_api_by_uid.h"

namespace ae {
ServerApiByUid::ServerApiByUid(ProtocolContext& protocol_context)
    : ApiClass{protocol_context},
      online_time{protocol_context},
      next_online_time{protocol_context} {}
}  // namespace ae
