/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef AETHER_TELE_TRAPS_TELE_STATISTICS_H_
#define AETHER_TELE_TRAPS_TELE_STATISTICS_H_

#include <memory>

#include "aether-objects/obj/obj.h"
#include "aether/config.h"
#include "aether/tele_statistics_backend.h"
#include "aether/telemetry_persistent_state.h"

namespace ae {
class TeleStatistics : public Obj {
  AE_OBJECT(TeleStatistics, Obj, 1)

  TeleStatistics() = default;

 public:
#ifdef AE_DISTILLATION
  explicit TeleStatistics(ObjProp prop);
#endif

  AE_OBJECT_REFLECT()

#if AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    // The archive serializer stages and validates the entire value. A failed
    // load leaves the live backend unchanged; Loaded is called only on success.
    dnv(base_, loaded_state_);
  }
  template <typename Dnv>
  void Load(Version<0>, Dnv& dnv) {
    // The previous class version has no compatibility gate. Do not deserialize
    // its telemetry at all; the independent application objects still load.
    dnv(base_);
    loaded_state_ = {};
    loaded_state_.reset = true;
  }
  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    TelemetryPersistentState snapshot{TelemetryCompatibilityId(),
                                      backend_->Snapshot(), outbox_};
    snapshot.source = GetTelemetryProductInfo();
    snapshot.epochs = epochs_;
    dnv(base_, snapshot);
  }

  void Loaded();
  std::shared_ptr<TeleStatisticsBackend> const& backend() const;
  // Model/owner-thread operations. Call before pumping the delivery session;
  // its persistence callback saves this ordinary Obj before first network IO.
  bool PrepareDelivery();
  TelemetryOutbox& delivery_outbox() { return outbox_; }
  bool telemetry_state_reset() const { return loaded_state_.reset; }

 private:
  TelemetryPersistentState loaded_state_;
  TelemetryOutbox outbox_;
  std::vector<TelemetrySourceEpoch> epochs_;
  std::shared_ptr<TeleStatisticsBackend> backend_ =
      std::make_shared<TeleStatisticsBackend>();
#endif
};
}  // namespace ae
#endif  // AETHER_TELE_TRAPS_TELE_STATISTICS_H_
