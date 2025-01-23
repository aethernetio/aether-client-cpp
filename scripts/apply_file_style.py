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

import re
import sys
import os
import argparse

LICENSE_START_PATTERN = ('/*\n * Copyright ', '; Copyright', '# Copyright', 'REM Copyright')
FILE_INFO_PATTERN = '/**\n * \\file '

LICENSE_CPP_STR ='''/*
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

'''

LICENSE_HASH_STR = '''# Copyright 2024 Aethernet Inc.
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

'''

LICENSE_INI_STR = '''; Copyright 2024 Aethernet Inc.
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;     http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.

'''

LICENSE_BAT_STR = '''REM Copyright 2024 Aethernet Inc.
REM
REM Licensed under the Apache License, Version 2.0 (the "License");
REM you may not use this file except in compliance with the License.
REM You may obtain a copy of the License at
REM
REM     http://www.apache.org/licenses/LICENSE-2.0
REM
REM Unless required by applicable law or agreed to in writing, software
REM distributed under the License is distributed on an "AS IS" BASIS,
REM WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
REM See the License for the specific language governing permissions and
REM limitations under the License.

'''

def starts_with_empty(data: str):
  return data.startswith(('\n', ' '))

def is_contain_license(data: str):
  return data.startswith(LICENSE_START_PATTERN)

def is_contain_file_info(data: str):
  return data.startswith(FILE_INFO_PATTERN)

def remove_cpp_multiline_comment(data:str):
  start = data.find('/*')
  end = data.find('*/')
  if (start == -1 or end == -1):
    return data
  return data[:start] + data[end+3:]

def apply_license(file_data: str, license_str: str):
  fd = file_data
  if (is_contain_license(fd)):
    return fd
  if (is_contain_file_info(fd)):
    fd = remove_cpp_multiline_comment(fd)
  if (starts_with_empty(fd)):
    fd = fd[1:]
  fd =  license_str + fd
  return fd

def fix_include_guard(file_data: str, file_path: str, base: str):
  file_abs =  os.path.abspath(file_path)
  file_rel = os.path.relpath(file_abs, os.path.abspath(base))

  guard_name = file_rel.replace('/', '_').replace('.', '_').replace('-', '_').upper() + '_'

  file_data = re.sub(r'^#ifndef.*_H(_?)$', '#ifndef '+guard_name, file_data, flags=re.IGNORECASE | re.MULTILINE)
  file_data = re.sub(r'^#define.*_H(_?)$', '#define '+guard_name, file_data, flags=re.IGNORECASE | re.MULTILINE)
  file_data = re.sub(r'^#endif ((//)|(/*)).*_H(_?)$', '#endif  // '+guard_name, file_data, flags=re.IGNORECASE | re.MULTILINE)
  return file_data

def fix_trailing_new_line(file_data: str):
  if (not file_data.endswith('\n')):
    file_data = file_data + '\n'
  return file_data

def apply_to_file_list(file_list: list, base_path:str):
   for file in file_list:
    if (file.endswith('.h')):
       file_type = 'header'
    elif (file.endswith('.cpp') or file.endswith('.c')):
       file_type = 'cpp'
    elif (file.endswith('.cmake')) or file.endswith('CMakeLists.txt'):
       file_type = 'cmake'
    elif (file.endswith('.bat')):
       file_type = 'bat'
    elif (file.endswith('.ini')):
       file_type = 'ini'
    else:
      print('unknown file type {}'.format(file))
      continue

    with open(file, 'r') as f:
      res = f.read()
      # 
      if (file_type == 'cmake' or file_type == 'sh'):
        res = apply_license(res, LICENSE_HASH_STR)
      elif (file_type == 'ini'):
        res = apply_license(res, LICENSE_INI_STR)
      elif (file_type == 'bat'):
        res = apply_license(res, LICENSE_BAT_STR)
      elif (file_type == 'header' or  file_type == 'cpp'):
        res = apply_license(res, LICENSE_CPP_STR)
      
      if (file_type == 'header'):
        res = fix_include_guard(res, file, base_path)

      res = fix_trailing_new_line(res)

      with open(file, 'w') as fres:
        fres.write(res)

def is_excluded(file: str, exclude: list):
  if (not exclude):
    return False
  for e in exclude:
    if (re.match("^.*" + e + ".*$", file)):
      return True
  return False

def get_file_list(base:str, exclude:list):
  print(exclude)
  file_list = []

  for root, dirs, files in os.walk(base):
    for d in dirs:
      if (d.startswith('.')):
        dirs.remove(d)

    for file in files:
      if (file.startswith('.')):
        continue
      file = os.path.join(root, file)
      if (is_excluded(file, exclude)):
        continue
      file_list.append(file)
  return file_list

if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  parser.add_argument( '-exclude','-x', nargs='*', help='exclude pattern to skip files', default=['build.*', 'third_party.*'])
  parser.add_argument('-base', help='base dir for include guard')
  parser.add_argument('files', nargs='*', help='files to apply license')
  args = parser.parse_args()
  if (not args.files):
    file_list = get_file_list(args.base, args.exclude)
  else:
    file_list = args.files

  apply_to_file_list(file_list, args.base)
