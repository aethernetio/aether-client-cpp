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

#include <unity.h>

#include <vector>

#include "aether/ptr/rc_ptr.h"

#include "tests/benchmarking.h"

namespace ae::test_rc_ptr_bench {
#if !defined NDEBUG
static constexpr std::size_t kBenchCount = 10'000'000;
#else
static constexpr std::size_t kBenchCount = 100'000'000;
#endif

struct Rabbit {
  Rabbit(int y) { x = y * y; }
  ~Rabbit() { x = x - x / 2; }
  volatile int x{0};
};

void test_rc_ptr_CreationBench() {
  tests::BenchmarkFunc(
      [](auto i) {
        auto rabbit_ptr = MakeRcPtr<Rabbit>(static_cast<int>(i));
        rabbit_ptr->x += static_cast<int>(i) % 100;
        rabbit_ptr.Reset();
      },
      kBenchCount, "create and destroy ");

  std::vector<RcPtr<Rabbit>> rabbits1;
  rabbits1.reserve(1000);
  tests::BenchmarkFunc(
      [&](auto i) {
        for (volatile auto j = 0; j < rabbits1.capacity(); j++) {
          rabbits1.push_back(MakeRcPtr<Rabbit>(static_cast<int>(i + j)));
          rabbits1.back()->x += static_cast<int>(i) % 100;
        }
        rabbits1.clear();
      },
      kBenchCount / rabbits1.capacity(), "create a bunch size ",
      rabbits1.capacity(), " and destroy all after");
}

std::vector<RcPtr<Rabbit>> rabbits2;
void test_rc_ptr_CopyingBench() {
  tests::BenchmarkFunc(
      [&](auto i) {
        rabbits2.push_back(MakeRcPtr<Rabbit>(static_cast<int>(i % 10'000)));
        if (((i + 1) % 10'000) == 0) {
          rabbits2.clear();
          rabbits2.shrink_to_fit();
        }
      },
      kBenchCount, "moving inside the vector while it grows max ", 10'000,
      " elements");

  std::vector<RcPtr<Rabbit>> rabbits3;
  rabbits3.reserve(1000);
  for (volatile std::size_t j = 0; j < rabbits3.capacity(); j++) {
    rabbits3.push_back(MakeRcPtr<Rabbit>(static_cast<int>(j)));
  }
  tests::BenchmarkFunc(
      [&](auto i) {
        auto copy_rabbits = rabbits3;
        for (auto& r : copy_rabbits) {
          r->x += i % 100;
        }
      },
      kBenchCount / 100, "copying the vector size: ", rabbits3.capacity());
}

}  // namespace ae::test_rc_ptr_bench

int test_rc_ptr_bench() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_rc_ptr_bench::test_rc_ptr_CreationBench);
  RUN_TEST(ae::test_rc_ptr_bench::test_rc_ptr_CopyingBench);
  return UNITY_END();
}
