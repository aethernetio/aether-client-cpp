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

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aether/capi.h"

namespace py = pybind11;

namespace {

constexpr auto kWaitSlice = std::chrono::milliseconds{15};
constexpr char const* kPackageVersion = "0.1.0";

std::atomic<bool> g_runtime_active{false};

struct IncomingMessage {
  std::array<std::uint8_t, 16> sender{};
  std::vector<std::uint8_t> data;
};

struct ClientState {
  std::mutex mu;
  std::condition_variable cv;
  AetherClient* client{nullptr};
  std::string local_id;
  std::array<std::uint8_t, 16> uid{};
  bool selected{false};
  bool failed{false};
  bool closing{false};
  std::deque<IncomingMessage> inbox;
};

struct SendState {
  std::mutex mu;
  std::condition_variable cv;
  bool done{false};
  ActionStatus status{ActionStatus::kFailure};
};

enum class CommandKind {
  kSelect,
  kSend,
  kFreeClient,
  kShutdown,
  kLifetimeConfigLifetime,
};

struct SelectCommand {
  std::shared_ptr<ClientState> state;
  std::array<std::uint8_t, 16> parent_uid{};
};

struct SendCommand {
  std::shared_ptr<ClientState> state;
  std::shared_ptr<SendState> send_state;
  std::array<std::uint8_t, 16> destination{};
  std::vector<std::uint8_t> data;
};

struct FreeClientCommand {
  std::shared_ptr<ClientState> state;
};

struct LifetimeProbeCommand {
  std::shared_ptr<std::mutex> mu;
  std::shared_ptr<std::condition_variable> cv;
  std::shared_ptr<bool> done;
  std::shared_ptr<std::string> error;
};

struct Command {
  CommandKind kind;
  SelectCommand select;
  SendCommand send;
  FreeClientCommand free_client;
  LifetimeProbeCommand lifetime;
};

std::uint64_t NowEpochMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

void OnClientSelected(AetherClient* client, void* user_data) {
  auto* state = static_cast<ClientState*>(user_data);
  std::lock_guard<std::mutex> lock(state->mu);
  if (state->closing) {
    return;
  }
  if (client == nullptr) {
    state->failed = true;
    state->cv.notify_all();
    return;
  }
  CUid uid{};
  if (GetClientUid(client, &uid) != 0) {
    state->failed = true;
    state->cv.notify_all();
    return;
  }
  std::memcpy(state->uid.data(), uid.value, state->uid.size());
  state->selected = true;
  state->cv.notify_all();
}

void OnMessageReceived(AetherClient* /*client*/, CUid sender, void const* data,
                       size_t size, void* user_data) {
  auto* state = static_cast<ClientState*>(user_data);
  IncomingMessage message;
  std::memcpy(message.sender.data(), sender.value, message.sender.size());
  auto const* bytes = static_cast<std::uint8_t const*>(data);
  message.data.assign(bytes, bytes + size);

  std::lock_guard<std::mutex> lock(state->mu);
  if (state->closing) {
    return;
  }
  state->inbox.push_back(std::move(message));
  state->cv.notify_all();
}

void OnSendStatus(ActionStatus status, void* user_data) {
  auto* send_state = static_cast<SendState*>(user_data);
  std::lock_guard<std::mutex> lock(send_state->mu);
  send_state->status = status;
  send_state->done = true;
  send_state->cv.notify_all();
}

class NativeRuntime {
 public:
  NativeRuntime() {
    bool expected = false;
    if (!g_runtime_active.compare_exchange_strong(expected, true)) {
      throw std::runtime_error(
          "Only one aethernetio.Runtime may be active per process");
    }
    try {
      worker_ = std::thread([this] { WorkerMain(); });
      WaitUntilReady();
    } catch (...) {
      g_runtime_active.store(false);
      throw;
    }
  }

  ~NativeRuntime() { Close(); }

  NativeRuntime(NativeRuntime const&) = delete;
  NativeRuntime& operator=(NativeRuntime const&) = delete;

  void Close() {
    bool expected = true;
    if (!active_.compare_exchange_strong(expected, false)) {
      return;
    }
    Enqueue(Command{CommandKind::kShutdown, {}, {}, {}, {}});
    {
      py::gil_scoped_release release;
      if (worker_.joinable()) {
        worker_.join();
      }
    }
    g_runtime_active.store(false);
  }

  std::shared_ptr<ClientState> SelectClientWait(
      std::string local_id, std::array<std::uint8_t, 16> parent_uid,
      double timeout_sec) {
    EnsureActive();
    auto state = std::make_shared<ClientState>();
    state->local_id = std::move(local_id);

    Command cmd{};
    cmd.kind = CommandKind::kSelect;
    cmd.select.state = state;
    cmd.select.parent_uid = parent_uid;
    {
      std::lock_guard<std::mutex> lock(clients_mu_);
      clients_[state.get()] = state;
    }
    Enqueue(std::move(cmd));

    auto const deadline =
        std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(timeout_sec));

    bool timed_out = false;
    bool failed = false;
    bool selected = false;
    {
      py::gil_scoped_release release;
      std::unique_lock<std::mutex> lock(state->mu);
      while (!state->selected && !state->failed) {
        if (state->cv.wait_until(lock, deadline) == std::cv_status::timeout) {
          timed_out = true;
          break;
        }
      }
      failed = state->failed;
      selected = state->selected;
    }

    if (timed_out) {
      PyErr_SetString(PyExc_TimeoutError, "select_client timed out");
      throw py::error_already_set();
    }
    if (failed) {
      throw std::runtime_error("Client selection failed");
    }
    if (!selected) {
      throw std::runtime_error("Client selection incomplete");
    }
    return state;
  }

  void Send(std::shared_ptr<ClientState> const& state,
            std::array<std::uint8_t, 16> destination,
            std::vector<std::uint8_t> data, double timeout_sec) {
    EnsureActive();
    auto send_state = std::make_shared<SendState>();

    Command cmd{};
    cmd.kind = CommandKind::kSend;
    cmd.send.state = state;
    cmd.send.send_state = send_state;
    cmd.send.destination = destination;
    cmd.send.data = std::move(data);
    Enqueue(std::move(cmd));

    {
      std::lock_guard<std::mutex> lock(pending_mu_);
      pending_sends_.push_back(send_state);
    }

    auto const deadline =
        std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(timeout_sec));

    bool timed_out = false;
    bool done = false;
    ActionStatus status = ActionStatus::kFailure;
    {
      py::gil_scoped_release release;
      std::unique_lock<std::mutex> lock(send_state->mu);
      while (!send_state->done) {
        if (send_state->cv.wait_until(lock, deadline) ==
            std::cv_status::timeout) {
          timed_out = true;
          break;
        }
      }
      done = send_state->done;
      status = send_state->status;
    }

    if (timed_out) {
      // Keep send_state alive until callback or shutdown.
      PyErr_SetString(PyExc_TimeoutError, "send timed out");
      throw py::error_already_set();
    }
    if (!done) {
      throw std::runtime_error("send incomplete");
    }
    if (status == ActionStatus::kFailure) {
      throw std::runtime_error("send failed");
    }
    if (status == ActionStatus::kStopped) {
      throw std::runtime_error("send stopped");
    }
  }

  IncomingMessage Receive(std::shared_ptr<ClientState> const& state,
                          std::optional<double> timeout_sec) {
    EnsureActive();
    std::optional<std::chrono::steady_clock::time_point> deadline;
    if (timeout_sec.has_value()) {
      deadline = std::chrono::steady_clock::now() +
                 std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                     std::chrono::duration<double>(*timeout_sec));
    }

    IncomingMessage message;
    bool timed_out = false;
    {
      py::gil_scoped_release release;
      std::unique_lock<std::mutex> lock(state->mu);
      while (state->inbox.empty() && !state->closing) {
        if (!deadline.has_value()) {
          state->cv.wait(lock);
          continue;
        }
        if (state->cv.wait_until(lock, *deadline) == std::cv_status::timeout) {
          timed_out = true;
          break;
        }
      }
      if (!timed_out && !state->inbox.empty()) {
        message = std::move(state->inbox.front());
        state->inbox.pop_front();
      }
    }

    if (timed_out) {
      PyErr_SetString(PyExc_TimeoutError, "receive timed out");
      throw py::error_already_set();
    }
    if (state->closing && message.data.empty() &&
        message.sender == std::array<std::uint8_t, 16>{}) {
      throw std::runtime_error("client is closed");
    }
    return message;
  }

  void FreeClient(std::shared_ptr<ClientState> const& state) {
    if (!state) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(state->mu);
      state->closing = true;
    }
    state->cv.notify_all();

    if (!active_.load()) {
      return;
    }
    Command cmd{};
    cmd.kind = CommandKind::kFreeClient;
    cmd.free_client.state = state;
    Enqueue(std::move(cmd));

    {
      std::lock_guard<std::mutex> lock(clients_mu_);
      clients_.erase(state.get());
    }
  }

  // Lifetime regression probe: destroy external ClientConfig/id after SelectClient.
  void ProbeConfigLifetime() {
    EnsureActive();
    auto mu = std::make_shared<std::mutex>();
    auto cv = std::make_shared<std::condition_variable>();
    auto done = std::make_shared<bool>(false);
    auto error = std::make_shared<std::string>();

    Command cmd{};
    cmd.kind = CommandKind::kLifetimeConfigLifetime;
    cmd.lifetime.mu = mu;
    cmd.lifetime.cv = cv;
    cmd.lifetime.done = done;
    cmd.lifetime.error = error;
    Enqueue(std::move(cmd));

    {
      py::gil_scoped_release release;
      std::unique_lock<std::mutex> lock(*mu);
      cv->wait(lock, [&] { return *done; });
    }
    if (!error->empty()) {
      throw std::runtime_error(*error);
    }
  }

 private:
  void EnsureActive() const {
    if (!active_.load()) {
      throw std::runtime_error("Runtime is closed");
    }
  }

  void WaitUntilReady() {
    py::gil_scoped_release release;
    std::unique_lock<std::mutex> lock(ready_mu_);
    ready_cv_.wait(lock, [&] { return ready_ || worker_error_; });
    if (worker_error_) {
      throw std::runtime_error(worker_error_message_);
    }
  }

  void Enqueue(Command cmd) {
    {
      std::lock_guard<std::mutex> lock(queue_mu_);
      queue_.push_back(std::move(cmd));
    }
    queue_cv_.notify_one();
  }

  void WorkerMain() {
    try {
      AeDomainStorageConf storage_conf{};
      storage_conf.type = AeDomainStorage;
      storage_conf.variant = AeDomainStorageVariant::kRam;

      AetherConfig config{};
      config.domain_storage_conf = &storage_conf;
      config.adapters = nullptr;
      config.default_client = nullptr;

      if (AetherInit(&config) != AE_OK) {
        throw std::runtime_error("AetherInit failed");
      }
      initialized_ = true;

      {
        std::lock_guard<std::mutex> lock(ready_mu_);
        ready_ = true;
      }
      ready_cv_.notify_all();

      bool running = true;
      while (running) {
        DrainCommands(running);
        if (!running) {
          break;
        }
        auto const aether_deadline = AetherUpdate();
        auto const now = NowEpochMs();
        auto const slice_deadline =
            now + static_cast<std::uint64_t>(kWaitSlice.count());
        auto const wait_deadline =
            aether_deadline < slice_deadline ? aether_deadline : slice_deadline;
        AetherWait(wait_deadline);
      }
    } catch (std::exception const& ex) {
      {
        std::lock_guard<std::mutex> lock(ready_mu_);
        worker_error_ = true;
        worker_error_message_ = ex.what();
      }
      ready_cv_.notify_all();
    }

    ShutdownAether();
  }

  void DrainCommands(bool& running) {
    std::deque<Command> local;
    {
      std::unique_lock<std::mutex> lock(queue_mu_);
      if (queue_.empty()) {
        // Brief wait so we notice new commands without long AetherWait.
        queue_cv_.wait_for(lock, std::chrono::milliseconds(1));
      }
      local.swap(queue_);
    }

    for (auto& cmd : local) {
      switch (cmd.kind) {
        case CommandKind::kSelect:
          HandleSelect(cmd.select);
          break;
        case CommandKind::kSend:
          HandleSend(cmd.send);
          break;
        case CommandKind::kFreeClient:
          HandleFreeClient(cmd.free_client);
          break;
        case CommandKind::kLifetimeConfigLifetime:
          HandleLifetimeConfigLifetime(cmd.lifetime);
          break;
        case CommandKind::kShutdown:
          running = false;
          break;
      }
    }
  }

  void HandleSelect(SelectCommand& cmd) {
    auto& state = *cmd.state;
    CUid parent{};
    std::memcpy(parent.value, cmd.parent_uid.data(), sizeof(parent.value));

    ClientConfig config{};
    config.id = state.local_id.c_str();
    config.parent_uid = parent;
    config.client_selected_cb = &OnClientSelected;
    config.message_received_cb = &OnMessageReceived;
    config.user_data = cmd.state.get();

    state.client = ::SelectClient(&config);
    // External config and id storage may be destroyed after this return.
  }

  void HandleSend(SendCommand& cmd) {
    if (!cmd.state || !cmd.state->client || cmd.state->closing) {
      std::lock_guard<std::mutex> lock(cmd.send_state->mu);
      cmd.send_state->status = ActionStatus::kStopped;
      cmd.send_state->done = true;
      cmd.send_state->cv.notify_all();
      return;
    }
    CUid destination{};
    std::memcpy(destination.value, cmd.destination.data(),
                sizeof(destination.value));
    ClientSendMessage(cmd.state->client, destination, cmd.data.data(),
                      cmd.data.size(), &OnSendStatus, cmd.send_state.get());
  }

  void HandleFreeClient(FreeClientCommand& cmd) {
    if (!cmd.state) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(cmd.state->mu);
      cmd.state->closing = true;
    }
    if (cmd.state->client != nullptr) {
      ::FreeClient(cmd.state->client);
      cmd.state->client = nullptr;
    }
    cmd.state->cv.notify_all();
  }

  void HandleLifetimeConfigLifetime(LifetimeProbeCommand& cmd) {
    try {
      auto id = std::make_unique<std::string>("lifetime-probe-id");
      auto config = std::make_unique<ClientConfig>();
      config->id = id->c_str();
      config->parent_uid = CUidFromString(
          "3ac93165-3d37-4970-87a6-fa4ee27744e4");
      config->client_selected_cb = nullptr;
      config->message_received_cb = nullptr;
      config->user_data = nullptr;

      AetherClient* client = ::SelectClient(config.get());
      id.reset();
      config.reset();

      for (int i = 0; i < 5; ++i) {
        auto const deadline = AetherUpdate();
        auto const now = NowEpochMs();
        auto const slice =
            now + static_cast<std::uint64_t>(kWaitSlice.count());
        AetherWait(deadline < slice ? deadline : slice);
      }

      ::FreeClient(client);
    } catch (std::exception const& ex) {
      *cmd.error = ex.what();
    }
    {
      std::lock_guard<std::mutex> lock(*cmd.mu);
      *cmd.done = true;
    }
    cmd.cv->notify_all();
  }

  void ShutdownAether() {
    std::vector<std::shared_ptr<ClientState>> clients;
    {
      std::lock_guard<std::mutex> lock(clients_mu_);
      for (auto& [_, state] : clients_) {
        clients.push_back(state);
      }
      clients_.clear();
    }
    for (auto& state : clients) {
      {
        std::lock_guard<std::mutex> lock(state->mu);
        state->closing = true;
      }
      if (state->client != nullptr) {
        ::FreeClient(state->client);
        state->client = nullptr;
      }
      state->cv.notify_all();
    }

    {
      std::lock_guard<std::mutex> lock(pending_mu_);
      for (auto& send_state : pending_sends_) {
        std::lock_guard<std::mutex> send_lock(send_state->mu);
        if (!send_state->done) {
          send_state->status = ActionStatus::kStopped;
          send_state->done = true;
          send_state->cv.notify_all();
        }
      }
      pending_sends_.clear();
    }

    if (initialized_) {
      AetherEnd();
      initialized_ = false;
    }
  }

  std::atomic<bool> active_{true};
  std::thread worker_;
  bool initialized_{false};

  std::mutex ready_mu_;
  std::condition_variable ready_cv_;
  bool ready_{false};
  bool worker_error_{false};
  std::string worker_error_message_;

  std::mutex queue_mu_;
  std::condition_variable queue_cv_;
  std::deque<Command> queue_;

  std::mutex clients_mu_;
  std::unordered_map<ClientState*, std::shared_ptr<ClientState>> clients_;

  std::mutex pending_mu_;
  std::vector<std::shared_ptr<SendState>> pending_sends_;
};

std::array<std::uint8_t, 16> ParseUuid(py::object const& value) {
  std::array<std::uint8_t, 16> out{};
  if (py::isinstance<py::str>(value)) {
    auto text = py::cast<std::string>(value);
    CUid uid = CUidFromString(text.c_str());
    std::memcpy(out.data(), uid.value, out.size());
    return out;
  }
  py::buffer_info info = py::buffer(value.attr("bytes")).request();
  if (info.ndim != 1 || info.size != 16 || info.itemsize != 1) {
    throw std::invalid_argument("destination UUID must be 16 bytes");
  }
  std::memcpy(out.data(), info.ptr, out.size());
  return out;
}

py::object UuidFromBytes(std::array<std::uint8_t, 16> const& bytes) {
  auto uuid_mod = py::module_::import("uuid");
  py::bytes raw(reinterpret_cast<char const*>(bytes.data()), bytes.size());
  return uuid_mod.attr("UUID")(py::arg("bytes") = raw);
}

}  // namespace

PYBIND11_MODULE(_native, m) {
  m.attr("__version__") = kPackageVersion;

  py::class_<ClientState, std::shared_ptr<ClientState>>(m, "ClientState")
      .def_property_readonly("local_id",
                             [](ClientState const& self) {
                               return self.local_id;
                             })
      .def_property_readonly("uid", [](ClientState const& self) {
        return UuidFromBytes(self.uid);
      });

  py::class_<NativeRuntime>(m, "NativeRuntime")
      .def(py::init<>())
      .def("close", &NativeRuntime::Close)
      .def(
          "select_client",
          [](NativeRuntime& self, std::string local_id, py::object parent_uid,
             double timeout) {
            return self.SelectClientWait(std::move(local_id),
                                         ParseUuid(parent_uid), timeout);
          },
          py::arg("local_id"), py::arg("parent_uid"), py::arg("timeout") = 180.0)
      .def(
          "send",
          [](NativeRuntime& self, std::shared_ptr<ClientState> state,
             py::object destination, py::buffer data, double timeout) {
            py::buffer_info info = data.request();
            if (info.ndim != 1) {
              throw std::invalid_argument("data must be a 1-D bytes-like object");
            }
            auto const* ptr = static_cast<std::uint8_t const*>(info.ptr);
            std::vector<std::uint8_t> bytes(
                ptr, ptr + static_cast<std::size_t>(info.size) *
                               static_cast<std::size_t>(info.itemsize));
            self.Send(state, ParseUuid(destination), std::move(bytes), timeout);
          },
          py::arg("state"), py::arg("destination"), py::arg("data"),
          py::arg("timeout") = 60.0)
      .def(
          "receive",
          [](NativeRuntime& self, std::shared_ptr<ClientState> state,
             py::object timeout) {
            std::optional<double> timeout_sec;
            if (!timeout.is_none()) {
              timeout_sec = py::cast<double>(timeout);
            }
            auto message = self.Receive(state, timeout_sec);
            py::dict result;
            result["sender"] = UuidFromBytes(message.sender);
            result["data"] = py::bytes(
                reinterpret_cast<char const*>(message.data.data()),
                message.data.size());
            return result;
          },
          py::arg("state"), py::arg("timeout") = py::none())
      .def("free_client", &NativeRuntime::FreeClient)
      .def("probe_config_lifetime", &NativeRuntime::ProbeConfigLifetime);

  m.def("is_runtime_active", [] { return g_runtime_active.load(); });
}
