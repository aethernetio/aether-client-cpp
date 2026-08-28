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

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

#include "aether/tele.h"

#include "crc_map_commit_storage.h"

namespace ae::test_ds_save_transaction {

std::vector<std::uint8_t> DataGetter(
    std::unique_ptr<IDomainStorageReader>& reader) {
  std::size_t size{};
  TEST_ASSERT(reader->Read(ae::seri::SizeReadTag{size}));
  auto res = std::vector<std::uint8_t>(size);
  TEST_ASSERT(reader->Read(ae::seri::DataReadTag{res.data(), res.size()}));
  return res;
}

void WritePayload(IDomainStorage& storage, DomainQuery const& query,
                  std::span<std::uint8_t const> payload) {
  auto writer = storage.Store(query);
  TEST_ASSERT(writer->Write(ae::seri::SizeWriteTag{payload.size()}));
  TEST_ASSERT(
      writer->Write(ae::seri::DataWriteTag{payload.data(), payload.size()}));
  writer.reset();
}

static constexpr auto kClassId = std::uint32_t{100};
static constexpr auto kVersion = std::uint8_t{0};

static constexpr auto kDataA =
    std::array<std::uint8_t, 4>{1, 2, 3, 4};
static constexpr auto kDataB =
    std::array<std::uint8_t, 4>{5, 6, 7, 8};
static constexpr auto kDataC =
    std::array<std::uint8_t, 4>{9, 10, 11, 12};
static constexpr auto kDataD =
    std::array<std::uint8_t, 4>{13, 14, 15, 16};
static constexpr auto kDataE =
    std::array<std::uint8_t, 4>{17, 18, 19, 20};
static constexpr auto kDataA2 =
    std::array<std::uint8_t, 4>{21, 22, 23, 24};

void test_FirstSaveFiveObjectsOneMap() {
  CrcMapCommitStorage storage;
  storage.BeginSaveTransaction();
  WritePayload(storage, {ObjId{1}, kClassId, kVersion}, kDataA);
  WritePayload(storage, {ObjId{2}, kClassId, kVersion}, kDataB);
  WritePayload(storage, {ObjId{3}, kClassId, kVersion}, kDataC);
  WritePayload(storage, {ObjId{4}, kClassId, kVersion}, kDataD);
  WritePayload(storage, {ObjId{5}, kClassId, kVersion}, kDataE);
  storage.EndSaveTransaction();

  TEST_ASSERT_EQUAL_UINT32(5, storage.object_write_count);
  TEST_ASSERT_EQUAL_UINT32(1, storage.map_rewrite_count);
  TEST_ASSERT_EQUAL_UINT32(0, storage.crc_skip_count);
  TEST_ASSERT_TRUE(storage.map_bytes > 0);
}

void test_RepeatSaveUnchangedNoWrites() {
  CrcMapCommitStorage storage;
  storage.BeginSaveTransaction();
  WritePayload(storage, {ObjId{1}, kClassId, kVersion}, kDataA);
  WritePayload(storage, {ObjId{2}, kClassId, kVersion}, kDataB);
  WritePayload(storage, {ObjId{3}, kClassId, kVersion}, kDataC);
  WritePayload(storage, {ObjId{4}, kClassId, kVersion}, kDataD);
  WritePayload(storage, {ObjId{5}, kClassId, kVersion}, kDataE);
  storage.EndSaveTransaction();

  storage.ResetCounters();
  storage.BeginSaveTransaction();
  WritePayload(storage, {ObjId{1}, kClassId, kVersion}, kDataA);
  WritePayload(storage, {ObjId{2}, kClassId, kVersion}, kDataB);
  WritePayload(storage, {ObjId{3}, kClassId, kVersion}, kDataC);
  WritePayload(storage, {ObjId{4}, kClassId, kVersion}, kDataD);
  WritePayload(storage, {ObjId{5}, kClassId, kVersion}, kDataE);
  storage.EndSaveTransaction();

  TEST_ASSERT_EQUAL_UINT32(0, storage.object_write_count);
  TEST_ASSERT_EQUAL_UINT32(0, storage.map_rewrite_count);
  TEST_ASSERT_EQUAL_UINT32(5, storage.crc_skip_count);
}

void test_ChangeOneObjectOneWriteOneMap() {
  CrcMapCommitStorage storage;
  storage.BeginSaveTransaction();
  WritePayload(storage, {ObjId{1}, kClassId, kVersion}, kDataA);
  WritePayload(storage, {ObjId{2}, kClassId, kVersion}, kDataB);
  WritePayload(storage, {ObjId{3}, kClassId, kVersion}, kDataC);
  storage.EndSaveTransaction();

  storage.ResetCounters();
  storage.BeginSaveTransaction();
  WritePayload(storage, {ObjId{1}, kClassId, kVersion}, kDataA);
  WritePayload(storage, {ObjId{2}, kClassId, kVersion}, kDataA2);
  WritePayload(storage, {ObjId{3}, kClassId, kVersion}, kDataC);
  storage.EndSaveTransaction();

  TEST_ASSERT_EQUAL_UINT32(1, storage.object_write_count);
  TEST_ASSERT_EQUAL_UINT32(1, storage.map_rewrite_count);
  TEST_ASSERT_EQUAL_UINT32(2, storage.crc_skip_count);
}

void test_NestedBeginEndOneMapCommit() {
  CrcMapCommitStorage storage;
  storage.BeginSaveTransaction();
  storage.BeginSaveTransaction();
  WritePayload(storage, {ObjId{1}, kClassId, kVersion}, kDataA);
  WritePayload(storage, {ObjId{2}, kClassId, kVersion}, kDataB);
  WritePayload(storage, {ObjId{3}, kClassId, kVersion}, kDataC);
  storage.EndSaveTransaction();
  TEST_ASSERT_EQUAL_UINT32(3, storage.object_write_count);
  TEST_ASSERT_EQUAL_UINT32(0, storage.map_rewrite_count);
  storage.EndSaveTransaction();

  TEST_ASSERT_EQUAL_UINT32(3, storage.object_write_count);
  TEST_ASSERT_EQUAL_UINT32(1, storage.map_rewrite_count);
}

void test_WriteFailureNoCrcNoMap() {
  CrcMapCommitStorage storage;
  storage.BeginSaveTransaction();
  WritePayload(storage, {ObjId{1}, kClassId, kVersion}, kDataA);
  storage.EndSaveTransaction();

  storage.ResetCounters();
  storage.inject_write_failure = true;
  storage.BeginSaveTransaction();
  WritePayload(storage, {ObjId{1}, kClassId, kVersion}, kDataA2);
  storage.EndSaveTransaction();

  TEST_ASSERT_EQUAL_UINT32(0, storage.object_write_count);
  TEST_ASSERT_EQUAL_UINT32(0, storage.map_rewrite_count);
  TEST_ASSERT_EQUAL_UINT32(0, storage.crc_skip_count);

  // CRC was not confirmed: a successful retry still sees the object as changed.
  storage.inject_write_failure = false;
  storage.ResetCounters();
  storage.BeginSaveTransaction();
  WritePayload(storage, {ObjId{1}, kClassId, kVersion}, kDataA2);
  storage.EndSaveTransaction();
  TEST_ASSERT_EQUAL_UINT32(1, storage.object_write_count);
  TEST_ASSERT_EQUAL_UINT32(1, storage.map_rewrite_count);
}

void test_RoundTripLoadFromCommittedState() {
  auto committed = std::make_shared<CrcMapCommitStorage::CommittedState>();
  {
    CrcMapCommitStorage writer{committed};
    writer.BeginSaveTransaction();
    WritePayload(writer, {ObjId{1}, kClassId, kVersion}, kDataA);
    WritePayload(writer, {ObjId{2}, kClassId, kVersion}, kDataB);
    WritePayload(writer, {ObjId{3}, kClassId, kVersion}, kDataC);
    writer.EndSaveTransaction();
    TEST_ASSERT_EQUAL_UINT32(3, writer.object_write_count);
    TEST_ASSERT_EQUAL_UINT32(1, writer.map_rewrite_count);
  }

  // New storage instance (new "Domain" reader path) loads from committed state.
  CrcMapCommitStorage reader{committed};
  {
    auto load = reader.Load({ObjId{1}, kClassId, kVersion});
    TEST_ASSERT(load.result == DomainLoadResult::kLoaded);
    auto data = DataGetter(load.reader);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kDataA.data(), data.data(), kDataA.size());
  }
  {
    auto load = reader.Load({ObjId{2}, kClassId, kVersion});
    TEST_ASSERT(load.result == DomainLoadResult::kLoaded);
    auto data = DataGetter(load.reader);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kDataB.data(), data.data(), kDataB.size());
  }
  {
    auto load = reader.Load({ObjId{3}, kClassId, kVersion});
    TEST_ASSERT(load.result == DomainLoadResult::kLoaded);
    auto data = DataGetter(load.reader);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kDataC.data(), data.data(), kDataC.size());
  }
}

}  // namespace ae::test_ds_save_transaction

int test_ds_save_transaction() {
  TELE_SINK::Instance().SetTrap(
      std::make_shared<ae::tele::IoStreamTrap>(std::cout));

  UNITY_BEGIN();
  RUN_TEST(ae::test_ds_save_transaction::test_FirstSaveFiveObjectsOneMap);
  RUN_TEST(ae::test_ds_save_transaction::test_RepeatSaveUnchangedNoWrites);
  RUN_TEST(ae::test_ds_save_transaction::test_ChangeOneObjectOneWriteOneMap);
  RUN_TEST(ae::test_ds_save_transaction::test_NestedBeginEndOneMapCommit);
  RUN_TEST(ae::test_ds_save_transaction::test_WriteFailureNoCrcNoMap);
  RUN_TEST(ae::test_ds_save_transaction::test_RoundTripLoadFromCommittedState);
  return UNITY_END();
}
