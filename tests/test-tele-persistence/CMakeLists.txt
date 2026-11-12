add_executable(test-tele-persistence main.cpp)
target_link_libraries(test-tele-persistence PRIVATE aether unity)
add_test(NAME test-tele-persistence COMMAND test-tele-persistence)
set_tests_properties(test-tele-persistence PROPERTIES TIMEOUT 60)
