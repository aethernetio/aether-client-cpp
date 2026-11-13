add_executable(adaptive-datagram-live main.cpp
  ../benches/stream_matrix/bench_persistence.cpp)
target_link_libraries(adaptive-datagram-live PRIVATE aether)
target_include_directories(adaptive-datagram-live PRIVATE ../benches)

add_executable(aether_stream_matrix_bootstrap
  ../benches/stream_matrix/stream_matrix_bootstrap.cpp
  ../benches/stream_matrix/bench_persistence.cpp)
target_include_directories(aether_stream_matrix_bootstrap PRIVATE ../benches)
target_link_libraries(aether_stream_matrix_bootstrap PRIVATE aether)

add_executable(p2p-throughput
  ../benches/stream_matrix/p2p_throughput.cpp
  ../benches/stream_matrix/bench_persistence.cpp)
target_include_directories(p2p-throughput PRIVATE ../benches ../common)
target_link_libraries(p2p-throughput PRIVATE aether)
