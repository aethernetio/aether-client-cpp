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

#include "browser_ping_pong.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#  include <emscripten.h>
#endif

#include "aether-miscpp/format/format.h"
#include "aether/all.h"
#include "aether/adapters/browser_adapter.h"
#include "aether/global_ids.h"
#include "aether/platform/emscripten_main_loop.h"
#include "aether/platform/emscripten_storage.h"
#include "aether/registration_cloud.h"
#include "aether/server.h"
#include "aether/transport/browser/browser_endpoint.h"
#include "aether/types/address.h"

#include "ping_pong_frame.h"

namespace ae::examples::browser_ping_pong {
namespace {

constexpr auto kParentUid =
    ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
constexpr std::size_t kMaxInFlight = 16;
constexpr std::size_t kMaxRttSamples = 512;
constexpr std::uint32_t kSessionId = 0x41505031u;  // 'APP1'
constexpr int kDefaultPayloadBytes = 16;
constexpr int kDefaultPingTimeoutMs = 5000;
constexpr ServerId kBrowserOverrideServerId{9001};

enum class AppState {
  kIdle,
  kLoadingStorage,
  kLoadingCrypto,
  kConstructApp,
  kSelectClient,
  kReady,
  kConnecting,
  kConnected,
  kError,
};

char const* StateLabel(AppState state) {
  switch (state) {
    case AppState::kIdle:
      return "Idle";
    case AppState::kLoadingStorage:
      return "Loading storage";
    case AppState::kLoadingCrypto:
      return "Loading crypto";
    case AppState::kConstructApp:
      return "Constructing app";
    case AppState::kSelectClient:
      return "Registering/loading client";
    case AppState::kReady:
      return "Ready";
    case AppState::kConnecting:
      return "Connecting";
    case AppState::kConnected:
      return "Connected";
    case AppState::kError:
      return "Error";
  }
  return "Unknown";
}

char const* ProtocolName(Protocol protocol) {
  switch (protocol) {
    case Protocol::kHttp:
      return "HTTP";
    case Protocol::kHttps:
      return "HTTPS";
    case Protocol::kWebSocket:
      return "WS";
    case Protocol::kWebSocketSecure:
      return "WSS";
    case Protocol::kTcp:
      return "TCP";
    case Protocol::kUdp:
      return "UDP";
  }
  return "unknown";
}

std::uint64_t MonoNs() {
  using clock = std::chrono::steady_clock;
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          clock::now().time_since_epoch())
          .count());
}

struct ParsedGateway {
  Protocol protocol{Protocol::kWebSocket};
  BrowserAddr addr{};
  std::uint16_t port{0};
};

std::optional<Protocol> ParseTransport(std::string_view transport) {
  if (transport == "HTTP" || transport == "http") {
    return Protocol::kHttp;
  }
  if (transport == "HTTPS" || transport == "https") {
    return Protocol::kHttps;
  }
  if (transport == "WS" || transport == "ws") {
    return Protocol::kWebSocket;
  }
  if (transport == "WSS" || transport == "wss") {
    return Protocol::kWebSocketSecure;
  }
  return std::nullopt;
}

std::optional<ParsedGateway> ParseGateway(std::string_view url,
                                          Protocol protocol) {
  ParsedGateway out;
  out.protocol = protocol;
  auto rest = url;
  auto scheme_sep = rest.find("://");
  if (scheme_sep != std::string_view::npos) {
    auto scheme = rest.substr(0, scheme_sep);
    auto inferred = ParseTransport(scheme);
    if (inferred) {
      out.protocol = *inferred;
    }
    rest = rest.substr(scheme_sep + 3);
  }
  if (rest.empty()) {
    return std::nullopt;
  }
  auto path_pos = rest.find('/');
  auto hostport =
      path_pos == std::string_view::npos ? rest : rest.substr(0, path_pos);
  auto path = path_pos == std::string_view::npos ? std::string_view{}
                                                 : rest.substr(path_pos);
  auto colon = hostport.rfind(':');
  std::string host;
  std::uint16_t port = 0;
  if (colon != std::string_view::npos &&
      hostport.find(']') == std::string_view::npos) {
    host = std::string{hostport.substr(0, colon)};
    auto port_sv = hostport.substr(colon + 1);
    unsigned long parsed = 0;
    for (char c : port_sv) {
      if (c < '0' || c > '9') {
        return std::nullopt;
      }
      parsed = parsed * 10u + static_cast<unsigned long>(c - '0');
      if (parsed > 65535ul) {
        return std::nullopt;
      }
    }
    port = static_cast<std::uint16_t>(parsed);
  } else {
    host = std::string{hostport};
  }
  if (host.empty()) {
    return std::nullopt;
  }
  out.addr.representation_version = 1;
  out.addr.hostname = std::move(host);
  if (path.empty()) {
    if (IsWebSocketProtocol(out.protocol)) {
      out.addr.path = std::string{browser_endpoint_internal::kDefaultWsPath};
    } else {
      out.addr.path =
          std::string{browser_endpoint_internal::kDefaultHttpApiRoot};
    }
  } else {
    out.addr.path = std::string{path};
  }
  out.port = port;
  return out;
}

bool EndpointIsBrowserCapable(Endpoint const& endpoint) {
  if (endpoint.address.Index() == AddrVersion::kBrowser) {
    return IsHttpProtocol(endpoint.protocol) ||
           IsWebSocketProtocol(endpoint.protocol);
  }
  if (endpoint.address.Index() == AddrVersion::kNamed &&
      (IsHttpProtocol(endpoint.protocol) ||
       IsWebSocketProtocol(endpoint.protocol))) {
    return true;
  }
  return false;
}

bool CloudHasOnlyTcpUdp(Cloud::ptr const& cloud) {
  if (!cloud.is_valid()) {
    return true;
  }
  bool any = false;
  bool only_tcp_udp = true;
  for (auto const& [_, entry] : cloud->servers()) {
    auto server = entry.server.Load();
    if (!server) {
      continue;
    }
    for (auto const& ep : server->endpoints) {
      any = true;
      if (EndpointIsBrowserCapable(ep)) {
        only_tcp_udp = false;
      }
    }
  }
  return any && only_tcp_udp;
}

double Percentile(std::vector<double> sorted, double p) {
  if (sorted.empty()) {
    return 0.0;
  }
  if (sorted.size() == 1) {
    return sorted.front();
  }
  double const idx = (p / 100.0) * static_cast<double>(sorted.size() - 1);
  auto const lo = static_cast<std::size_t>(std::floor(idx));
  auto const hi = static_cast<std::size_t>(std::ceil(idx));
  if (lo == hi) {
    return sorted[lo];
  }
  double const w = idx - static_cast<double>(lo);
  return sorted[lo] * (1.0 - w) + sorted[hi] * w;
}

struct Stats {
  std::uint64_t sent{0};
  std::uint64_t pong_received{0};
  std::uint64_t timed_out{0};
  std::uint64_t duplicate{0};
  std::uint64_t out_of_order{0};
  std::uint64_t malformed{0};
  std::uint64_t reconnect_count{0};
  double rtt_latest_ms{0};
  double rtt_min_ms{0};
  double rtt_avg_ms{0};
  double rtt_max_ms{0};
  double rtt_p50_ms{0};
  double rtt_p95_ms{0};
  double rtt_p99_ms{0};
  std::string transport{"none"};
  std::vector<double> rtt_samples_ms;

  void AddRtt(double ms) {
    rtt_latest_ms = ms;
    if (rtt_samples_ms.empty()) {
      rtt_min_ms = ms;
      rtt_max_ms = ms;
    } else {
      rtt_min_ms = std::min(rtt_min_ms, ms);
      rtt_max_ms = std::max(rtt_max_ms, ms);
    }
    rtt_samples_ms.push_back(ms);
    if (rtt_samples_ms.size() > kMaxRttSamples) {
      rtt_samples_ms.erase(rtt_samples_ms.begin());
    }
    double sum = 0;
    for (double v : rtt_samples_ms) {
      sum += v;
    }
    rtt_avg_ms = sum / static_cast<double>(rtt_samples_ms.size());
    auto sorted = rtt_samples_ms;
    std::sort(sorted.begin(), sorted.end());
    rtt_p50_ms = Percentile(sorted, 50);
    rtt_p95_ms = Percentile(sorted, 95);
    rtt_p99_ms = Percentile(sorted, 99);
  }
};

struct InFlightPing {
  std::uint64_t send_mono_ns{0};
  std::uint64_t sequence{0};
};

class BrowserPingPongApp {
 public:
  void Configure(std::string profile, std::string gateway_url,
                 std::string transport) {
    profile_ = std::move(profile);
    if (profile_.empty()) {
      profile_ = "A";
    }
    gateway_url_ = std::move(gateway_url);
    transport_ = std::move(transport);
  }

  void SetRemoteUid(std::string uid) { remote_uid_str_ = std::move(uid); }

  void SetPingTimeoutMs(int timeout_ms) {
    if (timeout_ms > 0) {
      ping_timeout_ms_ = timeout_ms;
    }
  }

  void SetPayloadBytes(int payload_bytes) {
    if (payload_bytes >= 0 &&
        static_cast<std::size_t>(payload_bytes) <=
            kMaxFrameSize - kFrameHeaderSize) {
      payload_bytes_ = payload_bytes;
    }
  }

  void Start() {
    if (state_ != AppState::kIdle && state_ != AppState::kError) {
      return;
    }
    SetState(AppState::kLoadingStorage, "Mounting IDBFS profile");
#if defined(__EMSCRIPTEN__)
    emscripten_storage::AcquireProfileLock(
        profile_, [this](emscripten_storage::OpResult lock_result) {
          if (!lock_result.ok) {
            SetState(AppState::kError,
                     std::string{"Profile lock failed: "} +
                         lock_result.message);
            return;
          }
          if (!emscripten_storage::MountProfile(profile_)) {
            emscripten_storage::ReleaseProfileLock();
            SetState(AppState::kError, "Invalid profile name");
            return;
          }
          emscripten_storage::SyncFromIdb(
              [this](emscripten_storage::OpResult result) {
                if (!result.ok) {
                  SetState(AppState::kError,
                           std::string{"IDBFS sync failed: "} + result.message);
                  return;
                }
                OnStorageReady();
              });
        });
#else
    OnStorageReady();
#endif
  }

  void Connect() {
    if (state_ != AppState::kReady && state_ != AppState::kConnected &&
        state_ != AppState::kError) {
      return;
    }
    if (!client_) {
      SetState(AppState::kError, "Client not ready");
      return;
    }
    if (remote_uid_str_.size() != 36) {
      SetState(AppState::kError, "Invalid remote UID");
      return;
    }
    auto remote = Uid::FromString(remote_uid_str_);
    if (remote.empty()) {
      SetState(AppState::kError, "Invalid remote UID");
      return;
    }
    remote_uid_ = remote;
    SetState(AppState::kConnecting, "Opening P2P stream");
    OpenP2pStream();
  }

  void SendPing() {
    if (state_ != AppState::kConnected || !stream_) {
      return;
    }
    ExpireInFlight();
    if (in_flight_.size() >= kMaxInFlight) {
      return;
    }
    PingPongFrame frame;
    frame.type = FrameType::kPing;
    frame.session = kSessionId;
    frame.sequence = next_sequence_++;
    frame.timestamp_mono_ns = MonoNs();
    frame.payload.resize(static_cast<std::size_t>(payload_bytes_));
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : frame.payload) {
      b = static_cast<std::uint8_t>(dist(rng_));
    }
    auto encoded = EncodeFrame(frame);
    if (!encoded) {
      return;
    }
    in_flight_[frame.sequence] =
        InFlightPing{frame.timestamp_mono_ns, frame.sequence};
    ++stats_.sent;
    stream_->Write(std::move(*encoded));
    RefreshStatsJson();
  }

  void StartPeriodic(int interval_ms) {
    if (interval_ms < 10) {
      interval_ms = 10;
    }
    periodic_interval_ms_ = interval_ms;
    periodic_enabled_ = true;
  }

  void Stop() {
    periodic_enabled_ = false;
    in_flight_.clear();
  }

  void ClearProfile() {
    Stop();
    stream_.reset();
    stream_subs_.Reset();
    new_port_sub_.Reset();
    select_sub_.Reset();
    client_ = {};
    uid_str_.clear();
    if (app_) {
      app_->Exit(0);
      app_.reset();
    }
    loop_started_ = false;
#if defined(__EMSCRIPTEN__)
    EM_ASM(
        {
          try {
            var path = UTF8ToString($0);
            function rmrf(p) {
              if (!FS.analyzePath(p).exists) {
                return;
              }
              var st = FS.stat(p);
              if (FS.isDir(st.mode)) {
                (FS.readdir(p) || []).forEach(function(name) {
                  if (name === '.' || name === '..') {
                    return;
                  }
                  rmrf(p + '/' + name);
                });
                FS.rmdir(p);
              } else {
                FS.unlink(p);
              }
            }
            rmrf(path + '/state');
          } catch (e) {
          }
        },
        emscripten_storage::ProfileMountPath().c_str());
    emscripten_storage::SyncToIdb([](emscripten_storage::OpResult) {});
    emscripten_storage::ReleaseProfileLock();
#endif
    stats_ = {};
    next_sequence_ = 1;
    expected_pong_sequence_ = 1;
    SetState(AppState::kIdle, "Profile cleared");
    RefreshStatsJson();
  }

  void OnTick() {
    ExpireInFlight();
    if (periodic_enabled_ && state_ == AppState::kConnected) {
      auto const now = std::chrono::steady_clock::now();
      if (now - last_periodic_ >=
          std::chrono::milliseconds{periodic_interval_ms_}) {
        last_periodic_ = now;
        SendPing();
      }
    }
#if defined(__EMSCRIPTEN__)
    static int ticks = 0;
    if (++ticks % 40 == 0) {
      emscripten_storage::SyncToIdb([](emscripten_storage::OpResult) {});
    }
#endif
  }

  char const* uid() const { return uid_str_.c_str(); }
  char const* state() const { return state_str_.c_str(); }
  char const* stats_json() const { return stats_json_.c_str(); }
  char const* profile() const { return profile_.c_str(); }

 private:
  void SetState(AppState state, std::string detail) {
    state_ = state;
    state_detail_ = std::move(detail);
    state_str_ = StateLabel(state);
    if (!state_detail_.empty()) {
      state_str_ += ": ";
      state_str_ += state_detail_;
    }
  }

  void OnStorageReady() {
    SetState(AppState::kLoadingCrypto, "Waiting for sodium.ready (JS)");
    OnCryptoReady();
  }

  void OnCryptoReady() {
    SetState(AppState::kConstructApp, "Constructing AetherApp");
    auto gateway = ParseGateway(gateway_url_, Protocol::kWebSocket);
    auto transport = ParseTransport(transport_);
    if (!gateway) {
      SetState(AppState::kError, "Invalid gateway URL");
      return;
    }
    if (transport) {
      gateway->protocol = *transport;
    }
    gateway_endpoint_ =
        Endpoint{{Address{gateway->addr}, gateway->port}, gateway->protocol};
    stats_.transport = ProtocolName(gateway->protocol);

    try {
      app_ = AetherApp::Construct(
          AetherAppContext{}
#if AE_DISTILLATION
              .AddAdapterFactory([](AetherAppContext const& context) {
                return BrowserAdapter::ptr::Create(
                    CreateWith{context.domain()}.with_id(
                        GlobalId::kBrowserAdapter),
                    context.aether());
              })
#  if AE_SUPPORT_REGISTRATION
              .RegistrationCloudFactory(
                  [this](AetherAppContext const& context) {
                    auto reg_c = RegistrationCloud::ptr::Create(
                        CreateWith{context.domain()}
                            .with_id(GlobalId::kRegistrationCloud)
                            .with_flags(ObjFlags::kUnloadedByDefault),
                        context.aether());
                    reg_c->AddServerSettings(gateway_endpoint_);
                    return reg_c;
                  })
#  endif
#endif
      );
    } catch (...) {
      SetState(AppState::kError, "AetherApp::Construct failed");
      return;
    }

    if (!loop_started_) {
      loop_started_ = true;
#if defined(__EMSCRIPTEN__)
      RunAetherBrowserLoop(*app_, [this]() { OnTick(); });
#endif
    }

    SetState(AppState::kSelectClient, "SelectClient in progress");
    auto client_id = std::string{"browser-"} + profile_;
    auto& action =
        app_->aether()->SelectClient(kParentUid, std::move(client_id));
    select_sub_ = action.result_event().Subscribe(
        [this](Result<ObjPtr<Client>, int> const& result) {
          if (!result) {
            SetState(AppState::kError,
                     Format("SelectClient failed code={}", result.error()));
            return;
          }
          client_ = result.value();
          uid_str_ = Format("{}", client_->uid());
          MaybeWarnTcpUdpOnly();
          InjectBrowserEndpointIntoWorkCloud();
          ListenForIncomingPorts();
          SetState(AppState::kReady, "Set remote UID and Connect");
          RefreshStatsJson();
        });
  }

  void MaybeWarnTcpUdpOnly() {
    if (!client_) {
      return;
    }
    if (CloudHasOnlyTcpUdp(client_->cloud())) {
      cloud_tcp_udp_only_ = true;
      SetState(state_,
               "Work cloud advertises only TCP/UDP; injected browser gateway "
               "override for connection path");
    } else {
      cloud_tcp_udp_only_ = false;
    }
  }

  void InjectBrowserEndpointIntoWorkCloud() {
    if (!client_ || !app_) {
      return;
    }
    auto cloud = client_->cloud();
    if (!cloud.is_valid()) {
      return;
    }
    bool already = false;
    for (auto const& [id, entry] : cloud->servers()) {
      if (id == kBrowserOverrideServerId) {
        already = true;
        break;
      }
      auto server = entry.server.Load();
      if (!server) {
        continue;
      }
      for (auto const& ep : server->endpoints) {
        if (EndpointIsBrowserCapable(ep)) {
          already = true;
          break;
        }
      }
      if (already) {
        break;
      }
    }
    if (already) {
      return;
    }
    auto server = Server::ptr::Create(
        app_->domain(), kBrowserOverrideServerId,
        std::vector<Endpoint>{gateway_endpoint_},
        app_->aether()->adapter_registry);
    if (server.is_valid()) {
      cloud->AddServer(server);
    }
  }

  void ListenForIncomingPorts() {
    if (!client_) {
      return;
    }
    new_port_sub_ =
        client_->message_stream_manager().new_port_event().Subscribe(
            [this](P2pPortHandle handle) {
              if (stream_) {
                return;
              }
              BindStream(std::make_shared<P2pStream>(
                  AeContext{*app_->aether()}, client_.Load(),
                  handle.destination(), std::move(handle)));
              SetState(AppState::kConnected, "Accepted inbound P2P port");
            });
  }

  void OpenP2pStream() {
    if (!client_ || !app_) {
      return;
    }
    auto port = client_->message_stream_manager().CreatePort(remote_uid_);
    BindStream(std::make_shared<P2pStream>(AeContext{*app_->aether()},
                                          client_.Load(), remote_uid_,
                                          std::move(port)));
    auto info = stream_->stream_info();
    if (info.link_state == LinkState::kLinked) {
      SetState(AppState::kConnected, "P2P stream linked");
    } else {
      SetState(AppState::kConnecting, "Waiting for P2P link");
    }
  }

  void BindStream(std::shared_ptr<P2pStream> stream) {
    stream_ = std::move(stream);
    was_linked_ = stream_->stream_info().link_state == LinkState::kLinked;
    stream_subs_.Reset();
    stream_subs_ += stream_->out_data_event().Subscribe(
        [this](DataBuffer const& data) { OnStreamData(data); });
    stream_subs_ += stream_->stream_update_event().Subscribe([this]() {
      if (!stream_) {
        return;
      }
      auto const info = stream_->stream_info();
      bool const linked = info.link_state == LinkState::kLinked;
      if (linked && !was_linked_) {
        if (seen_unlink_) {
          ++stats_.reconnect_count;
          RefreshStatsJson();
        }
        SetState(AppState::kConnected, "P2P stream linked");
      } else if (!linked && was_linked_) {
        seen_unlink_ = true;
        if (info.link_state == LinkState::kLinkError) {
          SetState(AppState::kConnecting, "P2P link error; waiting reconnect");
        } else {
          SetState(AppState::kConnecting, "P2P unlinked");
        }
      }
      was_linked_ = linked;
    });
  }

  void OnStreamData(DataBuffer const& data) {
    auto frame = DecodeFrame(data);
    if (!frame) {
      ++stats_.malformed;
      RefreshStatsJson();
      return;
    }
    if (frame->type == FrameType::kPing) {
      ReplyPong(*frame);
      return;
    }
    if (frame->type != FrameType::kPong) {
      ++stats_.malformed;
      RefreshStatsJson();
      return;
    }
    auto it = in_flight_.find(frame->sequence);
    if (it == in_flight_.end()) {
      ++stats_.duplicate;
      RefreshStatsJson();
      return;
    }
    if (frame->sequence < expected_pong_sequence_) {
      ++stats_.out_of_order;
    } else {
      expected_pong_sequence_ = frame->sequence + 1;
    }
    auto const now = MonoNs();
    auto const send_ns = frame->timestamp_mono_ns != 0
                             ? frame->timestamp_mono_ns
                             : it->second.send_mono_ns;
    double const rtt_ms =
        static_cast<double>(now - send_ns) / 1'000'000.0;
    in_flight_.erase(it);
    ++stats_.pong_received;
    stats_.AddRtt(rtt_ms);
    RefreshStatsJson();
  }

  void ReplyPong(PingPongFrame const& ping) {
    if (!stream_) {
      return;
    }
    PingPongFrame pong = ping;
    pong.type = FrameType::kPong;
    auto encoded = EncodeFrame(pong);
    if (!encoded) {
      return;
    }
    stream_->Write(std::move(*encoded));
  }

  void ExpireInFlight() {
    if (in_flight_.empty()) {
      return;
    }
    auto const now = MonoNs();
    auto const timeout_ns =
        static_cast<std::uint64_t>(ping_timeout_ms_) * 1'000'000ull;
    for (auto it = in_flight_.begin(); it != in_flight_.end();) {
      if (now - it->second.send_mono_ns >= timeout_ns) {
        ++stats_.timed_out;
        it = in_flight_.erase(it);
        RefreshStatsJson();
      } else {
        ++it;
      }
    }
  }

  void RefreshStatsJson() {
    char rtt_buf[256];
    std::snprintf(
        rtt_buf, sizeof(rtt_buf),
        "\"rtt_latest_ms\":%.3f,\"rtt_min_ms\":%.3f,\"rtt_avg_ms\":%.3f,"
        "\"rtt_p50_ms\":%.3f,\"rtt_p95_ms\":%.3f,\"rtt_p99_ms\":%.3f,"
        "\"rtt_max_ms\":%.3f",
        stats_.rtt_latest_ms, stats_.rtt_min_ms, stats_.rtt_avg_ms,
        stats_.rtt_p50_ms, stats_.rtt_p95_ms, stats_.rtt_p99_ms,
        stats_.rtt_max_ms);
    stats_json_ = Format(
        R"({{"sent":{},"pong_received":{},"timed_out":{},"duplicate":{},)"
        R"("out_of_order":{},"malformed":{},{},"transport":"{}",)"
        R"("reconnect_count":{},"cloud_tcp_udp_only":{}}})",
        stats_.sent, stats_.pong_received, stats_.timed_out, stats_.duplicate,
        stats_.out_of_order, stats_.malformed, rtt_buf, stats_.transport,
        stats_.reconnect_count, cloud_tcp_udp_only_ ? "true" : "false");
  }

  AppState state_{AppState::kIdle};
  std::string state_str_{"Idle"};
  std::string state_detail_;
  std::string profile_{"A"};
  std::string gateway_url_{"ws://127.0.0.1:8080"};
  std::string transport_{"WS"};
  std::string remote_uid_str_;
  std::string uid_str_;
  std::string stats_json_{"{}"};
  Endpoint gateway_endpoint_{};
  bool cloud_tcp_udp_only_{false};
  bool loop_started_{false};
  bool periodic_enabled_{false};
  bool was_linked_{false};
  bool seen_unlink_{false};
  int periodic_interval_ms_{200};
  int ping_timeout_ms_{kDefaultPingTimeoutMs};
  int payload_bytes_{kDefaultPayloadBytes};
  std::uint64_t next_sequence_{1};
  std::uint64_t expected_pong_sequence_{1};
  std::chrono::steady_clock::time_point last_periodic_{};
  std::mt19937 rng_{std::random_device{}()};
  Stats stats_{};
  std::map<std::uint64_t, InFlightPing> in_flight_;
  std::unique_ptr<AetherApp> app_;
  Client::ptr client_;
  Uid remote_uid_{};
  std::shared_ptr<P2pStream> stream_;
  Subscription select_sub_;
  Subscription new_port_sub_;
  MultiSubscription stream_subs_;
};

BrowserPingPongApp& App() {
  static BrowserPingPongApp instance;
  return instance;
}

}  // namespace
}  // namespace ae::examples::browser_ping_pong

using ae::examples::browser_ping_pong::App;

extern "C" {

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void aether_bpp_configure(char const* profile, char const* gateway_url,
                          char const* transport) {
  App().Configure(profile ? profile : "A", gateway_url ? gateway_url : "",
                  transport ? transport : "WS");
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void aether_bpp_start(void) { App().Start(); }

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void aether_bpp_set_remote_uid(char const* uid) {
  App().SetRemoteUid(uid ? uid : "");
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void aether_bpp_connect(void) { App().Connect(); }

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void aether_bpp_send_ping(void) { App().SendPing(); }

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void aether_bpp_start_periodic(int interval_ms) {
  App().StartPeriodic(interval_ms);
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void aether_bpp_stop(void) { App().Stop(); }

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void aether_bpp_clear_profile(void) { App().ClearProfile(); }

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void aether_bpp_set_ping_timeout_ms(int timeout_ms) {
  App().SetPingTimeoutMs(timeout_ms);
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void aether_bpp_set_payload_bytes(int payload_bytes) {
  App().SetPayloadBytes(payload_bytes);
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
char const* aether_bpp_get_uid(void) { return App().uid(); }

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
char const* aether_bpp_get_state(void) { return App().state(); }

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
char const* aether_bpp_get_stats_json(void) { return App().stats_json(); }

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
char const* aether_bpp_get_profile(void) { return App().profile(); }

}  // extern "C"
