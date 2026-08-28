/*
 * Copyright 2026 Aethernet Inc.
 *
 * Experiment-only in-memory Save statistics (AE_EXP_SAVE_STATS).
 * No printf / ESP_LOG / fflush. Counters only.
 */
#pragma once

#include <cstdint>

#if defined(AE_EXP_SAVE_STATS)
#  if defined(ESP_PLATFORM)
extern "C" std::int64_t esp_timer_get_time(void);
#  else
#    include <chrono>
#  endif
#endif

namespace ae {

#if defined(AE_EXP_SAVE_STATS)
inline std::int64_t AeExpSaveNowUs() {
#  if defined(ESP_PLATFORM)
  return esp_timer_get_time();
#  else
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
#  endif
}
#else
inline std::int64_t AeExpSaveNowUs() { return 0; }
#endif

enum class AeExpSaveCaller : std::uint8_t {
  kNone = 0,
  kConstructInternal = 1,  // CONSTRUCT_INTERNAL
  kExplicitA = 2,          // EXPLICIT_A
  kExplicitB = 3,          // EXPLICIT_B
  kExplicitC = 4,          // EXPLICIT_C
  kExplicitD = 5,          // EXPLICIT_D
  kDestructor = 6,         // DESTRUCTOR
};

// One completed root Save sample (POD for static arrays / binary payload).
struct AeExpSaveSample {
  std::uint8_t caller{0};
  std::uint8_t success{0};
  std::uint32_t root_object_id{0};
  std::uint32_t save_total_us{0};
  std::uint32_t graph_and_serialization_us{0};
  std::uint32_t objects_serialized{0};
  std::uint32_t serialized_bytes{0};
  std::uint32_t crc_count{0};
  std::uint32_t crc_time_us{0};
  std::uint32_t crc_skip_count{0};
  std::uint32_t changed_object_count{0};
  std::uint32_t object_file_write_count{0};
  std::uint32_t object_file_bytes{0};
  std::uint32_t object_open_us{0};
  std::uint32_t object_fwrite_us{0};
  std::uint32_t object_close_us{0};
  std::uint32_t map_rewrite_count{0};
  std::uint32_t map_bytes{0};
  std::uint32_t map_serialization_us{0};
  std::uint32_t map_open_us{0};
  std::uint32_t map_fwrite_us{0};
  std::uint32_t map_close_us{0};
  std::uint32_t save_root_calls{0};
  std::uint32_t writers_created{0};
};

#if defined(AE_EXP_SAVE_STATS)

struct AeExpSaveActive {
  AeExpSaveSample sample{};
  std::uint32_t tx_depth{0};
  bool recording{false};
  AeExpSaveCaller pending_caller{AeExpSaveCaller::kNone};
};

inline AeExpSaveActive& AeExpSaveState() {
  static AeExpSaveActive state;
  return state;
}

inline void AeExpSaveSetCaller(AeExpSaveCaller caller) {
  AeExpSaveState().pending_caller = caller;
}

inline AeExpSaveCaller AeExpSaveGetCaller() {
  return AeExpSaveState().pending_caller;
}

inline void AeExpSaveBeginRoot(std::uint32_t root_id) {
  auto& s = AeExpSaveState();
  if (s.tx_depth == 0) {
    s.sample = AeExpSaveSample{};
    s.sample.caller = static_cast<std::uint8_t>(s.pending_caller);
    s.sample.root_object_id = root_id;
    s.sample.success = 1;
    s.recording = true;
  }
  ++s.tx_depth;
}

inline void AeExpSaveEndRoot(std::uint32_t total_us) {
  auto& s = AeExpSaveState();
  if (s.tx_depth == 0) {
    return;
  }
  --s.tx_depth;
  if (s.tx_depth != 0) {
    return;
  }
  s.sample.save_total_us = total_us;
  auto const exclusive = s.sample.crc_time_us + s.sample.object_open_us +
                         s.sample.object_fwrite_us + s.sample.object_close_us +
                         s.sample.map_serialization_us + s.sample.map_open_us +
                         s.sample.map_fwrite_us + s.sample.map_close_us;
  s.sample.graph_and_serialization_us =
      (total_us > exclusive) ? (total_us - exclusive) : 0;
  s.recording = false;
}

inline AeExpSaveSample const& AeExpSaveLastSample() {
  return AeExpSaveState().sample;
}

inline void AeExpSaveAdd(std::uint32_t AeExpSaveSample::*field,
                         std::uint32_t value) {
  auto& s = AeExpSaveState();
  if (s.recording) {
    s.sample.*field += value;
  }
}

inline void AeExpSaveInc(std::uint32_t AeExpSaveSample::*field) {
  AeExpSaveAdd(field, 1);
}

inline void AeExpSaveMarkFail() {
  auto& s = AeExpSaveState();
  if (s.recording) {
    s.sample.success = 0;
  }
}

#else  // !AE_EXP_SAVE_STATS

inline void AeExpSaveSetCaller(AeExpSaveCaller) {}
inline AeExpSaveCaller AeExpSaveGetCaller() {
  return AeExpSaveCaller::kNone;
}
inline void AeExpSaveBeginRoot(std::uint32_t) {}
inline void AeExpSaveEndRoot(std::uint32_t) {}
inline AeExpSaveSample const& AeExpSaveLastSample() {
  static AeExpSaveSample empty;
  return empty;
}
inline void AeExpSaveAdd(std::uint32_t AeExpSaveSample::*, std::uint32_t) {}
inline void AeExpSaveInc(std::uint32_t AeExpSaveSample::*) {}
inline void AeExpSaveMarkFail() {}

#endif

}  // namespace ae
