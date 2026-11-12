add_executable(test-tele-namespace main.cpp before.cpp after.cpp)
target_link_libraries(test-tele-namespace PRIVATE aether unity)
add_test(NAME test-tele-namespace COMMAND test-tele-namespace)
