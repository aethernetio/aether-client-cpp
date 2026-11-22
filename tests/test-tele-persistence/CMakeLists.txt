add_executable(test-tele-persistence main.cpp)
target_link_libraries(test-tele-persistence PRIVATE aether unity)
add_test(NAME test-tele-persistence COMMAND test-tele-persistence)
set_tests_properties(test-tele-persistence PROPERTIES TIMEOUT 60)

if(AE_FILTRATION AND NOT CMAKE_CROSSCOMPILING AND (WIN32 OR UNIX))
  add_executable(test-tele-process-restart process_restart.cpp)
  target_link_libraries(test-tele-process-restart PRIVATE aether)
  add_test(NAME test-tele-process-restart COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/process_restart.py --exe $<TARGET_FILE:test-tele-process-restart>)
  set_tests_properties(test-tele-process-restart PROPERTIES TIMEOUT 300)
endif()
