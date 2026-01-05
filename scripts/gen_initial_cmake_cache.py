#!/usr/bin/env python
#
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
#

# This script generates a cmake file for cmake initial cache variables
# from list of cache variables in CMakeCache.txt format
# string like
# HAVE_STRUCT_ADDRINFO:INTERNAL=1
# would be converted to
# set(HAVE_STRUCT_ADDRINFO 1 CACHE INTERNAL "" FORCE)
#
# provide generated file to the cmake's -C argument to reuse cache variables
#

import argparse
import re


def convert_var_to_initial_cache(var:str):
  rx = re.compile(r'([\w\-\_]+):(\w+)=(.*)')
  m = rx.match(var)
  if m:
    name, type, value = m.groups()
    return f'set({name} {value} CACHE {type} "" FORCE)'
  else:
    return None

def generate_initial_cache(vars_file:str, out_file:str):
  with open(vars_file, 'r') as f:
    vars = f.read().splitlines()
  with open(out_file, 'w') as f:
    for var in vars:
      set_var = convert_var_to_initial_cache(var)
      if set_var:
        f.write(set_var + '\n')

if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  parser.add_argument('-vars', required=True, help='File with list of cmake cache variables')
  parser.add_argument('out_file')
  args = parser.parse_args()
  assert(args.vars)
  assert(args.out_file)
  generate_initial_cache(args.vars, args.out_file)
