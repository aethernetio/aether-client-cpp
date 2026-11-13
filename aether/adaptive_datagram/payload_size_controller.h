#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace ae::adaptive_datagram {
// Producer policy, sampled once per second. Message boundaries are unchanged.
class PayloadSizeController {
 public:
  PayloadSizeController(std::size_t initial, std::size_t maximum)
      : size_{initial},
        minimum_{initial},
        maximum_{std::max(initial, maximum)} {}
  std::size_t Observe(bool healthy, bool overloaded, std::uint64_t goodput) {
    if (overloaded) {
      size_ = std::max(minimum_, size_ / 2);
      probing_ = false;
      Reset();
      return size_;
    }
    samples_[next_] = {goodput, healthy && goodput != 0};
    next_ = (next_ + 1) % samples_.size();
    count_ = std::min(count_ + 1, samples_.size());
    if (count_ < samples_.size()) {
      return size_;
    }
    unsigned valid = 0;
    long double sum = 0, squares = 0, first = 0, last = 0;
    for (std::size_t i = 0; i < samples_.size(); ++i) {
      auto const& sample = samples_[(next_ + i) % samples_.size()];
      valid += sample.healthy;
      auto const rate = static_cast<long double>(sample.rate);
      sum += rate;
      squares += rate * rate;
      (i < 5 ? first : last) += rate;
    }
    auto const mean = sum / samples_.size();
    if (probing_) {
      // Bound the probe to ten seconds, including unknown samples. Unknown
      // feedback is not overload and cannot establish a successful probe.
      if (valid < 7 || !healthy || mean <= baseline_ * 1.02L) {
        if (valid >= 7 && healthy) {
          maximum_ = old_size_;
        }
        size_ = old_size_;
      }
      probing_ = false;
      Reset();
    } else if (healthy && valid >= 7 && mean > 0 &&
               squares / samples_.size() - mean * mean <=
                   mean * mean * 0.35L * 0.35L &&
               last >= first * 0.8L && size_ < maximum_) {
      old_size_ = size_;
      baseline_ = mean;
      size_ = std::min(maximum_, size_ + std::max<std::size_t>(1, size_ / 4));
      probing_ = true;
      Reset();
    }
    return size_;
  }

 private:
  struct Sample {
    std::uint64_t rate{};
    bool healthy{};
  };
  void Reset() {
    count_ = 0;
    next_ = 0;
  }
  std::array<Sample, 10> samples_{};
  std::size_t count_{}, next_{};
  std::size_t size_, minimum_, maximum_, old_size_{};
  bool probing_{};
  long double baseline_{};
};
}  // namespace ae::adaptive_datagram
