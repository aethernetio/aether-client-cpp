add_executable(test-telemetry-delivery main.cpp)
target_link_libraries(test-telemetry-delivery PRIVATE aether unity)
add_test(NAME test-telemetry-delivery COMMAND test-telemetry-delivery)
set_tests_properties(test-telemetry-delivery PROPERTIES TIMEOUT 60)
