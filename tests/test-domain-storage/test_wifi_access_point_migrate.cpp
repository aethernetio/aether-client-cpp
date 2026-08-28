/*
 * Copyright 2026 Aethernet Inc.
 *
 * WifiAccessPoint v0→v1 migration: discard cached BSSID/channel.
 */
#include <unity.h>

#include <cstdint>
#include <optional>
#include <type_traits>

#include "aether/access_points/wifi_access_point.h"
#include "aether/clock.h"
#include "aether/config.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/domain.h"
#include "aether/obj/obj_id.h"
#include "aether/wifi/wifi_driver.h"
#include "aether/wifi/wifi_driver_types.h"

namespace ae::test_wifi_access_point_migrate {

#if AE_SUPPORT_WIFIS

void test_ConnectHasNoBssidParameter() {
  using ConnectFn =
      void (WifiDriver::*)(WiFiAp const&,
                           std::optional<WiFiPowerSaveParam> const&);
  ConnectFn const fn = &WifiDriver::Connect;
  TEST_ASSERT_NOT_NULL(reinterpret_cast<void const*>(
      *reinterpret_cast<void const* const*>(&fn)));

  Result<WifiDriver::ConnectOk, int> const ok{Ok{WifiDriver::ConnectOk{}}};
  TEST_ASSERT_TRUE(static_cast<bool>(ok));
  TEST_ASSERT_EQUAL_UINT32(1, WifiAccessPoint::kVersion);
}

void test_MigrateFromVersion0Only() {
  RamDomainStorage storage;
  Domain domain{ae::Now(), storage};

  WiFiAp wifi_ap{};
  wifi_ap.creds.ssid = "ssid-migrate";
  wifi_ap.creds.password = "pw";

  {
    auto ap = WifiAccessPoint::ptr::Create(
        CreateWith{domain}.with_id(42), ObjPtr<Aether>{}, ObjPtr<WifiAdapter>{},
        ObjPtr<IPoller>{}, ObjPtr<DnsResolver>{}, wifi_ap, std::nullopt);
    ap.Save();
  }

  // Simulate device that only has legacy version-0 blob.
  auto& class_data = storage.state.at(ObjId{42}).value();
  class_data.at(WifiAccessPoint::kClassId).erase(1);

  {
    auto ap = WifiAccessPoint::ptr::Declare(CreateWith{domain}.with_id(42));
    bool loaded = false;
    bool has_config = true;
    ap.WithLoaded([&](auto const& p) {
      loaded = static_cast<bool>(p);
      TEST_ASSERT(p);
      // Version 0 load intentionally skips legacy BSSID blob.
      has_config = p->HasWifiConfig();
    });
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_FALSE(has_config);
    ap.Save();
  }

  TEST_ASSERT_EQUAL(
      DomainLoadResult::kLoaded,
      storage.Load({ObjId{42}, WifiAccessPoint::kClassId, 1}).result);
}

void test_Version0And1BothSaved() {
  RamDomainStorage storage;
  Domain domain{ae::Now(), storage};

  WiFiAp wifi_ap{};
  wifi_ap.creds.ssid = "a";
  wifi_ap.creds.password = "b";

  auto ap = WifiAccessPoint::ptr::Create(
      CreateWith{domain}.with_id(7), ObjPtr<Aether>{}, ObjPtr<WifiAdapter>{},
      ObjPtr<IPoller>{}, ObjPtr<DnsResolver>{}, wifi_ap, std::nullopt);
  ap.Save();

  TEST_ASSERT_EQUAL(
      DomainLoadResult::kLoaded,
      storage.Load({ObjId{7}, WifiAccessPoint::kClassId, 0}).result);
  TEST_ASSERT_EQUAL(
      DomainLoadResult::kLoaded,
      storage.Load({ObjId{7}, WifiAccessPoint::kClassId, 1}).result);
}

#else

void test_ConnectHasNoBssidParameter() {
  TEST_IGNORE_MESSAGE("AE_SUPPORT_WIFIS=0");
}
void test_MigrateFromVersion0Only() { TEST_IGNORE_MESSAGE("AE_SUPPORT_WIFIS=0"); }
void test_Version0And1BothSaved() { TEST_IGNORE_MESSAGE("AE_SUPPORT_WIFIS=0"); }

#endif

}  // namespace ae::test_wifi_access_point_migrate

int test_wifi_access_point_migrate() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_wifi_access_point_migrate::test_ConnectHasNoBssidParameter);
  RUN_TEST(ae::test_wifi_access_point_migrate::test_MigrateFromVersion0Only);
  RUN_TEST(ae::test_wifi_access_point_migrate::test_Version0And1BothSaved);
  return UNITY_END();
}
