if(TARGET aether::lz4_block)
  return()
endif()

CPMAddPackage(NAME aether_lz4_source GITHUB_REPOSITORY lz4/lz4
  GIT_TAG ebb370ca83af193212df4dcbadcc5d87bc0de2f0 DOWNLOAD_ONLY YES)

add_library(aether_lz4_block STATIC lz4_block.cpp "${aether_lz4_source_SOURCE_DIR}/lib/lz4.c")
add_library(aether::lz4_block ALIAS aether_lz4_block)
target_compile_features(aether_lz4_block PUBLIC cxx_std_20)
include(GNUInstallDirs)
target_include_directories(aether_lz4_block PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE "${aether_lz4_source_SOURCE_DIR}/lib")
set_target_properties(aether_lz4_block PROPERTIES
  EXPORT_NAME lz4_block POSITION_INDEPENDENT_CODE ON)
