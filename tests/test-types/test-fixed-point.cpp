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

#include "aether/fixed_point.h"
#include "aether/tiered_int.h"
#include "aether/mstream.h"

namespace ae::test_fixed_point {
static_assert(CalculateIntegerBits<uint8_t>(0.000975) == -11);
static_assert(CalculateIntegerBits<uint32_t>(0.000975) == -10);
static_assert(CalculateIntegerBits<uint8_t>(0.4) == -1);
static_assert(CalculateIntegerBits<uint8_t>(0.499) == 0);
static_assert(CalculateIntegerBits<uint8_t>(0.996) == 0);
static_assert(CalculateIntegerBits<uint16_t>(0.997) == 0);
static_assert(CalculateIntegerBits<uint8_t>(0.997) == 1);
static_assert(CalculateIntegerBits<uint8_t>(122.0) == 7);
static_assert(CalculateIntegerBits<uint8_t>(222.0) == 8);
static_assert(CalculateIntegerBits<uint8_t>(16300.001) == 14);
static_assert(CalculateIntegerBits<uint32_t>(16380.001) == 14);
static_assert(CalculateIntegerBits<uint8_t>(16380.001) == 15);
static_assert(CalculateIntegerBits<uint8_t>(1363148.8) == 21);

template <typename Type, int int_bits>
void TestConversion(double value) {
  using F = Fixed<Type, 0, int_bits>;
  F f(value);
  // auto min_value = F::kMinValue;
  double diff = std::abs(static_cast<double>(f) - value);
  TEST_ASSERT_LESS_THAN_DOUBLE(std::numeric_limits<F>::min(), diff);
}

void test_FixedPoint() {
  {
    // Runtime assignment
    using F1 = AE_FIXED(uint8_t, 123.5);
    static_assert(F1::kIntBits == 7);
    static_assert(F1::kMaxValue == 247);
    F1 f1 = F1(123.5);
    TEST_ASSERT_EQUAL(f1.value_, 247);
    TEST_ASSERT_EQUAL(static_cast<double>(f1), 123.5);
    auto const& f2 = f1 = 61.5;
    TEST_ASSERT_EQUAL(f1.value_, 123);
    TEST_ASSERT_EQUAL(static_cast<double>(f1), 61.5);
    TEST_ASSERT_EQUAL(&f2, &f1);
  }

  {
    // Convert from/to double
    constexpr double value = 0.0001;
    constexpr auto f = AE_FIXED(uint8_t, 0.001){value};
    using F = std::decay_t<decltype(f)>;
    static_assert(F::kIntBits == -9);
    static_assert(F::kMaxValue == 131);
    static_assert(f.value_ == 13);
    constexpr double diff = gcem::abs(static_cast<double>(f) - value);
    static_assert(diff < std::numeric_limits<F>::min());
  }
  {
    constexpr auto f2 = AE_FIXED(uint8_t, 123.5){61.5};
    static_assert(decltype(f2)::kIntBits == 7);
    static_assert(decltype(f2)::kMaxValue == 247);
    static_assert(f2.value_ == 123);
    static_assert(static_cast<double>(f2) == 61.5);
  }

  static_assert(CompareDoubles<uint8_t>(
      std::numeric_limits<Fixed<uint8_t, 0, 0>>::min(), 1.0 / 256));
  static_assert(CompareDoubles<uint16_t>(
      std::numeric_limits<Fixed<uint16_t, 0, 0>>::min(), 1.0 / 65536));
  static_assert(
      CompareDoubles<uint8_t>(Fixed<uint8_t, 0, -1>::kMinValue, 1.0 / 512));
  static_assert(
      CompareDoubles<uint8_t>(Fixed<uint8_t, 0, 0>::kMinValue, 1.0 / 256));
  static_assert(
      CompareDoubles<uint8_t>(Fixed<uint8_t, 0, 1>::kMinValue, 1.0 / 128));
  static_assert(
      CompareDoubles<uint8_t>(Fixed<uint8_t, 0, 7>::kMinValue, 1.0 / 2));
  static_assert(CompareDoubles<uint8_t>(Fixed<uint8_t, 0, 8>::kMinValue, 1.0));
  static_assert(CompareDoubles<uint8_t>(Fixed<uint8_t, 0, 9>::kMinValue, 2.0));
  static_assert(Fixed<uint8_t, 0, -21>::Value(4.1e-7) ==
                static_cast<uint8_t>(4.1e-7 * (1ul << (21 + 8))));
  static_assert(Fixed<uint8_t, 0, 7>::Value(123.5) == 123.5 * 2);
  static_assert(Fixed<uint8_t, 0, 8>::Value(123.0) == 123);
  static_assert(Fixed<uint8_t, 0, 9>::Value(123.0) == 123 / 2);
  static_assert(Fixed<uint8_t, 0, 29>::Value(123456789.0) ==
                std::uint8_t{123456789 / (1ul << (29 - 8))});
  static_assert(gcem::abs(Fixed<uint16_t, 0, 7>::Value(123.5) -
                          123.5 * (1ul << (16 - 7))) < 2);
  TestConversion<uint8_t, -21>(4.1e-7);
  TestConversion<uint8_t, 0>(0.984375);
  TestConversion<uint8_t, 7>(123.5);
  TestConversion<uint8_t, 29>(123456789.0);

  {
    using T = std::uint16_t;
    auto v1 = AE_FIXED(T, 3.5){3.1};
    auto v2 = AE_FIXED(T, 100){0.5};
    auto v3 = v1 / v2;
    //    auto v3 = Div<T, decltype(v2)::Value(0.5)>(v1, v2);
    double b0 = decltype(v3)::kTotalBits;
    double b1 = decltype(v3)::kIntBits;
    double b2 = decltype(v3)::kMaxValue * decltype(v3)::kMinValue;
    double r = v3;
    auto v4 = v3 - AE_FIXED(T, 4){3.1};
    double n0 = decltype(v4)::kTotalBits;
    double n1 = decltype(v4)::kIntBits;
    double n2 = decltype(v4)::kMaxValue * decltype(v4)::kMinValue;
    double r1 = v4;
    int df = 0;
  }
}

void test_Exponent() {
  {
    using P = TieredInt<std::uint16_t, std::uint8_t, 250>;
    using E = AE_EXPONENT(P, 0.001, 60.0);

    E e(60.0);
    auto stored = e.Serialize();
    E e2;
    e2.Deserialize(stored);
    double rr = e2;

    e = E(0.01);
    stored = e.Serialize();
    e2.Deserialize(stored);
    double rr2 = e2;
  }
  {
    using E = AE_EXPONENT(uint8_t, 0.001, 60.0);
    E e(E::Value(60.0));
    auto stored = e.Serialize();
    E e2;
    e2.Deserialize(stored);
    double rr = e2;

    e = E(E::Value(0.01));
    stored = e.Serialize();
    e2.Deserialize(stored);
    double rr2 = e2;
  }
}
}  // namespace ae::test_fixed_point

int test_fixed_point() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_fixed_point::test_FixedPoint);
  RUN_TEST(ae::test_fixed_point::test_Exponent);
  return UNITY_END();
}
