#ifndef AETHER_TYPES_PRESENCE_NUMERIC_H_
#define AETHER_TYPES_PRESENCE_NUMERIC_H_
// MSVC diagnoses unreachable unsigned branches while instantiating the pinned
// numeric library. Keep suppression scoped to that external header.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
#include "ae-numeric/percentile.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif
