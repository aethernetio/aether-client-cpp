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

#include <memory>
#include <unity.h>

struct person {
  uint8_t version;
  ae::ObjId obj_id;
  uint32_t class_id;
};

int test_fs_synchronization() {
  int res{0};
  std::unique_ptr<ae::IDomainFacility> fs_header_std{};
  std::unique_ptr<ae::IDomainFacility> fs_header_ram{};
  std::unique_ptr<ae::IDomainFacility> fs_header_spifs1{};
  std::unique_ptr<ae::IDomainFacility> fs_header_spifs2{};
  std::unique_ptr<ae::IDomainFacility> fs_std{};
  std::unique_ptr<ae::IDomainFacility> fs_ram{};
  std::unique_ptr<ae::IDomainFacility> fs_spifs1{};
  std::unique_ptr<ae::IDomainFacility> fs_spifs2{};
  const std::string& header_file{FS_INIT};
  auto data_vector_source = std::vector<std::uint8_t>{};
  auto data_vector_destination = std::vector<std::uint8_t>{};
  auto data_vector_test = std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9};

  struct person test_data_files[] = {
      {0, 1, 2178182515},          {0, 1, 3757268233},
      {0, 2, 207799855},           {0, 2, 2178182515},
      {0, 2, 2967114514},          {0, 3, 1105219716},
      {0, 1004, 698262047},        {0, 1004, 1232350972},
      {0, 1004, 1402813048},       {0, 2001, 1058537116},
      {0, 2001, 2178182515},       {0, 2004, 207799855},
      {0, 2004, 712345007},        {0, 2004, 2178182515},
      {0, 2005, 1296832837},       {0, 2005, 2178182515},
      {0, 3000, 2178182515},       {0, 3000, 2424033868},
      {0, 4000, 844107085},        {0, 4000, 2178182515},
      {0, 4000, 2595772064},       {0, 5000, 460694392},
      {0, 5000, 733819916},        {0, 5000, 2178182515},
      {0, 31884539, 2178182515},   {0, 31884539, 2721984319},
      {0, 238033351, 2178182515},  {0, 238033351, 2721984319},
      {0, 427865074, 2178182515},  {0, 427865074, 2461747236},
      {0, 446995294, 2178182515},  {0, 446995294, 2721984319},
      {0, 468037826, 2178182515},  {0, 468037826, 2461747236},
      {0, 509853244, 2178182515},  {0, 509853244, 2461747236},
      {0, 839911124, 2178182515},  {0, 839911124, 2721984319},
      {0, 925671810, 2178182515},  {0, 925671810, 2721984319},
      {0, 1389725441, 207799855},  {0, 1389725441, 712345007},
      {0, 1389725441, 2178182515}, {0, 1463238598, 701908926},
      {0, 1463238598, 2178182515}, {0, 1487906605, 2178182515},
      {0, 1487906605, 2721984319}, {0, 1702287495, 2178182515},
      {0, 1702287495, 2461747236}, {0, 2108186850, 2178182515},
      {0, 2108186850, 2461747236}, {0, 2497715137, 2178182515},
      {0, 2497715137, 2461747236}, {0, 2541476992, 2178182515},
      {0, 2541476992, 2721984319}, {0, 2552111850, 207799855},
      {0, 2552111850, 712345007},  {0, 2552111850, 2178182515},
      {0, 2629900721, 207799855},  {0, 2629900721, 712345007},
      {0, 2629900721, 2178182515}, {0, 3243445386, 207799855},
      {0, 3243445386, 712345007},  {0, 3243445386, 2178182515},
      {0, 3322975516, 701908926},  {0, 3322975516, 2178182515},
      {0, 3412601104, 1058537116}, {0, 3412601104, 2178182515},
      {0, 3593105038, 2178182515}, {0, 3593105038, 2461747236},
      {0, 3619068402, 1058537116}, {0, 3619068402, 2178182515},
      {0, 3624950362, 701908926},  {0, 3624950362, 2178182515},
      {0, 3661549106, 701908926},  {0, 3661549106, 2178182515},
      {0, 3844414539, 2178182515}, {0, 3844414539, 2461747236},
      {0, 3888289677, 1058537116}, {0, 3888289677, 2178182515},
      {0, 3899239211, 698262047},  {0, 3899239211, 3658551669},
      {0, 3949981516, 2178182515}, {0, 3949981516, 2721984319},
      {0, 4092760964, 1058537116}, {0, 4092760964, 2178182515}};

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
  res++;

  data_vector_destination.clear();
  fs_std->Remove(test_data_file.obj_id);
  fs_std->Load(test_data_file.obj_id, test_data_file.class_id,
               test_data_file.version, data_vector_destination);
  TEST_ASSERT(data_vector_destination.size() == 0);
  res++;

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
  res++;

  // Testing write new data
  data_vector_source.clear();
  fs_header_std->Store(test_data_file.obj_id, test_data_file.class_id,
                       test_data_file.version, data_vector_test);
  fs_header_std->Load(test_data_file.obj_id, test_data_file.class_id,
                      test_data_file.version, data_vector_source);
  TEST_ASSERT(data_vector_test == data_vector_source);
  res++;

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
  res++;

  data_vector_destination.clear();
  fs_ram->Remove(test_data_file.obj_id);
  fs_ram->Load(test_data_file.obj_id, test_data_file.class_id,
               test_data_file.version, data_vector_destination);
  TEST_ASSERT(data_vector_destination.size() == 0);
  res++;

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
  res++;

  // Testing write new data
  data_vector_source.clear();
  fs_header_ram->Store(test_data_file.obj_id, test_data_file.class_id,
                       test_data_file.version, data_vector_test);
  fs_header_ram->Load(test_data_file.obj_id, test_data_file.class_id,
                      test_data_file.version, data_vector_source);
  TEST_ASSERT(data_vector_test == data_vector_source);
  res++;
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
  res++;

  data_vector_destination.clear();
  fs_spifs1->Remove(test_data_file.obj_id);
  fs_spifs1->Load(test_data_file.obj_id, test_data_file.class_id,
                  test_data_file.version, data_vector_destination);
  TEST_ASSERT(data_vector_destination.size() == 0);
  res++;

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
  res++;

  // Testing write new data
  data_vector_source.clear();
  fs_header_spifs1->Store(test_data_file.obj_id, test_data_file.class_id,
                          test_data_file.version, data_vector_test);
  fs_header_spifs1->Load(test_data_file.obj_id, test_data_file.class_id,
                         test_data_file.version, data_vector_source);
  TEST_ASSERT(data_vector_test == data_vector_source);
  res++;

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
  res++;

  data_vector_destination.clear();
  fs_spifs2->Remove(test_data_file.obj_id);
  fs_spifs2->Load(test_data_file.obj_id, test_data_file.class_id,
                  test_data_file.version, data_vector_destination);
  TEST_ASSERT(data_vector_destination.size() == 0);
  res++;

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
  res++;

  // Testing write new data
  data_vector_source.clear();
  fs_header_spifs2->Store(test_data_file.obj_id, test_data_file.class_id,
                          test_data_file.version, data_vector_test);
  fs_header_spifs2->Load(test_data_file.obj_id, test_data_file.class_id,
                         test_data_file.version, data_vector_source);
  TEST_ASSERT(data_vector_test == data_vector_source);
  res++;
#endif  // (!defined(ESP_PLATFORM))

  return res;
}
