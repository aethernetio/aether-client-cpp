/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#ifndef AETHER_TEST_TELE_DISABLED_CONFIG_H_
#define AETHER_TEST_TELE_DISABLED_CONFIG_H_

#include "config/user_config_sodium.h"

#undef AE_TELE_ENABLED
#undef AE_TELE_LOG_TO_STATISTICS
#undef AE_TELE_LOG_CONSOLE
#define AE_TELE_ENABLED 0
#define AE_TELE_LOG_TO_STATISTICS 0
#define AE_TELE_LOG_CONSOLE 0

#endif  // AETHER_TEST_TELE_DISABLED_CONFIG_H_
