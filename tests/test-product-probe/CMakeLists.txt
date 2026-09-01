# Copyright 2026 Aethernet Inc.

cmake_minimum_required(VERSION 3.16)

list(APPEND test_srcs
  main.cpp
  test_product_probe_select.cpp
  test_product_probe_protocol.cpp
  test_power_factor_bench.cpp
)

if(NOT CM_PLATFORM)
  project(test-product-probe LANGUAGES CXX)
  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${test_srcs})
  target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT_DIR})
  target_link_libraries(${PROJECT_NAME} PRIVATE unity)
  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()
