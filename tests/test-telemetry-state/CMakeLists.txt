add_executable(test-telemetry-state main.cpp)
target_link_libraries(test-telemetry-state PRIVATE aether unity)
add_test(NAME test-telemetry-state COMMAND test-telemetry-state)
set_tests_properties(test-telemetry-state PROPERTIES TIMEOUT 60)
