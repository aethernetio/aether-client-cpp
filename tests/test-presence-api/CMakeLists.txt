add_executable(test-presence-api main.cpp)
target_link_libraries(test-presence-api PRIVATE aether unity)
add_test(NAME test-presence-api COMMAND test-presence-api)
