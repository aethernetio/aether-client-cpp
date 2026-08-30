#ifndef AETHER_PRESENCE_PROBE_DIRECTORY_DOMAIN_STORAGE_H_
#define AETHER_PRESENCE_PROBE_DIRECTORY_DOMAIN_STORAGE_H_

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <system_error>

#include "aether-miscpp/types/result.h"
#include "aether/obj/idomain_storage.h"

namespace presence_probe {

class DirectoryDomainStorage final : public ae::IDomainStorage {
 public:
  explicit DirectoryDomainStorage(std::filesystem::path root)
      : root_{std::move(root)} {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
  }

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override {
    auto class_dir =
        root_ / std::to_string(query.id.id()) / std::to_string(query.class_id);
    std::filesystem::create_directories(class_dir);
    auto path = class_dir / std::to_string(query.version);
    std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
    class Writer final : public ae::IDomainStorageWriter {
     public:
      explicit Writer(std::ofstream&& file) : file_{std::move(file)} {}
      ~Writer() override { file_.close(); }
      ae::seri::SeriResult Write(ae::seri::SizeWriteTag data) override {
        auto const u_size = static_cast<std::uint32_t>(data.size);
        return Write(ae::seri::DataTag{u_size});
      }
      ae::seri::SeriResult Write(ae::seri::DataWriteTag data) override {
        file_.write(reinterpret_cast<char const*>(data.data),
                    static_cast<std::streamsize>(data.size));
        if (file_.fail()) {
          return ae::Error{ae::seri::write_error};
        }
        return ae::Ok{ae::seri::good};
      }

     private:
      std::ofstream file_;
    };
    return std::make_unique<Writer>(std::move(f));
  }

  ae::ClassList Enumerate(ae::ObjId const& obj_id) override {
    std::set<std::uint32_t> classes;
    std::error_code ec;
    auto obj_dir = root_ / std::to_string(obj_id.id());
    for (auto const& class_dir :
         std::filesystem::directory_iterator(obj_dir, ec)) {
      classes.insert(static_cast<std::uint32_t>(
          std::stoul(class_dir.path().filename().string())));
    }
    return ae::ClassList{classes.begin(), classes.end()};
  }

  ae::DomainLoad Load(ae::DomainQuery const& query) override {
    auto object_dir = root_ / std::to_string(query.id.id());
    std::error_code ec;
    if (!std::filesystem::exists(object_dir, ec)) {
      return {ae::DomainLoadResult::kEmpty, {}};
    }
    auto path = object_dir / std::to_string(query.class_id) /
                std::to_string(query.version);
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.good()) {
      return {ae::DomainLoadResult::kEmpty, {}};
    }
    class Reader final : public ae::IDomainStorageReader {
     public:
      explicit Reader(std::ifstream&& file) : file_{std::move(file)} {}
      ~Reader() override { file_.close(); }
      ae::seri::SeriResult Read(ae::seri::SizeReadTag data) override {
        std::uint32_t u_size{};
        auto r = Read(ae::seri::DataTag{u_size});
        if (!r) {
          return r;
        }
        data.size = static_cast<std::size_t>(u_size);
        return ae::Ok{ae::seri::good};
      }
      ae::seri::SeriResult Read(ae::seri::DataReadTag data) override {
        if (file_.eof()) {
          return ae::Error{ae::seri::read_eof};
        }
        file_.read(reinterpret_cast<char*>(data.data),
                   static_cast<std::streamsize>(data.size));
        if (file_.bad()) {
          return ae::Error{ae::seri::read_error};
        }
        if (file_.gcount() != static_cast<std::streamsize>(data.size)) {
          return ae::Error{file_.eof() ? ae::seri::read_eof
                                       : ae::seri::read_error};
        }
        return ae::Ok{ae::seri::good};
      }

     private:
      std::ifstream file_;
    };
    return {ae::DomainLoadResult::kLoaded,
            std::make_unique<Reader>(std::move(f))};
  }

  void Remove(ae::ObjId const& obj_id) override {
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

  void CleanUp() override {}

 private:
  std::filesystem::path root_;
};

}  // namespace presence_probe

#endif  // AETHER_PRESENCE_PROBE_DIRECTORY_DOMAIN_STORAGE_H_
