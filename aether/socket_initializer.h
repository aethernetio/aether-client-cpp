/*
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

#ifndef AETHER_SOCKET_INITIALIZER_H_
#define AETHER_SOCKET_INITIALIZER_H_

namespace ae {
/**
 * \brief Initializes the socket library.
 */
class SocketInitializer {
#if defined WIN32
 public:
  SocketInitializer();
  ~SocketInitializer();

 private:
  bool initialized_;
#endif
};
}  // namespace ae

#endif  // AETHER_SOCKET_INITIALIZER_H_
