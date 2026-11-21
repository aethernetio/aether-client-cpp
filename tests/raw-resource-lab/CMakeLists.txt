# Windows virtual-time lab uses the same compiled resource as P2pStream.
add_executable(raw-resource-lab EXCLUDE_FROM_ALL main.cpp)
target_link_libraries(raw-resource-lab PRIVATE aether)
