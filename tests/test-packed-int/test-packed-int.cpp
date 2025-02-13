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
#include <cstdint>
#include <cstring>
#include <random>
#include <iostream>

#include "aether/mstream.h"
#include "aether/packed_int.h"
#include "aether/format/format.h"

void setUp() {}
void tearDown() {}

namespace ae::test_packet_int {

struct ObVector {
  using size_type = std::uint32_t;
  std::vector<std::uint8_t> data_;

  size_t write(const void* data, size_t size) {
    data_.insert(std::end(data_), static_cast<std::uint8_t const*>(data),
                 static_cast<std::uint8_t const*>(data) + size);
    return size;
  }
};

struct Omstream {
  ObVector obv_;
  omstream<ObVector> stream_{obv_};

  auto& stream() { return stream_; }
  auto& data() const { return obv_.data_; }
};

struct IbVector {
  using size_type = std::uint32_t;

  std::vector<std::uint8_t> data_;
  ReadResult result_;

  IbVector(const std::vector<std::uint8_t>& v) : data_(v) {}

  size_t read(void* data, size_t size, size_t /* min_size */) {
    std::memcpy(data, data_.data(), size);
    data_.erase(data_.begin(), data_.begin() + size);
    result_ = ReadResult::kYes;
    return size;
  }

  ReadResult result() const { return result_; }
  void result(ReadResult res) { result_ = res; }
};

struct Imstream {
  IbVector ibv_;
  imstream<IbVector> stream_;

  Imstream(std::vector<std::uint8_t> const& v) : ibv_{v}, stream_{ibv_} {}

  auto& stream() { return stream_; }
};

static std::string print(std::vector<std::uint8_t> const& v) {
  std::stringstream ss;
  ss << "[";
  for (auto i : v) {
    ss << std::setfill('0') << std::setw(2) << std::hex
       << static_cast<std::uint32_t>(i);
  }
  ss << "]";
  return ss.str();
}

template <typename T>
T GetRandom(T min, T max) {
  static std::random_device rd;
  static std::mt19937 gen(rd());

  std::uniform_int_distribution<T> dis{min, max};
  return dis(gen);
}

template <typename PInt>
void TestRange() {
  auto get_next_step = [&]() {
    constexpr auto step =
        static_cast<typename PInt::ValueType>(PInt::kUpper / 1000);
    if constexpr (step == 0 || step == 1) {
      return typename PInt::ValueType{1};
    } else {
      return GetRandom<typename PInt::ValueType>(0, step);
    }
  };

  for (auto i = get_next_step(); i < PInt::kUpper; i += get_next_step()) {
    auto p = PInt{i};
    Omstream os{};
    p.Serialize(os.stream());

    auto s = Format("Test on value {}, serialized as hex_arr:{}",
                    static_cast<std::uint64_t>(i), print(os.data()));
    UNITY_SET_DETAIL(s.c_str());

    Imstream is{os.data()};
    auto des_p = PInt{};
    auto res = des_p.Deserialize(is.stream());
    TEST_ASSERT_EQUAL(res, PackedDeserializeRes::kFinished);
    TEST_ASSERT_EQUAL(static_cast<typename PInt::ValueType>(p),
                      static_cast<typename PInt::ValueType>(des_p));
  }
}

template <typename PInt>
void TestValueToSize(typename PInt::ValueType value,
                     std::size_t expected_size) {
  auto p = PInt{value};
  Omstream os{};
  p.Serialize(os.stream());

  auto s = Format("Test on value {}, serialized as hex_arr:{} expected size:{}",
                  static_cast<std::uint64_t>(value), print(os.data()),
                  expected_size);
  UNITY_SET_DETAIL(s.c_str());

  TEST_ASSERT_EQUAL(expected_size, os.data().size());
  Imstream is{os.data()};
  auto des_p = PInt{};
  auto res = des_p.Deserialize(is.stream());
  TEST_ASSERT_EQUAL(res, PackedDeserializeRes::kFinished);
  TEST_ASSERT_EQUAL(static_cast<typename PInt::ValueType>(p),
                    static_cast<typename PInt::ValueType>(des_p));
}

void test_StorePackedInt250() {
  using P8 = Packed<std::uint8_t, std::uint8_t, 250>;

  TEST_MESSAGE("one byte packed");
  TestValueToSize<P8>(0, 1);
  TestValueToSize<P8>(250, 1);
  TestRange<P8>();

  using P16 = Packed<std::uint16_t, std::uint8_t, 250>;
  TEST_MESSAGE("two byte packed");
  TestValueToSize<P16>(0, 1);
  TestValueToSize<P16>(250, 1);
  TestValueToSize<P16>(251, 2);
  TestValueToSize<P16>(255, 2);
  TestValueToSize<P16>(1456, 2);
  TestValueToSize<P16>(1514, 2);
  TestRange<P16>();

  using P32 = Packed<std::uint32_t, std::uint8_t, 250>;
  TEST_MESSAGE("four byte packed");
  TestValueToSize<P32>(251, 2);
  TestValueToSize<P32>(1500, 2);
  TestValueToSize<P32>(2000, 4);
  TestValueToSize<P32>(64512, 4);
  TestValueToSize<P32>(P32::kUpper - 1, 4);
  TestRange<P32>();

  using P64 = Packed<std::uint64_t, std::uint8_t, 250>;
  TEST_MESSAGE("eight byte packed");
  TestValueToSize<P64>(154, 1);
  TestValueToSize<P64>(64511, 4);
  TestValueToSize<P64>(P32::kUpper - 1, 4);
  TestValueToSize<P64>(P32::kUpper + 1, 8);
  TestValueToSize<P64>(P64::kUpper - 1, 8);
  TestRange<P64>();
}

void test_StoreOneByteMin() {
  using P16_128 = Packed<std::uint16_t, std::uint8_t, 127>;
  constexpr auto upper_128 = P16_128::kUpper;
  TestValueToSize<P16_128>(0, 1);
  TestValueToSize<P16_128>(127, 1);
  TestValueToSize<P16_128>(128, 2);
  TestValueToSize<P16_128>(128, 2);
  TestValueToSize<P16_128>(250, 2);
  TestValueToSize<P16_128>(1200, 2);
  TestValueToSize<P16_128>(1515, 2);
  TestValueToSize<P16_128>(20000, 2);
  TestValueToSize<P16_128>(32879, 2);
  TestRange<P16_128>();

  using P16_255 = Packed<std::uint16_t, std::uint8_t, 254>;
  constexpr auto upper_255 = P16_255::kUpper;
  TestValueToSize<P16_255>(0, 1);
  TestValueToSize<P16_255>(127, 1);
  TestValueToSize<P16_255>(250, 1);
  TestValueToSize<P16_255>(254, 1);
  TestValueToSize<P16_255>(255, 2);
  TestValueToSize<P16_255>(256, 2);
  TestValueToSize<P16_255>(400, 2);
  TestValueToSize<P16_255>(494, 2);
  TestRange<P16_255>();

  using P16_2 = Packed<std::uint16_t, std::uint8_t, 1>;
  constexpr auto upper_2 = P16_2::kUpper;
  TestValueToSize<P16_2>(0, 1);
  TestValueToSize<P16_2>(1, 1);
  TestValueToSize<P16_2>(2, 2);
  TestValueToSize<P16_2>(20, 2);
  TestValueToSize<P16_2>(128, 2);
  TestValueToSize<P16_2>(255, 2);
  TestValueToSize<P16_2>(1500, 2);
  TestValueToSize<P16_2>(65009, 2);
  TestRange<P16_2>();
}

void test_StoreTwoBytesMin() {
  using P32_32768 = Packed<std::uint32_t, std::uint16_t, 32767>;
  constexpr auto upper_32768 = P32_32768::kUpper;
  TestValueToSize<P32_32768>(0, 2);
  TestValueToSize<P32_32768>(1200, 2);
  TestValueToSize<P32_32768>(1548, 2);
  TestValueToSize<P32_32768>(20000, 2);
  TestValueToSize<P32_32768>(32767, 2);
  TestValueToSize<P32_32768>(64000, 4);
  TestValueToSize<P32_32768>(65535, 4);
  TestValueToSize<P32_32768>(2000000, 4);
  TestValueToSize<P32_32768>(2100000100, 4);
  TestRange<P32_32768>();

  using P32_65531 = Packed<std::uint32_t, std::uint16_t, 65530>;
  constexpr auto upper_65531 = P32_65531::kUpper;
  TestValueToSize<P32_65531>(0, 2);
  TestValueToSize<P32_65531>(1598, 2);
  TestValueToSize<P32_65531>(20000, 2);
  TestValueToSize<P32_65531>(32767, 2);
  TestValueToSize<P32_65531>(64000, 2);
  TestValueToSize<P32_65531>(65530, 2);
  TestValueToSize<P32_65531>(65535, 4);
  TestValueToSize<P32_65531>(300000, 4);
  TestValueToSize<P32_65531>(392954, 4);
  TestRange<P32_65531>();
}

void test_StoreFourBytesMin() {
  using P64_0xfffffffb = Packed<std::uint64_t, std::uint32_t, 0xfffffffa>;
  constexpr auto upper_0xfffffffb = P64_0xfffffffb::kUpper;
  TestValueToSize<P64_0xfffffffb>(0, 4);
  TestValueToSize<P64_0xfffffffb>(0xffff, 4);
  TestValueToSize<P64_0xfffffffb>(0xfffffffa, 4);
  TestValueToSize<P64_0xfffffffb>(0xfffffffb, 8);
  TestValueToSize<P64_0xfffffffb>(0xffffffff, 8);
  TestValueToSize<P64_0xfffffffb>(0x4fffefffa, 8);
  TestRange<P64_0xfffffffb>();

  using P64_0x10000000 = Packed<std::uint64_t, std::uint32_t, 0x0fffffff>;
  constexpr auto upper_0x10000000 = P64_0x10000000::kUpper;
  TestValueToSize<P64_0x10000000>(0, 4);
  TestValueToSize<P64_0x10000000>(0xffff, 4);
  TestValueToSize<P64_0x10000000>(0x0fffffff, 4);
  TestValueToSize<P64_0x10000000>(0x10000000, 8);
  TestValueToSize<P64_0x10000000>(0xffffffff, 8);
  TestValueToSize<P64_0x10000000>(0xffffffffff, 8);
  TestRange<P64_0x10000000>();
}
}  // namespace ae::test_packet_int

int main() {
  UNITY_BEGIN();

  RUN_TEST(ae::test_packet_int::test_StorePackedInt250);
  RUN_TEST(ae::test_packet_int::test_StoreOneByteMin);
  RUN_TEST(ae::test_packet_int::test_StoreTwoBytesMin);
  RUN_TEST(ae::test_packet_int::test_StoreFourBytesMin);

  return UNITY_END();
}
