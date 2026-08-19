/*
 * Copyright 2026 Aethernet Inc.
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

#include <cstdio>
#include <cstdlib>
#include <string>

#include <dlfcn.h>

using SmokeFn = int (*)();

namespace {

char const* DefaultLibraryName() {
  return "libaether_android_smoke.so";
}

}  // namespace

int main(int argc, char** argv) {
  auto const* library_path =
      (argc > 1 && argv[1] != nullptr) ? argv[1] : DefaultLibraryName();

  void* handle = dlopen(library_path, RTLD_NOW);
  if (handle == nullptr) {
    std::fprintf(stderr, "dlopen failed for %s: %s\n", library_path, dlerror());
    return 2;
  }

  dlerror();
  auto* smoke = reinterpret_cast<SmokeFn>(dlsym(handle, "aether_android_smoke_run"));
  auto const* sym_error = dlerror();
  if (sym_error != nullptr || smoke == nullptr) {
    std::fprintf(stderr, "dlsym failed for aether_android_smoke_run: %s\n",
                 sym_error != nullptr ? sym_error : "null symbol");
    dlclose(handle);
    return 3;
  }

  auto const exit_code = smoke();
  dlclose(handle);
  return exit_code;
}
