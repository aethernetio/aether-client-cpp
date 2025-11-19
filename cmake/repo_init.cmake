# Copyright 2025 Aethernet Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.16.0)

#
# Initiate, update, patch project submodule dependencies
#

set(ROOT_REPO_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(THIRD_PARTY_DIR "${ROOT_REPO_DIR}/third_party")
set(UPDATED_FILE "${THIRD_PARTY_DIR}/.updated")

find_program(GIT_EXECUTABLE NAMES git)

#
# Check if repository is a git repository
#
function(_ae_is_git_repo RESULT_VAR)
  if(NOT EXISTS "${ROOT_REPO_DIR}/.git")
    set(${RESULT_VAR} "NotAGit" PARENT_SCOPE)
    return()
  endif()
  if (NOT GIT_EXECUTABLE)
    set(${RESULT_VAR} "NotAGit" PARENT_SCOPE)
    return()
  endif()
  set(${RESULT_VAR} "AGit" PARENT_SCOPE)
endfunction()

#
# Get list of submodules
# in format <hash> <path> <tag> <timestamp>
#
function(_ae_submodules_list RESULT_VAR)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C ${ROOT_REPO_DIR} submodule status
    OUTPUT_VARIABLE output_result
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  # output_result should has list of strings in format
  # <hash> <path> <tag>
  # b9d897b5f38674248c86fb58342b87cb6006fe1f ../third_party/Unity (v2.6.1-15-gb9d897b)
  #
  # Add a timestamp for a path at the end of the line
  string(REPLACE "\n" ";" output_result ${output_result})

  set(modules_list "")
  foreach(module IN LISTS output_result)
    string(REGEX MATCH "[a-fA-F0-9]+ (.+) \(.*\)$" match "${module}")
    set(file_path ${CMAKE_MATCH_1})
    file(TIMESTAMP "${ROOT_REPO_DIR}/${file_path}" timestamp)
    string(APPEND modules_list "${module} ${timestamp}\n")
  endforeach()

  set(${RESULT_VAR} "${modules_list}" PARENT_SCOPE)
endfunction()

#
# Check if update for third_parties required
#
function(_ae_is_updated RESULT_VAR)

  if(NOT EXISTS "${UPDATED_FILE}")
    set(${RESULT_VAR} "InitRequired" PARENT_SCOPE)
    return()
  endif()

  # get current submodules list
  _ae_submodules_list(submodules_list)

  file(READ "${UPDATED_FILE}" file_submodules_list)
  if (NOT submodules_list STREQUAL file_submodules_list)
    set(${RESULT_VAR} "UpdateRequired" PARENT_SCOPE)
    return()
  endif()

  set(${RESULT_VAR} "Updated" PARENT_SCOPE)
endfunction()

#
# Update third_parties
#
function(_ae_update_third_parties )
  message(STATUS "Updating/Initializing third_party dependencies")
  # update and init submodules
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C ${ROOT_REPO_DIR} submodule update --force --init --recursive
    RESULT_VARIABLE submodule_update_result)
  if (NOT submodule_update_result EQUAL 0 )
    message(FATAL_ERROR "Failed to update third_party dependencies")
  endif()
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C ${ROOT_REPO_DIR} submodule foreach "'${GIT_EXECUTABLE}'" reset --hard HEAD
    RESULT_VARIABLE submodule_reset_result)
  if (NOT submodule_reset_result EQUAL 0 )
    message(FATAL_ERROR "Failed to reset to default third_party dependencies")
  endif()

  # apply patches
  file(GLOB patch_list LIST_DIRECTORIES false RELATIVE "${THIRD_PARTY_DIR}" "${THIRD_PARTY_DIR}/*.patch")
  foreach(patch_file IN LISTS patch_list)
    string(REPLACE ".patch" "" dep_name "${patch_file}")
    message(STATUS "Applying patch to ${dep_name}")

    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${THIRD_PARTY_DIR}/${dep_name}" apply --ignore-whitespace "${THIRD_PARTY_DIR}/${patch_file}"
      RESULT_VARIABLE patch_apply_result)
    if (NOT patch_apply_result EQUAL 0 )
      message(FATAL_ERROR "Failed to apply patch ${patch_file}")
    endif()
  endforeach()

  #copy cmake files
  file(GLOB cmake_files LIST_DIRECTORIES false RELATIVE "${THIRD_PARTY_DIR}" "${THIRD_PARTY_DIR}/CMakeLists.*")
  foreach(cmake_file IN LISTS cmake_files)
    string(REPLACE "CMakeLists." "" dep_name "${cmake_file}")
    message(STATUS "Copying cmake file to ${dep_name}")

    if (EXISTS "${THIRD_PARTY_DIR}/${dep_name}/CMakeLists.txt")
      file(REMOVE "${THIRD_PARTY_DIR}/${dep_name}/CMakeLists.txt")
    endif()
    file(COPY "${THIRD_PARTY_DIR}/${cmake_file}" DESTINATION "${THIRD_PARTY_DIR}/${dep_name}")
    file(RENAME "${THIRD_PARTY_DIR}/${dep_name}/${cmake_file}" "${THIRD_PARTY_DIR}/${dep_name}/CMakeLists.txt")
  endforeach()
endfunction()

function(ae_update_dependencies)
  _ae_is_git_repo(IS_GIT)
  if (IS_GIT STREQUAL "NotAGit" )
    message(STATUS "Git not available, dependencies will not be updated")
    return()
  endif()

  _ae_is_updated(update_status)
  if (update_status STREQUAL "Updated")
    message(STATUS "Dependencies updated does not required")
    return()
  endif()

  _ae_update_third_parties()

  #write updated file
  _ae_submodules_list(submodules_list)
  message(STATUS "Submodules list is\n${submodules_list}")
  file(WRITE "${UPDATED_FILE}" "${submodules_list}")
endfunction()
