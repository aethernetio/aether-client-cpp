# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Release-user_config_android_smoke/cpm.cache/icm/5f60")
  file(MAKE_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Release-user_config_android_smoke/cpm.cache/icm/5f60")
endif()
file(MAKE_DIRECTORY
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Release-user_config_android_smoke/_deps/icm-build"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Release-user_config_android_smoke/_deps/icm-subbuild/icm-populate-prefix"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Release-user_config_android_smoke/_deps/icm-subbuild/icm-populate-prefix/tmp"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Release-user_config_android_smoke/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Release-user_config_android_smoke/_deps/icm-subbuild/icm-populate-prefix/src"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Release-user_config_android_smoke/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Release-user_config_android_smoke/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Release-user_config_android_smoke/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
