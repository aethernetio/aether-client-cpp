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

#include "aether/memory.h"
#include "aether/tele/tele_init.h"

// IWYU pragma: begin_exports
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/domain_storage/sync_domain_storage.h"
#include "aether/domain_storage/spifs_domain_storage.h"
#include "aether/domain_storage/static_domain_storage.h"
#include "aether/domain_storage/file_system_std_storage.h"
// IWYU pragma: end_exports

namespace ae::test_ds_synchronization {
auto GetFsDomainStorage() {
#if defined AE_SPIFS_DOMAIN_STORAGE_ENABLED
  return make_unique<SpiFsDomainStorage>();
#elif defined AE_FILE_SYSTEM_STD_ENABLED
  return make_unique<FileSystemStdStorage>();
#endif
}

std::vector<std::uint8_t> DataGetter(
    std::unique_ptr<IDomainStorageReader>& reader) {
  std::uint8_t size;
  reader->read(&size, sizeof(size));
  std::vector<std::uint8_t> res;
  res.resize(size + 1);
  res[0] = size;
  reader->read(res.data() + 1, res.size() - 1);
  return res;
}

static constexpr auto data_1 = std::array<std::uint8_t, 5>{4, 251, 12, 42, 11};
static constexpr auto data_2 = std::array<std::uint8_t, 5>{4, 252, 13, 42, 11};
static constexpr auto data_3 = std::array<std::uint8_t, 5>{4, 253, 14, 42, 11};
static constexpr auto data_4 = std::array<std::uint8_t, 5>{4, 254, 15, 42, 11};

static constexpr auto static_data = StaticDomainData{
    // data map
    StaticMap{{
        std::pair{ObjectPathKey{1, 0}, Span{data_1}},
        std::pair{ObjectPathKey{2, 0}, Span{data_2}},
        std::pair{ObjectPathKey{3, 0}, Span{data_3}},
    }},
};

template <typename StaticDS, typename RWDS>
void TestSyncDataStorage(std::unique_ptr<StaticDS> sds,
                         std::unique_ptr<RWDS> rwds) {
  auto data_storage = SyncDomainStorage{std::move(sds), std::move(rwds)};
  // get data
  {
    auto load_1_0 = data_storage.Load(1, 0);
    TEST_ASSERT(load_1_0.result == DomainLoadResult::kLoaded);
    auto data_1_0 = DataGetter(load_1_0.reader);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data_1.data(), data_1_0.data(),
                                  data_1.size());

    auto load_2_0 = data_storage.Load(2, 0);
    TEST_ASSERT(load_2_0.result == DomainLoadResult::kLoaded);
    auto data_2_0 = DataGetter(load_2_0.reader);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data_2.data(), data_2_0.data(),
                                  data_2.size());

    auto load_2_1 = data_storage.Load(2, 1);
    TEST_ASSERT(load_2_1.result == DomainLoadResult::kEmpty);
  }
  // add data and get after
  {
    auto writer_2_1 = data_storage.Store(2, 1);
    writer_2_1->write(data_4.data(), data_4.size());
    writer_2_1.reset();

    auto load_2_1 = data_storage.Load(2, 0);
    TEST_ASSERT(load_2_1.result == DomainLoadResult::kLoaded);
    auto data_2_1 = DataGetter(load_2_1.reader);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data_4.data(), data_2_1.data(),
                                  data_4.size());
  }
  // remove data and try to load
  {
    data_storage.Remove(1);
    auto load_1_0 = data_storage.Load(1, 0);
    TEST_ASSERT(load_1_0.result == DomainLoadResult::kRemoved);
    auto load_2_0 = data_storage.Load(2, 0);
    TEST_ASSERT(load_2_0.result == DomainLoadResult::kLoaded);
  }
  // add an object
  {
    auto writer_3_0 = data_storage.Store(3, 0);
    writer_3_0->write(data_4.data(), data_4.size());
    writer_3_0.reset();

    auto load_3_0 = data_storage.Load(3, 0);
    TEST_ASSERT(load_3_0.result == DomainLoadResult::kLoaded);
    auto data_3_0 = DataGetter(load_3_0.reader);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data_4.data(), data_3_0.data(),
                                  data_4.size());
    load_3_0.reader.reset();

    data_storage.Remove(3);
    auto load_3_0_r = data_storage.Load(3, 0);
    TEST_ASSERT(load_3_0_r.result == DomainLoadResult::kRemoved);
  }
}

void test_SyncWithRamStorage() {
  auto static_storage = make_unique<StaticDomainStorage>(static_data);
  auto ram_storage = make_unique<RamDomainStorage>();
  TestSyncDataStorage(std::move(static_storage), std::move(ram_storage));
}

void test_SyncWithFSStorage() {
  auto static_storage = make_unique<StaticDomainStorage>(static_data);
  auto fs_storage = GetFsDomainStorage();
  fs_storage->CleanUp();
  TestSyncDataStorage(std::move(static_storage), std::move(fs_storage));
}

}  // namespace ae::test_ds_synchronization

int test_ds_synchronization() {
  ae::tele::TeleInit::Init();
  UNITY_BEGIN();
  RUN_TEST(ae::test_ds_synchronization::test_SyncWithRamStorage);
  RUN_TEST(ae::test_ds_synchronization::test_SyncWithFSStorage);
  return UNITY_END();
}
