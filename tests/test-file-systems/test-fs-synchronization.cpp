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

#include "tests/test-file-systems/test-fs-synchronization.h"

namespace ae::test_fs {
struct person {
  uint8_t version;
  ae::ObjId obj_id;
  uint32_t class_id;
};

void test_FS() {
  std::unique_ptr<ae::IDomainFacility> fs_header_std{};
  std::unique_ptr<ae::IDomainFacility> fs_header_ram{};
  std::unique_ptr<ae::IDomainFacility> fs_header_spifs1{};
  std::unique_ptr<ae::IDomainFacility> fs_header_spifs2{};
  std::unique_ptr<ae::IDomainFacility> fs_std{};
  std::unique_ptr<ae::IDomainFacility> fs_ram{};
  std::unique_ptr<ae::IDomainFacility> fs_spifs1{};
  std::unique_ptr<ae::IDomainFacility> fs_spifs2{};
  const std::string& header_file{FS_INIT_TEST};
  auto data_vector_source = std::vector<std::uint8_t>{};
  auto data_vector_destination = std::vector<std::uint8_t>{};
  auto data_vector_test = std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9};

  struct person test_data_files[] = {
      {0, 1, 2178182515},          {0, 1, 3757268233},
      {0, 2, 207799855},           {0, 2, 2178182515},
      {0, 2, 2967114514},          {0, 3, 1105219716},
      {0, 3, 2178182515},          {0, 1004, 698262047},
      {0, 1004, 1232350972},       {0, 1004, 1402813048},
      {0, 1004, 2178182515},       {0, 2001, 1058537116},
      {0, 2001, 2178182515},       {0, 2004, 207799855},
      {0, 2004, 712345007},        {0, 2004, 2178182515},
      {0, 2005, 1296832837},       {0, 2005, 2178182515},
      {0, 3000, 2178182515},       {0, 3000, 2424033868},
      {0, 4000, 844107085},        {0, 4000, 2178182515},
      {0, 4000, 2595772064},       {0, 5000, 460694392},
      {0, 5000, 733819916},        {0, 5000, 2178182515},
      {0, 57945553, 2178182515},   {0, 57945553, 2721984319},
      {0, 420595957, 2178182515},  {0, 420595957, 2654162901},
      {0, 452580016, 207799855},   {0, 452580016, 712345007},
      {0, 452580016, 2178182515},  {0, 567922887, 207799855},
      {0, 567922887, 712345007},   {0, 567922887, 2178182515},
      {0, 723625478, 701908926},   {0, 723625478, 2178182515},
      {0, 835663356, 2178182515},  {0, 835663356, 2654162901},
      {0, 1039687326, 1058537116}, {0, 1039687326, 2178182515},
      {0, 1122194861, 2178182515}, {0, 1122194861, 2461747236},
      {0, 1157008489, 2178182515}, {0, 1157008489, 2654162901},
      {0, 1247126042, 698262047},  {0, 1247126042, 2178182515},
      {0, 1247126042, 3658551669}, {0, 1283522777, 1058537116},
      {0, 1283522777, 2178182515}, {0, 1437290635, 207799855},
      {0, 1437290635, 712345007},  {0, 1437290635, 2178182515},
      {0, 1464339113, 2178182515}, {0, 1464339113, 2461747236},
      {0, 1803625749, 2178182515}, {0, 1803625749, 2721984319},
      {0, 2081787544, 1058537116}, {0, 2081787544, 2178182515},
      {0, 2263485675, 701908926},  {0, 2263485675, 2178182515},
      {0, 2608149322, 2178182515}, {0, 2608149322, 2721984319},
      {0, 2922574180, 2178182515}, {0, 2922574180, 2461747236},
      {0, 2933981517, 2178182515}, {0, 2933981517, 2654162901},
      {0, 2945947715, 1058537116}, {0, 2945947715, 2178182515},
      {0, 3415235610, 207799855},  {0, 3415235610, 712345007},
      {0, 3415235610, 2178182515}, {0, 3422915294, 2178182515},
      {0, 3422915294, 2654162901}, {0, 3524039194, 2178182515},
      {0, 3524039194, 2461747236}, {0, 3564071848, 2178182515},
      {0, 3564071848, 2654162901}, {0, 3645644430, 2178182515},
      {0, 3645644430, 2461747236}, {0, 3684034888, 2178182515},
      {0, 3684034888, 2721984319}, {0, 3892072971, 2178182515},
      {0, 3892072971, 2461747236}, {0, 4001621809, 701908926},
      {0, 4001621809, 2178182515}, {0, 4051868437, 2178182515},
      {0, 4051868437, 2721984319}, {0, 4197235001, 2178182515},
      {0, 4197235001, 2721984319}, {0, 4198791665, 701908926},
      {0, 4198791665, 2178182515}};

  struct person test_data_file{1, 2, 3};

  ae::TeleInit::Init();
  AE_TELE_ENV();

#if (!defined(ESP_PLATFORM))

  // FS STD tests
  fs_header_std = std::make_unique<ae::FileSystemHeaderFacility>(
      header_file, ae::DriverFsType::kDriverStd);
  fs_std = std::make_unique<ae::FileSystemStdFacility>();

  // Data deletion test for the destination file system
  for (int i = 0; i < sizeof(test_data_files) / sizeof(test_data_files[0]);
       i++) {
    data_vector_destination.clear();
    fs_std->Remove(test_data_files[i].obj_id);
    fs_std->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                 test_data_files[i].version, data_vector_destination);
    TEST_ASSERT(data_vector_destination.size() == 0);
  }

  data_vector_destination.clear();
  fs_std->Remove(test_data_file.obj_id);
  fs_std->Load(test_data_file.obj_id, test_data_file.class_id,
               test_data_file.version, data_vector_destination);
  TEST_ASSERT(data_vector_destination.size() == 0);

  // Test copying data from the source file system to the destination file
  // system
  for (int i = 0; i < sizeof(test_data_files) / sizeof(test_data_files[0]);
       i++) {
    data_vector_source.clear();
    data_vector_destination.clear();
    fs_header_std->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                        test_data_files[i].version, data_vector_source);
    fs_std->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                 test_data_files[i].version, data_vector_destination);
    TEST_ASSERT(data_vector_source == data_vector_destination);
  }

  // Testing write new data
  data_vector_source.clear();
  fs_header_std->Store(test_data_file.obj_id, test_data_file.class_id,
                       test_data_file.version, data_vector_test);
  fs_header_std->Load(test_data_file.obj_id, test_data_file.class_id,
                      test_data_file.version, data_vector_source);
  TEST_ASSERT(data_vector_test == data_vector_source);

  // FS RAM tests
  fs_header_ram = std::make_unique<ae::FileSystemHeaderFacility>(
      header_file, ae::DriverFsType::kDriverRam);
  fs_ram = std::make_unique<ae::FileSystemRamFacility>();

  // Data deletion test for the destination file system
  for (int i = 0; i < sizeof(test_data_files) / sizeof(test_data_files[0]);
       i++) {
    data_vector_destination.clear();
    fs_ram->Remove(test_data_files[i].obj_id);
    fs_ram->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                 test_data_files[i].version, data_vector_destination);
    TEST_ASSERT(data_vector_destination.size() == 0);
  }

  data_vector_destination.clear();
  fs_ram->Remove(test_data_file.obj_id);
  fs_ram->Load(test_data_file.obj_id, test_data_file.class_id,
               test_data_file.version, data_vector_destination);
  TEST_ASSERT(data_vector_destination.size() == 0);

  // Test copying data from the source file system to the destination file
  // system
  for (int i = 0; i < sizeof(test_data_files) / sizeof(test_data_files[0]);
       i++) {
    data_vector_source.clear();
    data_vector_destination.clear();
    fs_header_ram->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                        test_data_files[i].version, data_vector_source);
    fs_ram->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                 test_data_files[i].version, data_vector_destination);
    TEST_ASSERT(data_vector_source == data_vector_destination);
  }

  // Testing write new data
  data_vector_source.clear();
  fs_header_ram->Store(test_data_file.obj_id, test_data_file.class_id,
                       test_data_file.version, data_vector_test);
  fs_header_ram->Load(test_data_file.obj_id, test_data_file.class_id,
                      test_data_file.version, data_vector_source);
  TEST_ASSERT(data_vector_test == data_vector_source);
#else

  // FS SPIFSV1 tests
  fs_header_spifs1 = std::make_unique<ae::FileSystemHeaderFacility>(
      header_file, ae::DriverFsType::kDriverSpifsV1);
  fs_spifs1 = std::make_unique<ae::FileSystemSpiFsV1Facility>();

  // Data deletion test for the destination file system
  for (int i = 0; i < sizeof(test_data_files) / sizeof(test_data_files[0]);
       i++) {
    data_vector_destination.clear();
    fs_spifs1->Remove(test_data_files[i].obj_id);
    fs_spifs1->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                    test_data_files[i].version, data_vector_destination);
    TEST_ASSERT(data_vector_destination.size() == 0);
  }

  data_vector_destination.clear();
  fs_spifs1->Remove(test_data_file.obj_id);
  fs_spifs1->Load(test_data_file.obj_id, test_data_file.class_id,
                  test_data_file.version, data_vector_destination);
  TEST_ASSERT(data_vector_destination.size() == 0);

  // Test copying data from the source file system to the destination file
  // system
  for (int i = 0; i < sizeof(test_data_files) / sizeof(test_data_files[0]);
       i++) {
    data_vector_source.clear();
    data_vector_destination.clear();
    fs_header_spifs1->Load(test_data_files[i].obj_id,
                           test_data_files[i].class_id,
                           test_data_files[i].version, data_vector_source);
    fs_spifs1->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                    test_data_files[i].version, data_vector_destination);
    TEST_ASSERT(data_vector_source == data_vector_destination);
  }

  // Testing write new data
  data_vector_source.clear();
  fs_header_spifs1->Store(test_data_file.obj_id, test_data_file.class_id,
                          test_data_file.version, data_vector_test);
  fs_header_spifs1->Load(test_data_file.obj_id, test_data_file.class_id,
                         test_data_file.version, data_vector_source);
  TEST_ASSERT(data_vector_test == data_vector_source);

  // FS SPIFSV2 tests
  fs_header_spifs2 = std::make_unique<ae::FileSystemHeaderFacility>(
      header_file, ae::DriverFsType::kDriverSpifsV2);
  fs_spifs2 = std::make_unique<ae::FileSystemSpiFsV2Facility>();

  // Data deletion test for the destination file system
  for (int i = 0; i < sizeof(test_data_files) / sizeof(test_data_files[0]);
       i++) {
    data_vector_destination.clear();
    fs_spifs2->Remove(test_data_files[i].obj_id);
    fs_spifs2->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                    test_data_files[i].version, data_vector_destination);
    TEST_ASSERT(data_vector_destination.size() == 0);
  }

  data_vector_destination.clear();
  fs_spifs2->Remove(test_data_file.obj_id);
  fs_spifs2->Load(test_data_file.obj_id, test_data_file.class_id,
                  test_data_file.version, data_vector_destination);
  TEST_ASSERT(data_vector_destination.size() == 0);

  // Test copying data from the source file system to the destination file
  // system
  for (int i = 0; i < sizeof(test_data_files) / sizeof(test_data_files[0]);
       i++) {
    data_vector_source.clear();
    data_vector_destination.clear();
    fs_header_spifs2->Load(test_data_files[i].obj_id,
                           test_data_files[i].class_id,
                           test_data_files[i].version, data_vector_source);
    fs_spifs2->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                    test_data_files[i].version, data_vector_destination);
    TEST_ASSERT(data_vector_source == data_vector_destination);
  }

  // Testing write new data
  data_vector_source.clear();
  fs_header_spifs2->Store(test_data_file.obj_id, test_data_file.class_id,
                          test_data_file.version, data_vector_test);
  fs_header_spifs2->Load(test_data_file.obj_id, test_data_file.class_id,
                         test_data_file.version, data_vector_source);
  TEST_ASSERT(data_vector_test == data_vector_source);
#endif  // (!defined(ESP_PLATFORM))
}
}  // namespace ae::test_fs
int test_fs_synchronization() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_fs::test_FS);
  return UNITY_END();
}
