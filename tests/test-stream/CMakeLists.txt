# Copyright 2024 Aethernet Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required( VERSION 3.18 )

list(APPEND test_srcs
  main.cpp
  mock_transport.cpp
  safe-stream/test_safe_stream_types.cpp
  safe-stream/test_sending_chunk_list.cpp
  safe-stream/test_send_data_buffer.cpp
  safe-stream/test_receiving_chunks.cpp
  safe-stream/test_safe_stream_send.cpp
  safe-stream/test_safe_stream_recv.cpp
  safe-stream/test_safe_stream_action.cpp
  safe-stream/test_safe_stream.cpp
  stream-api/test_stream_api.cpp
  crypto-stream/test_crypto_stream.cpp
  protocol-stream/test_protocol_stream.cpp
  templated-streams/test_templated_streams.cpp
  client-to-server-stream/test_client_to_server_stream.cpp
  test-tied-gates.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../test-object-system/map_domain_storage.cpp
)

if(NOT CM_PLATFORM)

  project(test-stream LANGUAGES CXX)

  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${test_srcs})
  # for aether
  target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT_DIR})
  target_link_libraries(${PROJECT_NAME} PRIVATE aether unity gcem)

  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()
