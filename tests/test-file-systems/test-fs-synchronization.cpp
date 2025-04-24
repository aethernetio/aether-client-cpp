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

#include <memory>
#include <unity.h>


#include "tests/test-file-systems/test-fs-synchronization.h"


struct person {
  uint8_t version;
  ae::ObjId obj_id;
  uint32_t class_id;  
};

int test_fs_synchronization()
{
  int res{0};
  std::unique_ptr<ae::IDomainFacility> fs_header{};
  std::unique_ptr<ae::IDomainFacility> fs_std{};
  const std::string& header_file{FS_INIT};  
  auto data_vector1 = std::vector<std::uint8_t>{};
  auto data_vector2 = std::vector<std::uint8_t>{};
  auto data_vector3 = std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9};
  
  struct person test_data_files[] = {
      {0, 1637257050, 2178182515}, {0, 67090949, 2178182515},
      {0, 67090949, 2461747236},   {0, 2004163049, 2178182515},
      {0, 2004163049, 2461747236}, {0, 1637257050, 2721984319},
      {0, 3849121931, 2178182515}, {0, 1015857726, 2178182515},
      {0, 1015857726, 2461747236}, {0, 3485820442, 2178182515},
      {0, 3485820442, 2461747236}, {0, 3849121931, 2721984319},
      {0, 3902688401, 2178182515}, {0, 3595772953, 2178182515},
      {0, 3595772953, 2461747236}, {0, 4124502534, 2178182515},
      {0, 4124502534, 2461747236}, {0, 3902688401, 2721984319},
      {0, 1690111839, 2178182515}, {0, 103406648, 2178182515},
      {0, 103406648, 2461747236},  {0, 3921755013, 2178182515},
      {0, 3921755013, 2461747236}, {0, 1690111839, 2721984319},
      {0, 1, 2178182515},          {0, 2001, 2178182515},
      {0, 2001, 1058537116},       {0, 2004, 2178182515},
      {0, 2004, 207799855},        {0, 2, 2178182515},
      {0, 4145604614, 2178182515}, {0, 3556243451, 2178182515},
      {0, 3556243451, 2461747236}, {0, 1646131247, 2178182515},
      {0, 1646131247, 2461747236}, {0, 4145604614, 2721984319},
      {0, 2, 207799855},           {0, 3000, 2178182515},
      {0, 3000, 2424033868},       {0, 3, 1105219716},
      {0, 4000, 2178182515},       {0, 4000, 844107085},
      {0, 4000, 3591204969},       {0, 5000, 2178182515},
      {0, 5000, 733819916},        {0, 5000, 1196019635},
      {0, 2005, 2178182515},       {0, 2005, 1296832837},
      {0, 1003, 698262047},        {0, 1003, 791384977},
      {0, 1, 3757268233}
  };

  struct person test_data_file{1, 2, 3};

  ae::TeleInit::Init();
  AE_TELE_ENV();

  fs_header = std::make_unique<ae::FileSystemHeaderFacility>(header_file);
  fs_std = std::make_unique<ae::FileSystemStdFacility>();

  // Data deletion test for the destination file system
  for (int i = 0; i < sizeof(test_data_files) / sizeof(test_data_files[0]);
       i++) {
    fs_std->Remove(test_data_files[i].obj_id);
    fs_std->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                 test_data_files[i].version,
                 data_vector1);
    TEST_ASSERT(data_vector1.size() == 0);
  }
  res++;

  fs_std->Remove(test_data_file.obj_id);
  fs_std->Load(test_data_file.obj_id, test_data_file.class_id,
               test_data_file.version, data_vector1);
  TEST_ASSERT(data_vector1.size() == 0);
  res++;

  // Test copying data from the source file system to the destination file system
  for (int i = 0; i < sizeof(test_data_files) / sizeof(test_data_files[0]);
       i++) {
    fs_header->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                    test_data_files[i].version, data_vector2);
    fs_std->Load(test_data_files[i].obj_id, test_data_files[i].class_id,
                 test_data_files[i].version, data_vector1);
    TEST_ASSERT(data_vector2 == data_vector1);
  }
  res++;

  // Testing write new data
  fs_header->Store(test_data_file.obj_id, test_data_file.class_id,
                   test_data_file.version, data_vector3);
  fs_header->Load(test_data_file.obj_id, test_data_file.class_id,
                   test_data_file.version, data_vector1);
  TEST_ASSERT(data_vector3 == data_vector1);
  res++;

  return res;
}