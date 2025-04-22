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


#include "tests/test-file-systems/test-fs-synchronization.h"


int test_fs_synchronization()
{
  int res{0};
  std::unique_ptr<ae::IDomainFacility> fs_header{};
  std::unique_ptr<ae::IDomainFacility> fs_std{};
  const std::string& header_file{"G:\\projects\\prj_aether\\GitHub\\aether-client-cpp\\tests\\test-file-systems\\config\\file_system_init.h"};
  const ae::ObjId& obj_id{1};
  auto data_vector = std::vector<std::uint8_t>{};

  ae::TeleInit::Init();
  AE_TELE_ENV();

  fs_header = std::make_unique<ae::FileSystemHeaderFacility>(header_file);

  fs_std = std::make_unique<ae::FileSystemStdFacility>();
  
  fs_header->Load(obj_id, 2178182515, 0, data_vector);
  
  for(auto& i : data_vector){
    AE_TELED_DEBUG("Enumerated classes 1 {}", i);
  }
  
  fs_std->Load(obj_id, 2178182515, 0, data_vector);
  
  for(auto& i : data_vector){
    AE_TELED_DEBUG("Enumerated classes 2 {}", i);
  }

  return res;
}