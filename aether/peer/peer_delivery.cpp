// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/peer/peer_delivery.h"
#include <random>
#include <stdexcept>
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
#  include <sodium/core.h>
#  include <sodium/randombytes.h>
#elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
#  include <hydrogen.h>
#endif
namespace ae {
Uid NewPeerDeliveryEpoch() {
  Uid epoch{};
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
  static bool const initialized = sodium_init() >= 0;
  if (!initialized) {
    throw std::runtime_error("Delivery RNG initialization failed");
  }
  randombytes_buf(epoch.value.data(), epoch.value.size());
#elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  static bool const initialized = hydro_init() == 0;
  if (!initialized) {
    throw std::runtime_error("Delivery RNG initialization failed");
  }
  hydro_random_buf(epoch.value.data(), epoch.value.size());
#else
  std::random_device random;
  for (auto& byte : epoch.value) {
    byte = static_cast<std::uint8_t>(random());
  }
#endif
  return epoch;
}
}  // namespace ae
