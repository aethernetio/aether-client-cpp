#define AE_EXAMPLE_LORA_MODULE 0
#define AE_EXAMPLE_MODEM 0
#ifdef ESP_PLATFORM
#  define AE_EXAMPLE_ESP_WIFI 1
#else
#  define AE_EXAMPLE_ETHERNET 1
#endif

#include <algorithm>
#include <iostream>
#include <memory>
#include <string_view>

#include "aether-miscpp/format/format.h"
#include "aether-miscpp/misc/override.h"
#include "aether/all.h"

// IWYU pragma: begin_keeps
#include "../common/aether_construct_esp_wifi.h"
#include "../common/aether_construct_ethernet.h"
#include "../common/aether_construct_lora_module.h"
#include "../common/aether_construct_modem.h"
// IWYU pragma: end_keeps

namespace ae::examples {

static constexpr auto kParentUid =
    ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");

static constexpr auto kStageTimeout = std::chrono::seconds{5};

template <typename... Args>
void Log(ae::FormatScheme const& format, Args&&... args) {
  ae::Format(std::cout, ">>> [{:time}] ", ae::Now());
  ae::Format(std::cout, format, std::forward<Args>(args)...);
  std::cout << '\n';
}

static std::string MakeMessage(std::string_view client_name, int message_num) {
  return ae::Format("Message from {} num {} {:time}", client_name, message_num,
                    ae::Now());
}

static ae::DataBuffer ToDataBuffer(std::string_view text) {
  return {text.begin(), text.end()};
}

static std::string_view ToString(ae::DataBuffer const& data) {
  return {reinterpret_cast<char const*>(data.data()), data.size()};
}

struct State {
  explicit State(ae::AetherApp& app) : aether{app.aether()} {}

  ae::Aether::ptr aether;
  ae::Client::ptr client_a;
  ae::Client::ptr client_b;
  std::shared_ptr<ae::P2pStream> a_stream;
  std::shared_ptr<ae::P2pStream> b_stream;
  Event<void(std::string_view message)> a_received_event;
  Event<void(std::string_view message)> b_received_event;
};

static auto SelectClientSender(State* state, std::string_view name,
                               ae::Client::ptr State::* out_client) {
  return ae::ex::let_value([=]() noexcept {
           Log("client.select.start name={}", name);
           return ae::ex::action_wait(
               state->aether->SelectClient(kParentUid, std::string{name}));
         }) |
         ex::then([=](Client::ptr const& client) noexcept {
           state->*out_client = client;
           Log("client.select.done name={} uid={}", name, client->uid());
         }) |
         ex::let_error([=](auto&&...) noexcept {
           Log("client.select.error name={}", name);
           return ex::just_error(1);
         }) |
         ae::ex::with_timeout(ae::AeContext{*state->aether}, kStageTimeout);
}

static void OpenAStream(State* state) {
  state->a_stream = std::make_shared<ae::P2pStream>(
      *state->aether, state->client_a.Load(), state->client_b->uid(),
      state->client_a->message_stream_manager().CreatePort(
          state->client_b->uid()));
  // subscribe to data receive
  state->a_stream->out_data_event().Subscribe([state](DataBuffer const& data) {
    state->a_received_event.Emit(ToString(data));
  });
  Log("stream.open.done side=A");

  // subscribe to open new message stream
  state->client_b->message_stream_manager().new_port_event().Subscribe(
      [state](ae::P2pPortHandle handle) {
        state->b_stream = std::make_shared<ae::P2pStream>(
            *state->aether, state->client_b.Load(), handle.destination(),
            std::move(handle));

        // subscribe to data receive
        state->b_stream->out_data_event().Subscribe(
            [state](DataBuffer const& data) {
              state->b_received_event.Emit(ToString(data));
            });
        Log("stream.open.done side=B");
      });
}

static auto SendMessageAtoB(State* state, int message_num) {
  return ae::ex::let_value([=]() noexcept {
    return ae::ex::create<ae::ex::set_value_t(), ae::ex::set_error_t(int)>(
               [=, test_sub_ = Subscription{}](auto& ctx) mutable noexcept {
                 auto text = MakeMessage("A", message_num);
                 Log("message.A_to_B.send.start num={} text=[{}]", message_num,
                     text);

                 // Expect B receive the message
                 test_sub_ =
                     ae::EventSubscriber{state->b_received_event}.Subscribe(
                         [&](std::string_view message) noexcept {
                           Log("message.A_to_B.receive.done num={} text=[{}]",
                               message_num, message);
                           return ae::ex::set_value(std::move(ctx.receiver));
                         });

                 state->a_stream->Write(ToDataBuffer(text))
                     .status_event()
                     .Subscribe([&, state](auto status) {
                       if (status == ae::WriteAction::Status::kFail) {
                         return ae::ex::set_error(std::move(ctx.receiver), 2);
                       }
                     });
               }) |
           ae::ex::with_timeout(ae::AeContext{*state->aether}, kStageTimeout);
  });
}

static auto SendMessageBtoA(State* state, int message_num) {
  return ae::ex::let_value([=]() noexcept {
    return ae::ex::create<ae::ex::set_value_t(), ae::ex::set_error_t(int)>(
               [=, test_sub_ = Subscription{}](auto& ctx) mutable noexcept {
                 auto text = MakeMessage("B", message_num);
                 Log("message.B_to_A.send.start num={} text=[{}]", message_num,
                     text);

                 // Expect A receive the message
                 test_sub_ =
                     ae::EventSubscriber{state->a_received_event}.Subscribe(
                         [&](std::string_view message) noexcept {
                           Log("message.B_to_A.receive.done num={} text=[{}]",
                               message_num, message);
                           return ae::ex::set_value(std::move(ctx.receiver));
                         });

                 if (!state->b_stream) {
                   Log("message.B_to_A.send.failed B has no stream to A");
                   return ae::ex::set_error(std::move(ctx.receiver), 3);
                 }

                 state->b_stream->Write(ToDataBuffer(text))
                     .status_event()
                     .Subscribe([&, state](auto status) {
                       if (status == ae::WriteAction::Status::kFail) {
                         return ae::ex::set_error(std::move(ctx.receiver), 2);
                       }
                     });
               }) |
           ae::ex::with_timeout(ae::AeContext{*state->aether}, kStageTimeout);
  });
}

}  // namespace ae::examples

int AetherABMessageExchangeExample() {
  using namespace ae::examples;  // NOLINT
  Log("app.create.start");
  auto aether_app = ae::examples::construct_aether_app();
  Log("app.create.done");

  State state{*aether_app};

  auto pipeline = ae::ex::just() |
                  SelectClientSender(&state, "A", &State::client_a) |
                  SelectClientSender(&state, "B", &State::client_b) |
                  ae::ex::then([&state]() noexcept { OpenAStream(&state); }) |
                  SendMessageAtoB(&state, 1) | SendMessageBtoA(&state, 1) |
                  SendMessageAtoB(&state, 2) | SendMessageBtoA(&state, 2);
  // asynchronously wait till pipeline is over
  auto waiter = ae::ex::AsyncWaiter{
      ae::AeContext{*aether_app}, std::move(pipeline),
      [&]<typename Res>(std::optional<Res> const& res) noexcept {
        if (!res) {
          Log("exchange.stopped");
          aether_app->Exit(2);
          return;
        }
        if (res->IsOk()) {
          Log("exchange.done");
          aether_app->Exit(0);
        } else {  // error
          std::visit(
              ae::Override{
                  [](ae::ex::TimeoutError) { Log("exchange.fail timeout"); },
                  [](auto&& e) { Log("exchange.fail code={}", e); }},
              res->error());
          aether_app->Exit(1);
        }
      }};

  while (!aether_app->IsExited()) {
    auto next_time = aether_app->Update(ae::Now());
    aether_app->WaitUntil(next_time);
  }

  return aether_app->ExitCode();
}
