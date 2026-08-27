/*
 * Copyright 2026 Aethernet Inc.
 * Experiment-only lifecycle / Save diagnostics (AE_EXP_DIAG).
 */
#pragma once

#include <cstdint>
#include <cstdio>

#if defined(AE_EXP_DIAG)

#  include <chrono>

inline std::int64_t AeExpNowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline std::uint32_t AeExpNextId() {
  static std::uint32_t n = 0;
  return ++n;
}

// cycle number set by experiment app_main (0 = unknown/register)
inline std::uint32_t& AeExpCycle() {
  static std::uint32_t c = 0;
  return c;
}

inline void AeExpSetCycle(std::uint32_t c) { AeExpCycle() = c; }

// LC\tcycle\tus\ttype\tid\tptr\tparent\tevent\tdetail
#  define AE_EXP_LC(type, id, ptr, parent, event, detail_fmt, ...)            \
    do {                                                                      \
      std::printf("LC\t%lu\t%lld\t%s\t%lu\t%p\t%lu\t" event "\t" detail_fmt  \
                  "\n",                                                       \
                  static_cast<unsigned long>(::AeExpCycle()),                 \
                  static_cast<long long>(::AeExpNowUs()), (type),              \
                  static_cast<unsigned long>(id),                             \
                  static_cast<void const*>(ptr),                              \
                  static_cast<unsigned long>(parent), ##__VA_ARGS__);         \
      std::fflush(stdout);                                                    \
    } while (0)

#  define AE_EXP_SAVE(event, detail_fmt, ...)                                 \
    do {                                                                      \
      std::printf("SV\t%lu\t%lld\t" event "\t" detail_fmt "\n",               \
                  static_cast<unsigned long>(::AeExpCycle()),                 \
                  static_cast<long long>(::AeExpNowUs()), ##__VA_ARGS__);     \
      std::fflush(stdout);                                                    \
    } while (0)

#else  // !AE_EXP_DIAG

#  define AE_EXP_LC(...) \
    do {                 \
    } while (0)
#  define AE_EXP_SAVE(...) \
    do {                   \
    } while (0)
inline void AeExpSetCycle(std::uint32_t) {}
inline std::uint32_t AeExpNextId() { return 0; }

#endif
