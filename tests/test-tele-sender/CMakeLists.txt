add_executable(test-tele-sender main.cpp)
target_link_libraries(test-tele-sender PRIVATE aether unity)
add_test(NAME test-tele-sender COMMAND test-tele-sender)
set_tests_properties(test-tele-sender PROPERTIES TIMEOUT 60)
