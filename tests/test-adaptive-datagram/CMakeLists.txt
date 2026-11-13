if(NOT CM_PLATFORM)
  add_executable(test-adaptive-datagram
    main.cpp ../test-prepared-packet/test-p2p-stream-write-budget.cpp test_adaptive_stream.cpp test_rate_controller.cpp
    test_emulated_socket.cpp test_recovery_controller.cpp test_saturation.cpp)
  target_link_libraries(test-adaptive-datagram PRIVATE aether unity)
  add_library(adaptive-saturation-scenario STATIC saturation_scenario.cpp)
  target_link_libraries(adaptive-saturation-scenario PUBLIC aether)
  target_link_libraries(test-adaptive-datagram PRIVATE adaptive-saturation-scenario)
  target_include_directories(test-adaptive-datagram PRIVATE ${ROOT_DIR})
  add_test(NAME test-adaptive-datagram COMMAND test-adaptive-datagram)
  set_tests_properties(test-adaptive-datagram PROPERTIES TIMEOUT 60)
  add_executable(adaptive-datagram-recovery recovery_scenarios.cpp)
  target_link_libraries(adaptive-datagram-recovery PRIVATE aether)
  add_test(NAME test-adaptive-recovery
    COMMAND adaptive-datagram-recovery recovery-ctest.csv 12345 --check)
  set_tests_properties(test-adaptive-recovery PROPERTIES TIMEOUT 60)
  add_executable(adaptive-datagram-saturation saturation_sweep.cpp)
  target_link_libraries(adaptive-datagram-saturation PRIVATE adaptive-saturation-scenario)
  add_test(NAME test-network-saturation
    COMMAND adaptive-datagram-saturation saturation-runs.csv saturation-summary.csv)
  set_tests_properties(test-network-saturation PROPERTIES TIMEOUT 60)
endif()
