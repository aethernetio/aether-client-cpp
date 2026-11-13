add_executable(native_presence_api_bench main.cpp)
target_link_libraries(native_presence_api_bench PRIVATE aether)
target_include_directories(native_presence_api_bench PRIVATE ../../common)
target_compile_options(native_presence_api_bench PRIVATE /W4 /WX /Zc:preprocessor)
