cmake_minimum_required(VERSION 3.16.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CM_PLATFORM)
  project("ab-message-exchange" VERSION "1.0.0" LANGUAGES C CXX)
  set(TARGET_NAME ${PROJECT_NAME})
  add_executable(${TARGET_NAME} main.cpp a_b_message_exchange.cpp)
  target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${TARGET_NAME} PRIVATE aether_examples_common)
else()
  idf_build_get_property(CM_PLATFORM CM_PLATFORM)
  if(CM_PLATFORM STREQUAL "ESP32")
    # TCP-only ping-pong config unless the build already selected one.
    if("${USER_CONFIG}" STREQUAL "")
      set(USER_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/user_config.h"
          CACHE PATH "Path to user provided configuration header file" FORCE)
    endif()
    idf_component_register(
      SRCS main.cpp a_b_message_exchange.cpp
      INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ../common
      REQUIRES esp_wifi esp_netif nvs_flash spiffs esp_driver_uart)
    add_subdirectory("../../" aether)
    target_link_libraries(${COMPONENT_LIB} PRIVATE aether)
  else()
    message(FATAL_ERROR "Platform ${CM_PLATFORM} is not supported")
  endif()
endif()
