/*
 * Copyright 2026 Aethernet Inc.
 * Validates ACK_TX against recv index coverage at ACK emission time (ring-aware).
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_STREAM_MATRIX_SAFE_STREAM_ACK_CHECK_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_STREAM_MATRIX_SAFE_STREAM_ACK_CHECK_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "aether/config.h"
#include "aether/types/ring_index.h"

namespace ae::stream_matrix_bench {

template <std::size_t RingMax = AE_SAFE_STREAM_CAPACITY>
class SafeStreamAckCoverageValidator {
 public:
  using Index = RingIndex<RingMax>;

  void ClearSessionsKeepViolations() { sessions_.clear(); }

  void OnRecv(char side, std::uint16_t begin, std::uint16_t delta, std::size_t len,
              bool reset) {
    any_recv_ = true;
    ++recv_events_;
    SessionState& s = SessionFor(side);
    if (len == 0) {
      if (reset) {
        s.origin = Index(begin);
        s.coverage_begin = Index(begin);
        s.active = true;
        s.ranges_.clear();
      }
      return;
    }
    Index const left = Index(begin) + static_cast<std::size_t>(delta);
    Index const right = left + (len - 1);
    if (reset) {
      s.ranges_.clear();
      s.active = true;
      s.origin = left;
      s.coverage_begin = left;
    } else if (!s.active) {
      s.origin = left;
      s.coverage_begin = left;
      s.active = true;
    }
    AddRange(s, left, right);
  }

  // Returns false if this ACK is invalid; latches so later recv cannot undo.
  bool OnAckTx(char side, std::uint16_t ack_offset) {
    if (!any_recv_) {
      latched_violation_ = true;
      return false;
    }
    SessionState& s = SessionFor(side);
    if (!s.active) {
      return true;
    }
    if (s.ranges_.empty()) {
      return true;
    }
    saw_ack_tx_ = true;
    Index const ack_idx(ack_offset);
    if (!CoversThroughLinear(s, s.coverage_begin, ack_idx) &&
        !CoversThrough(s, s.coverage_begin, ack_idx)) {
      latched_violation_ = true;
      return false;
    }
    if (IndexComparable<Index>{ack_idx, s.origin} >= s.coverage_begin) {
      s.coverage_begin = ack_idx + 1;
    }
    return true;
  }

  bool latched_violation() const { return latched_violation_; }
  bool saw_ack_tx() const { return saw_ack_tx_; }

  bool acks_ok() const { return saw_ack_tx_ && !latched_violation_; }

  std::size_t recv_events() const { return recv_events_; }

  // Replays the old end-of-test check (uint16 sort, ignores session origin gap).
  bool LegacyEndTestWouldPass(char side, std::uint16_t ack_offset) const {
    auto it = sessions_.find(side);
    if (it == sessions_.end() || !it->second.active ||
        it->second.ranges_.empty()) {
      return false;
    }
    struct Span {
      std::uint32_t start{};
      std::uint32_t end{};  // exclusive
    };
    std::vector<Span> spans;
    spans.reserve(it->second.ranges_.size());
    for (auto const& [l, r] : it->second.ranges_) {
      spans.push_back({static_cast<std::uint32_t>(l),
                       static_cast<std::uint32_t>(r) + 1});
    }
    std::sort(spans.begin(), spans.end(),
              [](Span const& a, Span const& b) { return a.start < b.start; });
    std::uint32_t covered = spans.front().start;
    for (auto const& sp : spans) {
      if (sp.start > covered) {
        break;
      }
      covered = std::max(covered, sp.end);
    }
    return covered >= static_cast<std::uint32_t>(ack_offset) + 1;
  }

 private:
  struct SessionState {
    Index origin{};
    Index coverage_begin{};
    bool active{false};
    std::vector<std::pair<Index, Index>> ranges_;  // inclusive [left, right]
  };

  SessionState& SessionFor(char side) { return sessions_[side]; }

  static void AddRange(SessionState& s, Index left, Index right) {
    s.ranges_.emplace_back(left, right);
    MergeRangesInPlace(s);
  }

  static void MergeRangesInPlace(SessionState& s) {
    Index const begin = s.origin;
    auto& ranges = s.ranges_;
    std::sort(ranges.begin(), ranges.end(), [begin](auto const& a, auto const& b) {
      return IndexComparable<Index>{a.first, begin}.IsBefore(b.first);
    });
    std::vector<std::pair<Index, Index>> merged;
    for (auto const& [l, r] : ranges) {
      if (merged.empty()) {
        merged.emplace_back(l, r);
        continue;
      }
      auto& back = merged.back();
      Index const next_after_back = back.second + 1;
      if (IndexComparable<Index>{l, begin} <= next_after_back &&
          IndexComparable<Index>{r, begin} >= back.second) {
        if (IndexComparable<Index>{r, begin} > back.second) {
          back.second = r;
        }
      } else if (IndexComparable<Index>{l, begin} > back.second + 1) {
        merged.emplace_back(l, r);
      } else if (IndexComparable<Index>{r, begin} > back.second) {
        back.second = r;
      }
    }
    ranges = std::move(merged);
  }

  static bool CoversThroughLinear(SessionState const& s, Index from,
                                  Index ack_inclusive) {
    std::uint32_t const from_u = static_cast<std::uint32_t>(from);
    std::uint32_t const ack_u = static_cast<std::uint32_t>(ack_inclusive);
    if (s.ranges_.empty() || ack_u < from_u) {
      return false;
    }
    std::uint32_t pos = from_u;
    while (pos <= ack_u) {
      bool advanced = false;
      for (auto const& [l, r] : s.ranges_) {
        std::uint32_t const lu = static_cast<std::uint32_t>(l);
        std::uint32_t const ru = static_cast<std::uint32_t>(r);
        if (lu <= pos && pos <= ru) {
          pos = ru + 1;
          advanced = true;
          break;
        }
      }
      if (!advanced) {
        return false;
      }
    }
    return true;
  }

  static bool CoversThrough(SessionState const& s, Index from,
                            Index ack_inclusive) {
    Index const begin = s.origin;
    if (s.ranges_.empty()) {
      return false;
    }
    if (IndexComparable<Index>{ack_inclusive, begin} < from) {
      return false;
    }
    Index pos = from;
    while (IndexComparable<Index>{pos, begin} <= ack_inclusive) {
      bool advanced = false;
      for (auto const& [l, r] : s.ranges_) {
        RingIndexRange<Index> band{.left = l, .right = r};
        if (band.InRange(pos, begin)) {
          pos = r + 1;
          advanced = true;
          break;
        }
      }
      if (!advanced) {
        return false;
      }
    }
    return true;
  }

  std::map<char, SessionState> sessions_;
  bool any_recv_{false};
  bool latched_violation_{false};
  bool saw_ack_tx_{false};
  std::size_t recv_events_{0};
};

}  // namespace ae::stream_matrix_bench

#endif
