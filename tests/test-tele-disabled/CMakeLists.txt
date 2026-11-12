# USER_CONFIG applies to the production Aether target and this consumer through
# its public definitions. A fixture-only define would not reproduce the bug.
add_executable(test-tele-disabled main.cpp)
target_link_libraries(test-tele-disabled PRIVATE aether unity)
add_test(NAME test-tele-disabled COMMAND test-tele-disabled)
set_tests_properties(test-tele-disabled PROPERTIES TIMEOUT 30)
