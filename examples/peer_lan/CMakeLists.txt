if(NOT CM_PLATFORM AND NOT EMSCRIPTEN)
  add_executable(peer-lan main.cpp)
  target_link_libraries(peer-lan PRIVATE aether_examples_common)
endif()
