#include <unity.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <variant>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/ae_actions/query_peer_online_schedule.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/api_protocol/request_id.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/types/uid.h"
#include "aether/vector_buffer.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"
#include "aether/work_cloud_api/work_server_api/server_api_by_uid.h"

#include "assert_packet.h"

namespace ae::test_peer_online_schedule {

void test_WireMethodIds() {
  TEST_ASSERT_EQUAL_UINT8(5, 5);   // AuthorizedApi.client
  TEST_ASSERT_EQUAL_UINT8(13, 13); // ServerApiByUid.onlineTime
  TEST_ASSERT_EQUAL_UINT8(18, 18); // ServerApiByUid.nextOnlineTime
}

void test_PackClientOpensServerApiByUid() {
  ProtocolContext pc;
  AuthorizedApi api{pc};
  auto uid = Uid{};
  uid.value.fill(0x11);

  auto call = ApiContext{api};
  call->client(uid, SubApi<ServerApiByUid>{[](ApiContext<ServerApiByUid>& child) {
                 (void)child->online_time();
                 (void)child->next_online_time();
               }});
  DataBuffer packet = std::move(call);

  AssertPacket(packet, MessageId{5}, uid, Skip<PackedSize>{}, MessageId{13},
               Skip<RequestId>{}, MessageId{18}, Skip<RequestId>{});
}

void test_DateZeroIsUnknownNextOnline() {
  TEST_ASSERT_FALSE(ApiDateMillisToOptional(0).has_value());
  TEST_ASSERT_TRUE(ApiDateMillisToOptional(6'000).has_value());
  auto const tp = ApiDateMillisToTimePoint(1'700'000'000'000);
  TEST_ASSERT_EQUAL_INT64(
      1'700'000'000'000,
      std::chrono::duration_cast<std::chrono::milliseconds>(
          tp.time_since_epoch())
          .count());
}

void test_DateMillisIsJavaEpochMillis() {
  // java.util.Date wire is signed int64 epoch milliseconds (writeLong).
  std::int64_t const millis = 1'700'000'000'000;
  std::vector<std::uint8_t> packed;
  {
    auto archive = ae::seri::BinaryArchive{
        ae::VectorBuffer<ae::PackedSize>{packed},
    };
    archive.Save(millis);
  }
  TEST_ASSERT_EQUAL_UINT(8, packed.size());
  std::int64_t decoded = 0;
  {
    auto archive = ae::seri::BinaryArchive{
        ae::VectorBuffer<ae::PackedSize>{packed},
    };
    archive.Load(decoded);
  }
  TEST_ASSERT_EQUAL_INT64(millis, decoded);
}

void test_MainServerPolicyAndUnsupportedMethod() {
  RequestPolicy::Variant policy = RequestPolicy::MainServer{};
  TEST_ASSERT_TRUE(std::holds_alternative<RequestPolicy::MainServer>(policy));
  TEST_ASSERT_EQUAL_INT(
      3, static_cast<int>(
             QueryPeerOnlineScheduleError::kNextOnlineTimeUnsupported));
}

}  // namespace ae::test_peer_online_schedule

int test_peer_online_schedule() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_peer_online_schedule::test_WireMethodIds);
  RUN_TEST(ae::test_peer_online_schedule::test_PackClientOpensServerApiByUid);
  RUN_TEST(ae::test_peer_online_schedule::test_DateZeroIsUnknownNextOnline);
  RUN_TEST(ae::test_peer_online_schedule::test_DateMillisIsJavaEpochMillis);
  RUN_TEST(ae::test_peer_online_schedule::test_MainServerPolicyAndUnsupportedMethod);
  return UNITY_END();
}
