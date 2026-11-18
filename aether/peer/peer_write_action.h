// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_PEER_WRITE_ACTION_H_
#define AETHER_PEER_PEER_WRITE_ACTION_H_
#include "aether/ae_context.h"
#include "aether/write_action/write_action.h"
namespace ae {
class PeerWriteAction final : public WriteAction {
 public:
  PeerWriteAction(AeContext const& context, bool success)
      : WriteAction{context} {
    context.scheduler().Task(completion_, [this, success] {
      SetStatus(success ? Status::kSuccess : Status::kFail);
    });
  }

 private:
  TaskSubscription completion_;
};
}  // namespace ae
#endif
