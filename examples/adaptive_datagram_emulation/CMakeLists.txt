add_executable(adaptive-datagram-emulation main.cpp)
target_link_libraries(adaptive-datagram-emulation PRIVATE aether)
set_target_properties(adaptive-datagram-emulation PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")
