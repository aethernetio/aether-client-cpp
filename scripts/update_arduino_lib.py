#!/usr/bin/env python
#
# Copyright 2024 Aethernet Inc.
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
#

#
# The script for updating aether-client-arduino-library from current repository
# clone the arduino https://github.com/aethernetio/aether-client-arduino-library
# and call this script with -out <path to aether-client-arduino-library>
#

import io
import re
import os
import shutil
import argparse

from pathlib import PureWindowsPath

# copy only files what matches
FILTER_SOURCES = re.compile(r'^[\d\w\-_]+($|(\.((h)|(hpp)|(hh)|(c)|(cpp)|(cc)|(md))$))')
# list names should not copied to arduino library
FILTER_NAMES = ['.git','.github','test', 'tests', 'testdata', 'c-ares', 'docs', 'extras', 'example', 'examples', 'Makefile', 'makefile', 'ini.h', 'unit_tests_runner.c', 'unit_tests_runner.cpp', 'unit_tests_runner.h', 'tz.cpp', 'Unity' ]
# list includes should not be changed to paths relative to repository
NOT_SEARCH_INCLUDES = ['unity.h','unity_config.h', 'randombytes_internal.h', 'stm32f4xx.h', 'stm32l4xx_hal_rng.h', 'etl_profile.h', 'stdint.h', 'FreeRTOS.h']

# filter files and dirs should to copied to arduino lib
def filter_sources(dir, list):
  res = [l for l in list if l in FILTER_NAMES or not FILTER_SOURCES.match(l)]
  return res

def remove_empty_folders(dir:str):
  for (root, dirs, files) in os.walk(dir):
    for d in dirs:
      d_path = os.path.join(root, d)
      while d_path != dir:
        if len(os.listdir(d_path)) != 0:
          break
        os.rmdir(d_path)
        d_path = os.path.split(d_path)[0]

# get file path to #include relative to base
def deep_get_file_path_relative_to(file_name:str, dir: str):
  for (root, dirs, _) in os.walk(dir):
    for d in dirs:
      file_path = os.path.join(root, d, file_name)
      if os.path.exists(file_path):
        return file_path
  return ''

# get file path to #include relative to base
def get_file_path_relative_to(file_name:str, dir: str, base: str):
  if file_name in NOT_SEARCH_INCLUDES:
    return file_name
  if (os.path.exists(os.path.join(base, file_name))):
    return file_name

  while dir != base:
    check_file = os.path.join(dir, file_name)
    if os.path.exists(check_file):
        return os.path.relpath(check_file, base)
    deep_find = deep_get_file_path_relative_to(file_name, dir)
    if len(deep_find):
      return os.path.relpath(deep_find, base)
    # go to parent
    dir = os.path.split(dir)[0]
  raise Exception('file not found', 'File {} should be somewhere in {}'.format(file_name, base))

# Detect there aether root dir is
def detect_aether_root():
  # assume that script is stored in repo's script dir'
  self_file = __file__
  sefl_dir = os.path.split(self_file)[0]
  repo_dir = os.path.abspath(os.path.join(sefl_dir, '..'))
  if os.path.exists(os.path.join(repo_dir, 'aether')):
    return repo_dir
  return ''

# make a file structure of aether library
def build_out_repo_structure(out_dir: str):
  src_dir = os.path.join(out_dir, 'src')
  # make src dir
  if not os.path.exists(src_dir):
    os.mkdir(src_dir)

def clean_out_aether(out_dir: str):
  aether_dir = os.path.join(out_dir, 'src', 'aether')
  if os.path.exists(aether_dir):
    shutil.rmtree(aether_dir)

def clean_out_third_party(out_dir: str):
  aether_dir = os.path.join(out_dir, 'src', 'third_party')
  if os.path.exists(aether_dir):
    shutil.rmtree(aether_dir)

# make include paths relative to repository dir instead of relative to library
def fix_include_paths(dir: str):
  re_include = re.compile(r'^\ *#\ *include \"(.*)\"$')
  re_sources = re.compile(r'^[\d\w\-_]+\.((h)|(hpp)|(hh)|(c)|(cpp)|(cc))$')
  base_dir = os.path.split(dir)[0]
  for (root, _, files) in os.walk(dir):
    for f in files:
      if not re_sources.match(f):
        continue
      file_path = os.path.join(root, f)
      lines = io.open(file_path, 'r', encoding='utf-8').readlines()
      with io.open(file_path, 'w', encoding='utf-8') as open_file:
        for l in lines:
          match = re_include.match(l)
          if match:
            should_include = get_file_path_relative_to(match[1], root, base_dir)
            l = '#include \"{}\"\n'.format(should_include)
            l = PureWindowsPath(l).as_posix()
          open_file.write(l)

def copy_aether_dir(aether_path:str, out_dir: str):
  clean_out_aether(out_dir)
  repo_aether = os.path.join(aether_path, 'aether')
  out_aether = os.path.join(out_dir, 'src','aether')
  shutil.copytree(repo_aether, out_aether, ignore= filter_sources)
  remove_empty_folders(out_aether)

def copy_third_party(repo_dir: str, out_dir: str):
  clean_out_third_party(out_dir)
  repo_third_party = os.path.join(aether_path, 'third_party')
  out_third_party = os.path.join(out_dir, 'src','third_party')
  shutil.copytree(repo_third_party, out_third_party, ignore= filter_sources)
  remove_empty_folders(out_third_party)
  fix_include_paths(out_third_party)

def make_aether_headers_h(repo_dir:str, out_dir: str):
  repo_aether = os.path.join(repo_dir, 'aether')
  aether_headers_h = os.path.join(out_dir, 'src','aether_headers.h')

  aether_lib_preamble = """/*
   * Copyright 2024 Aethernet Inc.
   *
   * Licensed under the Apache License, Version 2.0 (the "License");
   * you may not use this file except in compliance with the License.
   * You may obtain a copy of the License at
   *
   *     http://www.apache.org/licenses/LICENSE-2.0
   *
   * Unless required by applicable law or agreed to in writing, software
   * distributed under the License is distributed on an "AS IS" BASIS,
   * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   * See the License for the specific language governing permissions and
   * limitations under the License.
   */

  #ifndef AETHER_HEADERS_H_
  #define AETHER_HEADERS_H_

"""
  aether_lib_end = """
  #endif // AETHER_HEADERS_H_
"""

  with open(aether_headers_h, 'w') as f:
    f.write(aether_lib_preamble)
    for (root, dirs, files) in os.walk(repo_aether):
      included = False
      for file in files:
        if '.h' in file:
          included = True
          rel_path = os.path.relpath(os.path.join(root, file), aether_path)
          l = "  #include \"{}\"\n".format(PureWindowsPath(rel_path).as_posix())
          f.write(l)
      #split dirs with v space
      if included:
        f.write("\n")

    f.write(aether_lib_end)

def make_arduino_lib(repo_dir: str, out_dir: str):
  print("Aether path is used {}".format(repo_dir))
  print("Make arduino library in  {}".format(out_dir))

  build_out_repo_structure(out_dir)
  copy_aether_dir(repo_dir, out_dir)
  copy_third_party(repo_dir, out_dir)
  make_aether_headers_h(repo_dir, out_dir)


if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  parser.add_argument('-aether', help='Input aether-client-cpp dir, may be detected from script path')
  parser.add_argument('-out', required=True, help='Output directory for arduino library')
  args = parser.parse_args()
  aether_path =''
  if args.aether:
    aether_path = args.aether
  else:
    aether_path = detect_aether_root()
  assert(len(aether_path))
  make_arduino_lib(repo_dir = os.path.abspath(aether_path),out_dir = os.path.abspath(args.out))
