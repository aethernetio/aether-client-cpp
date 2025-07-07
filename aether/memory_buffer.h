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

#ifndef AETHER_MEMORY_BUFFER_H_
#define AETHER_MEMORY_BUFFER_H_

#include <ios>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <streambuf>

namespace ae {
template <typename CharT = char, typename Traits = std::char_traits<CharT>,
          std::streamsize BLOCK_SIZE = 1024>
class MemStreamBuf : public std::basic_streambuf<CharT, Traits> {
 public:
  using streambuf = std::basic_streambuf<CharT, Traits>;
  using char_type = typename streambuf::char_type;
  using int_type = typename streambuf::int_type;
  using traits_type = typename streambuf::traits_type;
  using pos_type = typename streambuf::pos_type;
  using off_type = typename streambuf::off_type;

  static constexpr std::streamsize kBlockSize = BLOCK_SIZE;

 private:
  char_type* membuf_{nullptr};

 public:
  MemStreamBuf() { SetCapacity(kBlockSize); }
  explicit MemStreamBuf(std::streamsize capacity) {
    SetExactCapacity(capacity);
  }
  MemStreamBuf(MemStreamBuf const&) = delete;
  MemStreamBuf(MemStreamBuf&& other) noexcept : membuf_{other.membuf_} {
    this->swap(other);
    other.membuf_ = nullptr;
  }

  virtual ~MemStreamBuf() {
    if (membuf_) {
      std::free(membuf_);
      membuf_ = nullptr;
    }
  }

  /**
   * \brief Changes the buffer capacity to value multiple to the block size.
   */
  constexpr char_type* SetCapacity(std::streamsize new_size) noexcept {
    // size must be a multiple of BLOCK_SIZE
    new_size = new_size ? (((new_size - 1) / kBlockSize + 1) * kBlockSize) : 0;
    return SetExactCapacity(new_size);
  }

  char_type* SetExactCapacity(std::streamsize new_size) noexcept {
    // minimum size is one byte
    if (new_size <= 0) {
      return nullptr;
    }
    auto* new_buf = std::realloc(membuf_, static_cast<size_t>(new_size));
    if (new_buf == nullptr) {
      return nullptr;
    }
    membuf_ = static_cast<char_type*>(new_buf);

    auto cur_size = Size();
    // set pbase, pptr and epptr to new buffer
    streambuf::setp(membuf_, membuf_ + new_size);
    // advance pptr to old position or end of buffer is smaller
    streambuf::pbump(static_cast<int>(std::min(cur_size, new_size)));
    // update get area pointers
    auto cur_pos = streambuf::gptr() - streambuf::eback();
    streambuf::setg(streambuf::pbase(), streambuf::pbase() + cur_pos,
                    streambuf::pptr());
    return membuf_;
  }

  constexpr char_type* ShrinkToFit() { return SetExactCapacity(Size()); }

  constexpr char_type* Data() const { return membuf_; }

  constexpr std::streamsize Size() const {
    return streambuf::pptr() - streambuf::pbase();
  }

  constexpr std::streamsize Capacity() const {
    return streambuf::epptr() - streambuf::pbase();
  }

  // Available capacity to write
  constexpr std::streamsize AvailableCapacity() const {
    return streambuf::epptr() - streambuf::pptr();
  }

  // Available data size to read
  constexpr std::streamsize AvailableData() const {
    return streambuf::egptr() - streambuf::gptr();
  }

  /**
   * \brief Implements std::basic_streambuf::xsputn
   */
  std::streamsize xsputn(const char_type* s, std::streamsize count) override {
    if (count > AvailableCapacity()) {
      SetCapacity(Size() + count);
    }

    auto written = streambuf::xsputn(s, count);
    streambuf::setg(streambuf::eback(), streambuf::gptr(), streambuf::pptr());

    return written;
  }

  /**
   * \brief Implements std::basic_streambuf::overflow
   */
  int_type overflow(int_type ch) override {
    // try to increase the size by one block
    if (!SetCapacity(Size() +
                     static_cast<std::streamsize>(sizeof(char_type)))) {
      // size increase did not work, return eof
      return traits_type::eof();
    }
    // put char if not eof
    if (!traits_type::eq_int_type(ch, traits_type::eof())) {
      char_type ch_char = traits_type::to_char_type(ch);
      xsputn(&ch_char, 1);
    }
    return traits_type::not_eof(ch);
  }

  // Implements std::basic_streambuf::showmanyc
  std::streamsize showmanyc() override { return -1; }

  /**
   * \brief Implements std::basic_streambuf::setbuf
   * \param[in] s - new buffer, must be allocated with std::malloc, MemStreamBuf
   * owns this new buffer.
   */
  MemStreamBuf* setbuf(char_type* s, std::streamsize size) override {
    if (membuf_) {
      std::free(membuf_);
    }
    membuf_ = s;
    streambuf::setp(membuf_, membuf_ + size);
    return this;
  }

  /**
   * \brief Implements std::basic_streambuf::seekpos
   */
  pos_type seekpos(pos_type sp, std::ios_base::openmode mode) override {
    if (mode == std::ios_base::in) {
      streambuf::setg(membuf_, membuf_ + static_cast<std::size_t>(sp),
                      membuf_ + static_cast<std::size_t>(Size()));
    } else if (mode == std::ios_base::out) {
      streambuf::setp(membuf_, membuf_ + static_cast<std::size_t>(sp));
    }
    return sp;
  }

 private:
};
}  // namespace ae
#endif  // AETHER_MEMORY_BUFFER_H_ */
