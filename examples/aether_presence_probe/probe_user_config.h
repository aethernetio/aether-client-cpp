/*
 * Match AppTraverse runtime flags for presence probe experiments:
 * filtration (via CMake AE_FILTRATION), registration + cloud DNS, tele off.
 */
#ifndef AETHER_PRESENCE_PROBE_USER_CONFIG_H_
#define AETHER_PRESENCE_PROBE_USER_CONFIG_H_

#include "aether/config_consts.h"

#define AE_TELE_ENABLED 0
#define AE_TELE_LOG_CONSOLE 0
#define AE_TELE_LOG_TO_STATISTICS 0
#define AE_SUPPORT_REGISTRATION 1
#define AE_SUPPORT_CLOUD_DNS 1

#endif /* AETHER_PRESENCE_PROBE_USER_CONFIG_H_ */
