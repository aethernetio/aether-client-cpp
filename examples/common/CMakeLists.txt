cmake_minimum_required(VERSION 3.16.0)

add_library(aether_examples_common INTERFACE)
target_include_directories(aether_examples_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aether_examples_common INTERFACE aether)
