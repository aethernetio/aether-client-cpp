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

#ifndef TESTS_BENCHMARKING_H_
#define TESTS_BENCHMARKING_H_

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace ae::tests {
template <typename Func, typename... MessageArgs>
void BenchmarkFunc(Func&& func, std::size_t count,
                   MessageArgs&&... message_args) {
  auto time_before = std::chrono::steady_clock::now();
  for (volatile std::size_t i = 0; i < count; ++i) {
    func(i);
  }
  auto time_after = std::chrono::steady_clock::now();
  auto diff = std::chrono::duration_cast<std::chrono::duration<double>>(
      time_after - time_before);

  std::cout << "┌" << '\n' << "│Bench: ";
  ((std::cout << message_args), ...);
  std::cout << "\n│\tcount: " << count
            << "\n│\tres time: " << std::setprecision(9) << std::fixed
            << diff.count() << " sec."
            << "\n└" << std::endl;
}

}  // namespace ae::tests

#endif  // TESTS_BENCHMARKING_H_
