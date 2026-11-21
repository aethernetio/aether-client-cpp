add_executable(test-stream-hierarchy main.cpp)
target_link_libraries(test-stream-hierarchy PRIVATE aether unity)
add_test(NAME test-stream-hierarchy COMMAND test-stream-hierarchy)
set_tests_properties(test-stream-hierarchy PROPERTIES TIMEOUT 60)
