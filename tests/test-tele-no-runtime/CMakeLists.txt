# Compile a public consumer without either C++ runtime feature, independently of
# the settings used to build Aether itself.
add_executable(test-tele-no-runtime main.cpp)
target_link_libraries(test-tele-no-runtime PRIVATE aether unity)
target_compile_options(test-tele-no-runtime PRIVATE
  $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/EHs-c-;/GR->
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-exceptions;-fno-rtti>)
target_compile_definitions(test-tele-no-runtime PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:_HAS_EXCEPTIONS=0>)
add_test(NAME test-tele-no-runtime COMMAND test-tele-no-runtime)
set_tests_properties(test-tele-no-runtime PROPERTIES TIMEOUT 10)
