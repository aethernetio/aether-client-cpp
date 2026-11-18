// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_PEER_SERIALIZATION_H_
#define AETHER_PEER_PEER_SERIALIZATION_H_
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether/api_protocol/details/api_message.h"
namespace ae {
template <typename... T>
DataBuffer PeerEncode(T const&... values) {
  DataBuffer data;
  auto archive = seri::BinaryArchive{MessageBuffer{data}};
  [[maybe_unused]] bool ok = (static_cast<bool>(archive.Save(values)) && ...);
  assert(ok && "Peer serialization failed");
  return data;
}
template <typename... T>
bool PeerDecode(DataBuffer const& data, T&... values) {
  auto copy = data;
  auto archive = seri::BinaryArchive{MessageBuffer{copy}, data.size()};
  return (static_cast<bool>(archive.Load(values)) && ...) &&
         archive.buffer().read_offset == data.size();
}
}  // namespace ae
#endif
