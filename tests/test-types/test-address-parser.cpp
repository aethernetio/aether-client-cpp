/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/types/address_parser.h"

namespace ae::test_address_parser {
void test_ParseIpv4() {
  constexpr auto addr = AddressParser::StringToAddress("127.0.0.1");
  static_assert(addr.Index() == AddrVersion::kIpV4);
  constexpr auto ipv4_addr = addr.Get<IpV4Addr>();
  static_assert(ipv4_addr.ipv4_value[0] == 127);
  static_assert(ipv4_addr.ipv4_value[1] == 0);
  static_assert(ipv4_addr.ipv4_value[2] == 0);
  static_assert(ipv4_addr.ipv4_value[3] == 1);

  auto addr2 = AddressParser::StringToAddress("192.168.1.19");
  TEST_ASSERT_EQUAL(AddrVersion::kIpV4, addr2.Index());
  TEST_ASSERT_EQUAL(192, addr2.Get<IpV4Addr>().ipv4_value[0]);
  TEST_ASSERT_EQUAL(19, addr2.Get<IpV4Addr>().ipv4_value[3]);

  // invalid format
  auto addr3 = AddressParser::StringToAddress("192.168.1.19.24");
  TEST_ASSERT_NOT_EQUAL(AddrVersion::kIpV4, addr3.Index());
}

void test_ParseIpv6() {
  constexpr auto addr =
      AddressParser::StringToAddress("2001:0db8:0000:0000:0000:0000:0000:0001");
  static_assert(addr.Index() == AddrVersion::kIpV6);
  constexpr auto ipv6addr = addr.Get<IpV6Addr>();
  static_assert(0x20 == ipv6addr.ipv6_value[0]);
  static_assert(0x01 == ipv6addr.ipv6_value[1]);
  static_assert(0x0d == ipv6addr.ipv6_value[2]);
  static_assert(0xb8 == ipv6addr.ipv6_value[3]);
  // ...
  static_assert(0x00 == ipv6addr.ipv6_value[14]);
  static_assert(0x01 == ipv6addr.ipv6_value[15]);

  auto addr2 = AddressParser::StringToAddress("2001:db8:0:0:0:0:0:1");
  TEST_ASSERT_EQUAL(AddrVersion::kIpV6, addr2.Index());
  TEST_ASSERT_EQUAL(0x20, addr2.Get<IpV6Addr>().ipv6_value[0]);
  TEST_ASSERT_EQUAL(0x01, addr2.Get<IpV6Addr>().ipv6_value[1]);
  TEST_ASSERT_EQUAL(0x0d, addr2.Get<IpV6Addr>().ipv6_value[2]);

  auto addr3 = AddressParser::StringToAddress("2001:db8::1");
  TEST_ASSERT_EQUAL(AddrVersion::kIpV6, addr3.Index());
  TEST_ASSERT_EQUAL(0x20, addr3.Get<IpV6Addr>().ipv6_value[0]);
  TEST_ASSERT_EQUAL(0x01, addr3.Get<IpV6Addr>().ipv6_value[1]);
  TEST_ASSERT_EQUAL(0x0d, addr3.Get<IpV6Addr>().ipv6_value[2]);

  // invalid format
  auto addr4 = AddressParser::StringToAddress("2001:db8::2::f2");
  TEST_ASSERT_NOT_EQUAL(AddrVersion::kIpV6, addr4.Index());
}

void test_ParseNamedAddress() {
  constexpr auto addr = AddressParser::StringToAddress("funny.land.com");
  static_assert(AddrVersion::kNamed == addr.Index());
  constexpr auto named_addr = addr.Get<AddressParser::NamedAddrView>();
  static_assert(named_addr.name == "funny.land.com");

  auto addr2 = AddressParser::StringToAddress("example.com");
  TEST_ASSERT_EQUAL(AddrVersion::kNamed, addr2.Index());
  TEST_ASSERT_EQUAL_STRING(
      "example.com", addr2.Get<AddressParser::NamedAddrView>().name.data());

  auto addr3 = AddressParser::StringToAddress("nothing.net");
  TEST_ASSERT_EQUAL(AddrVersion::kNamed, addr3.Index());
  TEST_ASSERT_EQUAL_STRING(
      "nothing.net", addr3.Get<AddressParser::NamedAddrView>().name.data());
}

}  // namespace ae::test_address_parser

int test_address_parser() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_address_parser::test_ParseIpv4);
  RUN_TEST(ae::test_address_parser::test_ParseIpv6);
  RUN_TEST(ae::test_address_parser::test_ParseNamedAddress);
  return UNITY_END();
}
