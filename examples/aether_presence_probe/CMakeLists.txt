cmake_minimum_required(VERSION 3.16.0)
if(NOT CM_PLATFORM AND WIN32)
  project("aether_presence_probe" VERSION "1.0.0" LANGUAGES C CXX)
  add_executable(aether_presence_probe main.cpp)
  target_link_libraries(aether_presence_probe PRIVATE aether)
  target_include_directories(aether_presence_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
  )
  target_compile_definitions(aether_presence_probe PRIVATE
    AE_TELE_LOG_CONSOLE=0
    _CRT_SECURE_NO_WARNINGS
  )
  if(MSVC)
    target_compile_options(aether_presence_probe PRIVATE /utf-8 /W4)
  endif()
endif()
