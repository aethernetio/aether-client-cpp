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

// Copyright 2016 Aether authors. All Rights Reserved.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//   http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================

#ifndef AETHER_MSTREAM_H_
#define AETHER_MSTREAM_H_

#include <map>
#include <list>
#include <deque>
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <cstdint>
#include <cstring>
#include <utility>
#include <type_traits>
#include <unordered_map>

#include "aether/reflect/reflect.h"
#include "aether/reflect/domain_visitor.h"

namespace ae {

// Base classes to simplify std::conditional checks in serialization functions.
class stream {};
class ostream : public stream {};
class istream : public stream {};

/**
 * struct OBuffer {
 * using size_type = std::uint32_t;
 * size_t write(void* data, size_t size);
 * };
 */

template <typename OBuffer>
class omstream : public ostream {
 public:
  OBuffer& ob_;

  omstream(OBuffer& out_buffer) : ob_{out_buffer} {}

  void write(const void* data, size_t size) { ob_.write(data, size); }
};

enum class ReadResult {
  kNo,
  kYes,
};

/**
 * struct IBuffer {
 * using size_type = std::uint32_t;
 * size_t read(void* data, size_t size, size_t minimum_size);
 * ReadResult result() const;
 * void result(ReadResult);
 };
 */

template <typename IBuffer>
class imstream : public istream {
 public:
  IBuffer& ib_;

  imstream(IBuffer& input_buffer) : ib_{input_buffer} {}

  void read(void* data, size_t size, size_t minimum_size = 1) {
    ib_.read(data, size, minimum_size);
  }

  ReadResult result() const { return ib_.result(); }
  void result(ReadResult result) { ib_.result(result); }
};

template <typename TStream>
inline bool data_was_read(TStream& /* is */) {
  return true;
}

template <typename Ib>
inline bool data_was_read(imstream<Ib>& is) {
  return is.result() == ReadResult::kYes;
}

template <typename TStream>
inline bool data_was_written(TStream& /* s */) {
  return true;
}

template <bool condition, typename T>
struct omstream_enable_if : std::enable_if<condition, omstream<T>&> {};

template <bool condition, typename T>
using omstream_enable_if_t = typename omstream_enable_if<condition, T>::type;

template <bool condition, typename T>
struct imstream_enable_if : std::enable_if<condition, imstream<T>&> {};

template <bool condition, typename T>
using imstream_enable_if_t = typename imstream_enable_if<condition, T>::type;

template <typename T, typename Ob, typename Enable = void>
struct has_omstream : std::false_type {};

template <typename T, typename Ob>
struct has_omstream<T, Ob,
                    std::void_t<decltype(std::declval<omstream<Ob>&>()
                                         << std::declval<T const&>())>>
    : std::true_type {};

template <typename T, typename Ib, typename Enable = void>
struct has_imstream : std::false_type {};

template <typename T, typename Ib>
struct has_imstream<
    T, Ib,
    std::void_t<decltype(std::declval<imstream<Ib>&>() >> std::declval<T&>())>>
    : std::true_type {};

/*********************** operator << implementation **************** */

template <typename T, typename Ob>
omstream_enable_if_t<!std::is_enum<T>::value && std::is_arithmetic<T>::value,
                     Ob>
operator<<(omstream<Ob>& s, const T& v) {
  s.write(&v, sizeof(T));
  return s;
}

template <typename T, typename Ob>
omstream_enable_if_t<std::is_enum_v<T>, Ob> operator<<(omstream<Ob>& s,
                                                       const T& v) {
  using Type = typename std::underlying_type<T>::type;
  auto t = static_cast<Type>(v);
  s << t;
  return s;
}

template <typename T, typename Ib>
imstream_enable_if_t<!std::is_enum<T>::value && std::is_arithmetic<T>::value,
                     Ib>
operator>>(imstream<Ib>& s, T& v) {
  s.read(&v, sizeof(T));
  return s;
}

template <typename T, typename Ib>
imstream_enable_if_t<std::is_enum<T>::value, Ib> operator>>(imstream<Ib>& s,
                                                            T& v) {
  using Type = typename std::underlying_type<T>::type;
  Type t;
  s >> t;
  if (s.result() == ReadResult::kYes) {
    v = static_cast<T>(t);
  }
  return s;
}

template <typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const char* t) {
  auto length = std::strlen(t);
  s << static_cast<typename Ob::size_type>(length);
  s.write(t, length);
  return s;
}

template <typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const std::string& t) {
  s << static_cast<typename Ob::size_type>(t.size());
  s.write(t.data(), t.size());
  return s;
}

template <typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, std::string& t) {
  typename Ib::size_type size;
  s >> size;
  if (data_was_read(s)) {
    t.resize(static_cast<size_t>(size));
    s.read(t.data(), static_cast<size_t>(size));
  }
  return s;
}

template <typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const std::string_view& t) {
  s << static_cast<typename Ob::size_type>(t.size());
  s.write(t.data(), t.size());
  return s;
}

template <typename T, typename Ob>
omstream_enable_if_t<std::is_scalar_v<T>, Ob> operator<<(
    omstream<Ob>& s, const std::vector<T>& t) {
  s << static_cast<typename Ob::size_type>(t.size());
  s.write(reinterpret_cast<uint8_t const*>(t.data()), t.size() * sizeof(T));
  return s;
}

template <typename T, typename Ib>
imstream_enable_if_t<std::is_scalar_v<T>, Ib> operator>>(imstream<Ib>& s,
                                                         std::vector<T>& t) {
  typename Ib::size_type size;
  s >> size;
  if (data_was_read(s)) {
    t.resize(static_cast<size_t>(size));
    s.read(reinterpret_cast<uint8_t*>(t.data()), t.size() * sizeof(T));
  }
  return s;
}

template <typename T, typename Ob>
omstream_enable_if_t<!std::is_scalar_v<T>, Ob> operator<<(
    omstream<Ob>& s, const std::vector<T>& t) {
  s << static_cast<typename Ob::size_type>(t.size());
  for (const T& v : t) {
    s << v;
  }
  return s;
}

template <typename T, typename Ib>
imstream_enable_if_t<!std::is_scalar_v<T>, Ib> operator>>(imstream<Ib>& s,
                                                          std::vector<T>& t) {
  typename Ib::size_type size;
  s >> size;
  if (!data_was_read(s)) {
    return s;
  }
  t.resize(static_cast<std::size_t>(size));
  for (auto& v : t) {
    s >> v;
    if (!data_was_read(s)) {
      break;
    }
  }
  return s;
}

template <size_t N, typename T, typename Ob>
omstream_enable_if_t<std::is_scalar_v<T>, Ob> operator<<(omstream<Ob>& s,
                                                         T const (&t)[N]) {
  s.write(reinterpret_cast<uint8_t const*>(t), N * sizeof(T));
  return s;
}

template <size_t N, typename T, typename Ib>
imstream_enable_if_t<std::is_scalar_v<T>, Ib> operator>>(imstream<Ib>& s,
                                                         T (&t)[N]) {
  s.read(reinterpret_cast<uint8_t*>(t), N * sizeof(T));
  return s;
}

template <size_t N, typename T, typename Ob>
omstream_enable_if_t<std::is_scalar_v<T>, Ob> operator<<(
    omstream<Ob>& s, const std::array<T, N>& t) {
  s.write(reinterpret_cast<uint8_t const*>(t.data()), t.size() * sizeof(T));
  return s;
}

template <size_t N, typename T, typename Ib>
imstream_enable_if_t<std::is_scalar_v<T>, Ib> operator>>(imstream<Ib>& s,
                                                         std::array<T, N>& t) {
  s.read(reinterpret_cast<uint8_t*>(t.data()), t.size() * sizeof(T));
  return s;
}

template <size_t N, typename T, typename Ob>
omstream_enable_if_t<!std::is_scalar_v<T>, Ob> operator<<(
    omstream<Ob>& s, const std::array<T, N>& t) {
  for (const T& v : t) {
    s << v;
  }
  return s;
}

template <size_t N, typename T, typename Ib>
imstream_enable_if_t<!std::is_scalar_v<T>, Ib> operator>>(imstream<Ib>& s,
                                                          std::array<T, N>& t) {
  for (auto& v : t) {
    s >> v;
    if (!data_was_read(s)) {
      break;
    }
  }
  return s;
}

template <typename T1, typename T2, typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const std::map<T1, T2>& t) {
  s << static_cast<typename Ob::size_type>(t.size());
  for (const auto& i : t) {
    s << i;
  }
  return s;
}
template <typename T1, typename T2, typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const std::unordered_map<T1, T2>& t) {
  s << static_cast<typename Ob::size_type>(t.size());
  for (const auto& i : t) {
    s << i;
  }
  return s;
}

template <typename T1, typename T2, typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, std::map<T1, T2>& t) {
  typename Ib::size_type size{};
  s >> size;
  if (!data_was_read(s)) {
    return s;
  }
  t.clear();
  for (uint32_t i = 0; i < static_cast<size_t>(size); i++) {
    std::pair<T1, T2> kv;
    s >> kv;
    if (!data_was_read(s)) {
      break;
    }
    t.emplace(std::move(kv));
  }
  return s;
}

template <typename T1, typename T2, typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, std::unordered_map<T1, T2>& t) {
  typename Ib::size_type size{};
  s >> size;
  if (!data_was_read(s)) {
    return s;
  }
  t.clear();
  t.reserve(static_cast<size_t>(size));
  for (auto i = 0; i < static_cast<size_t>(size); ++i) {
    std::pair<T1, T2> kv;
    s >> kv;
    if (!data_was_read(s)) {
      break;
    }
    t.emplace(std::move(kv));
  }
  return s;
}

template <typename T1, typename T2, typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const std::pair<T1, T2>& t) {
  s << t.first << t.second;
  return s;
}
template <typename T1, typename T2, typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, std::pair<T1, T2>& t) {
  s >> t.first >> t.second;
  return s;
}

template <typename T, typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const std::deque<T>& t) {
  s << static_cast<typename Ob::size_type>(t.size());
  for (const auto& v : t) {
    s << v;
  }
  return s;
}
template <typename T, typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, std::deque<T>& t) {
  typename Ib::size_type size;
  s >> size;

  if (!data_was_read(s)) {
    return s;
  }
  t.resize(static_cast<size_t>(size));

  for (auto& v : t) {
    s >> v;
    if (!data_was_read(s)) {
      break;
    }
  }
  return s;
}

template <typename T, typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const std::list<T>& t) {
  s << static_cast<typename Ob::size_type>(t.size());
  for (const auto& i : t) {
    s << i;
  }
  return s;
}

template <typename T, typename Ib>
imstream_enable_if_t<std::is_default_constructible_v<T>, Ib>& operator>>(
    imstream<Ib>& s, std::list<T>& t) {
  typename Ib::size_type size;
  s >> size;
  if (!data_was_read(s)) {
    return s;
  }
  t.clear();
  for (uint32_t i = 0; i < size; i++) {
    T v;
    s >> v;
    if (!data_was_read(s)) {
      break;
    }
    t.push_back(std::move(v));
  }
  return s;
}

template <typename... T, typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s,
                         std::chrono::duration<T...> const& val) {
  s << static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(val).count());
  return s;
}

template <typename... T, typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, std::chrono::duration<T...>& val) {
  std::uint32_t temp;
  s >> temp;
  val = std::chrono::duration_cast<std::chrono::duration<T...>>(
      std::chrono::microseconds{
          static_cast<std::chrono::microseconds::rep>(temp)});
  return s;
}

template <typename... T, typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s,
                         std::chrono::time_point<T...> const& t) {
  auto d = std::chrono::duration_cast<std::chrono::microseconds>(
      t.time_since_epoch());
  s << static_cast<uint64_t>(d.count());
  return s;
}

template <typename... T, typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, std::chrono::time_point<T...>& t) {
  std::uint64_t tp;
  s >> tp;
  if (!data_was_read(s)) {
    return s;
  }
  t = std::chrono::time_point<T...>(std::chrono::microseconds(tp));
  return s;
}

template <typename T, typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const std::unique_ptr<T>& v) {
  if (v) {
    s << true;
    s << *v;
  } else {
    s << false;
  }
  return s;
}

template <typename T, typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const std::shared_ptr<T>& v) {
  if (v) {
    s << true;
    s << *v;
  } else {
    s << false;
  }
  return s;
}

template <typename T, typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, std::unique_ptr<T>& v) {
  bool has_value{};
  s >> has_value;
  if (!data_was_read(s)) {
    return s;
  }
  if (has_value) {
    auto temp = std::make_unique<T>();
    s >> *temp;
    if (data_was_read(s)) {
      v = std::move(temp);
    }
  }
  return s;
}

template <typename T, typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, std::shared_ptr<T>& v) {
  bool has_value{};
  s >> has_value;
  if (!data_was_read(s)) {
    return s;
  }
  if (has_value) {
    auto temp = std::make_unique<T>();
    s >> *temp;
    if (data_was_read(s)) {
      v = std::move(temp);
    }
  }
  return s;
}

template <typename T, typename Ob>
omstream_enable_if_t<has_omstream<T, Ob>::value, Ob> operator<<(
    omstream<Ob>& s, T const* const& v) {
  if (v != nullptr) {
    s << *v;
  }
  return s;
}

template <typename T, typename Ib>
imstream_enable_if_t<has_imstream<T, Ib>::value, Ib> operator>>(imstream<Ib>& s,
                                                                T* const& v) {
  if (v != nullptr) {
    s >> *v;
  }
  return s;
}

template <typename T, typename Ib>
std::enable_if_t<reflect::IsReflectable<T>::value, imstream<Ib>&> operator>>(
    imstream<Ib>& s, T& t) {
  auto reflection = reflect::Reflection{t};
  reflection.Apply([&s](auto&... val) { ((s >> val), ...); });
  return s;
}

template <typename T, typename Ob>
std::enable_if_t<reflect::IsReflectable<T>::value, omstream<Ob>&> operator<<(
    omstream<Ob>& s, T const& t) {
  auto reflection = reflect::Reflection{t};
  reflection.Apply([&s](auto const&... val) { ((s << val), ...); });
  return s;
}
}  // namespace ae

#endif  // AETHER_MSTREAM_H_
