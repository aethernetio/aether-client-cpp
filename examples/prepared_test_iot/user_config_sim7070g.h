// Copyright 2026 Aethernet Inc.
// SPDX-License-Identifier: Apache-2.0
#ifndef EXAMPLES_PREPARED_TEST_IOT_USER_CONFIG_SIM7070G_H_
#define EXAMPLES_PREPARED_TEST_IOT_USER_CONFIG_SIM7070G_H_

#include "config/user_config_hydrogen.h"  // IWYU pragma: export

#define AE_SUPPORT_MODEMS 1
#define AE_ENABLE_SIM7070 1
#define AE_ENABLE_THINGY91X 0
#define AE_ENABLE_BG95 0

// PreparedSendMessage needs a numeric UDP endpoint.
#define AE_SUPPORT_UDP 1
#define AE_SUPPORT_IPV4 1
#define AE_SUPPORT_IPV6 0

#endif  // EXAMPLES_PREPARED_TEST_IOT_USER_CONFIG_SIM7070G_H_
