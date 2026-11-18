if(NOT CM_PLATFORM AND NOT EMSCRIPTEN)
  add_executable(peer-rendezvous main.cpp)
  target_link_libraries(peer-rendezvous PRIVATE aether_examples_common)
endif()
