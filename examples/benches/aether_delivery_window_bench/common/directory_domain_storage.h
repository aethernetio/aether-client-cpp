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

#ifndef AETHER_DELIVERY_WINDOW_BENCH_COMMON_DIRECTORY_DOMAIN_STORAGE_H_
#define AETHER_DELIVERY_WINDOW_BENCH_COMMON_DIRECTORY_DOMAIN_STORAGE_H_

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <system_error>

#include "aether-miscpp/types/result.h"
#include "aether/obj/idomain_storage.h"

namespace ae::bench::dw {

// File-backed storage rooted at an explicit directory (not CWD).
// Layout matches FileSystemStdStorage: <root>/<obj_id>/<class_id>/<version>
class DirectoryDomainStorage final : public IDomainStorage {
 public:
  explicit DirectoryDomainStorage(std::filesystem::path root)
      : root_{std::move(root)} {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
  }

  std::unique_ptr<IDomainStorageWriter> Store(
      DomainQuery const& query) override {
    auto class_dir =
        root_ / std::to_string(query.id.id()) / std::to_string(query.class_id);
    std::filesystem::create_directories(class_dir);
    auto path = class_dir / std::to_string(query.version);
    std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
    class Writer final : public IDomainStorageWriter {
     public:
      explicit Writer(std::ofstream&& file) : file_{std::move(file)} {}
      ~Writer() override { file_.close(); }
      seri::SeriResult Write(seri::SizeWriteTag data) override {
        auto const u_size = static_cast<std::uint32_t>(data.size);
        return Write(seri::DataTag{u_size});
      }
      seri::SeriResult Write(seri::DataWriteTag data) override {
        file_.write(reinterpret_cast<char const*>(data.data),
                    static_cast<std::streamsize>(data.size));
        if (file_.fail()) {
          return Error{seri::write_error};
        }
        return Ok{seri::good};
      }

     private:
      std::ofstream file_;
    };
    return std::make_unique<Writer>(std::move(f));
  }

  ClassList Enumerate(ObjId const& obj_id) override {
    std::set<std::uint32_t> classes;
    std::error_code ec;
    auto obj_dir = root_ / std::to_string(obj_id.id());
    for (auto const& class_dir :
         std::filesystem::directory_iterator(obj_dir, ec)) {
      classes.insert(static_cast<std::uint32_t>(
          std::stoul(class_dir.path().filename().string())));
    }
    return ClassList{classes.begin(), classes.end()};
  }

  DomainLoad Load(DomainQuery const& query) override {
    auto object_dir = root_ / std::to_string(query.id.id());
    std::error_code ec;
    if (!std::filesystem::exists(object_dir, ec)) {
      return {DomainLoadResult::kEmpty, {}};
    }
    auto path = object_dir / std::to_string(query.class_id) /
                std::to_string(query.version);
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.good()) {
      return {DomainLoadResult::kEmpty, {}};
    }
    class Reader final : public IDomainStorageReader {
     public:
      explicit Reader(std::ifstream&& file) : file_{std::move(file)} {}
      ~Reader() override { file_.close(); }
      seri::SeriResult Read(seri::SizeReadTag data) override {
        std::uint32_t u_size{};
        TRY_RESULT(Read(seri::DataTag{u_size}));
        data.size = static_cast<std::size_t>(u_size);
        return Ok{seri::good};
      }
      seri::SeriResult Read(seri::DataReadTag data) override {
        if (file_.eof()) {
          return Error{seri::read_eof};
        }
        file_.read(reinterpret_cast<char*>(data.data),
                   static_cast<std::streamsize>(data.size));
        if (file_.bad()) {
          return Error{seri::read_error};
        }
        if (file_.gcount() != static_cast<std::streamsize>(data.size)) {
          return Error{file_.eof() ? seri::read_eof : seri::read_error};
        }
        return Ok{seri::good};
      }

     private:
      std::ifstream file_;
    };
    return {DomainLoadResult::kLoaded,
            std::make_unique<Reader>(std::move(f))};
  }

  void Remove(ObjId const& obj_id) override {
    auto object_dir = root_ / std::to_string(obj_id.id());
    std::error_code ec;
    if (!std::filesystem::exists(object_dir, ec)) {
      std::filesystem::create_directory(object_dir, ec);
      return;
    }
    for (auto const& class_dir :
         std::filesystem::directory_iterator(object_dir, ec)) {
      std::error_code ec2;
      std::filesystem::remove_all(class_dir.path(), ec2);
    }
  }

  void CleanUp() override {
    // Bench intentionally preserves state across child restarts so the same
    // UIDs remain in state-a / state-b. Distillation still calls CleanUp on
    // Construct; do not wipe the root here.
  }

 private:
  std::filesystem::path root_;
};

}  // namespace ae::bench::dw

#endif  // AETHER_DELIVERY_WINDOW_BENCH_COMMON_DIRECTORY_DOMAIN_STORAGE_H_
