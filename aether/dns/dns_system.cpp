#include "aether/dns/dns_c_ares.h"

#if defined DNS_RESOLVE_SYSTEM_ENABLED
#  include <cstring>
#  include <functional>
#  include <memory>
#  include <mutex>
#  include <string>
#  include <utility>
#  include <vector>

#  if defined(_WIN32)
#    include <windows.h>
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <bit>
#    include "aether/socket_initializer.h"
#  elif defined(__APPLE__)
#    include <dispatch/dispatch.h>
#    include <dns_sd.h>
#    include <netinet/in.h>
#  else
#    include <netdb.h>
#    include <netinet/in.h>
#    include <pthread.h>
#    include <atomic>
#  endif

#  include "aether/aether.h"
#  include "aether/dns/cares_query_completion.h"

namespace ae {
namespace {

using Resolution = Result<std::vector<Endpoint>, int>;
using Complete = std::function<void(Resolution)>;
using Cancel = std::function<void()>;

struct Lookup {
  std::string host;
  std::uint16_t port;
  Protocol protocol;
  Complete complete;
};

void AddEndpoint(std::vector<Endpoint>& endpoints, sockaddr const* address,
                 std::uint16_t port, Protocol protocol) {
  if (!address) {
    return;
  }
#  if AE_SUPPORT_IPV4
  if (address->sa_family == AF_INET) {
    auto const& native = *reinterpret_cast<sockaddr_in const*>(address);
    IpV4Addr ip{};
    std::memcpy(&ip.ipv4_value, &native.sin_addr, sizeof(ip.ipv4_value));
    endpoints.push_back(Endpoint{{ip, port}, protocol});
  }
#  endif
#  if AE_SUPPORT_IPV6
  if (address->sa_family == AF_INET6) {
    auto const& native = *reinterpret_cast<sockaddr_in6 const*>(address);
    IpV6Addr ip{};
    std::memcpy(&ip.ipv6_value, &native.sin6_addr, sizeof(ip.ipv6_value));
    endpoints.push_back(Endpoint{{ip, port}, protocol});
  }
#  endif
}

#  if !defined(__APPLE__)
template <typename Address>
Resolution Decode(Lookup const& lookup, Address const* addresses) {
  std::vector<Endpoint> endpoints;
  for (auto* address = addresses; address; address = address->ai_next) {
    AddEndpoint(endpoints, address->ai_addr, lookup.port, lookup.protocol);
  }
  if (endpoints.empty()) {
    return Error{1};
  }
  return Ok{std::move(endpoints)};
}

void RunBlocking(Lookup& lookup) {
#    if defined(_WIN32)
  SocketInitializer sockets;
#    endif
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses{};
  if (getaddrinfo(lookup.host.c_str(), nullptr, &hints, &addresses) != 0) {
    lookup.complete(Error{1});
    return;
  }
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> result{addresses,
                                                            freeaddrinfo};
  lookup.complete(Decode(lookup, result.get()));
}
#  endif

#  if defined(_WIN32)
struct WinsockAsync {
  using CancelLookup = INT(WSAAPI*)(LPHANDLE);
  using GetResult = INT(WSAAPI*)(LPOVERLAPPED);

  template <typename F>
  static F Load(HMODULE module, char const* name) {
    return std::bit_cast<F>(GetProcAddress(module, name));
  }

  static WinsockAsync const& Get() {
    static auto const api = [] {
      auto module = GetModuleHandleW(L"ws2_32.dll");
      return WinsockAsync{
          Load<decltype(&GetAddrInfoExW)>(module, "GetAddrInfoExW"),
          Load<CancelLookup>(module, "GetAddrInfoExCancel"),
          Load<GetResult>(module, "GetAddrInfoExOverlappedResult"),
          Load<decltype(&FreeAddrInfoExW)>(module, "FreeAddrInfoExW")};
    }();
    return api;
  }

  decltype(&GetAddrInfoExW) lookup;
  CancelLookup cancel;
  GetResult get_result;
  decltype(&FreeAddrInfoExW) release;
};

class WindowsLookup {
 public:
  explicit WindowsLookup(Lookup value) : lookup_{std::move(value)} {}
  ~WindowsLookup() {
    if (addresses_) {
      WinsockAsync::Get().release(addresses_);
    }
    if (wait_) {
      CloseThreadpoolWait(wait_);
    }
    if (overlapped_.hEvent) {
      CloseHandle(overlapped_.hEvent);
    }
  }

  static Cancel Start(Lookup lookup) {
    auto const& api = WinsockAsync::Get();
    if (!api.lookup || !api.cancel || !api.get_result || !api.release) {
      return StartWorker(std::move(lookup));
    }
    auto request = std::make_shared<WindowsLookup>(std::move(lookup));
    auto size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            request->lookup_.host.c_str(), -1, nullptr, 0);
    if (size == 0) {
      request->CompleteWith(WSAHOST_NOT_FOUND);
      return {};
    }
    request->name_.resize(static_cast<std::size_t>(size));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        request->lookup_.host.c_str(), -1,
                        request->name_.data(), size);
    request->overlapped_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    request->wait_ = CreateThreadpoolWait(OnComplete, request.get(), nullptr);
    if (!request->overlapped_.hEvent || !request->wait_) {
      request->CompleteWith(WSA_NOT_ENOUGH_MEMORY);
      return {};
    }

    request->hints_.ai_socktype = SOCK_STREAM;
    auto status = api.lookup(request->name_.c_str(), nullptr, NS_ALL, nullptr,
                             &request->hints_, &request->addresses_, nullptr,
                             &request->overlapped_, nullptr, &request->cancel_);
    if (status != WSA_IO_PENDING) {
      request->CompleteWith(status);
      return {};
    }

    request->pending_ = request;
    SetThreadpoolWait(request->wait_, request->overlapped_.hEvent, nullptr);
    return [request] {
      auto lock = std::scoped_lock{request->mutex_};
      if (request->cancel_) {
        WinsockAsync::Get().cancel(&request->cancel_);
      }
    };
  }

 private:
  static Cancel StartWorker(Lookup lookup) {
    auto* work = new Lookup{std::move(lookup)};
    if (!TrySubmitThreadpoolCallback(RunWorker, work, nullptr)) {
      std::unique_ptr<Lookup> cleanup{work};
      cleanup->complete(Error{1});
    }
    return {};
  }
  static void CALLBACK RunWorker(PTP_CALLBACK_INSTANCE callback,
                                 void* context) {
    std::unique_ptr<Lookup> work{static_cast<Lookup*>(context)};
    CallbackMayRunLong(callback);
    RunBlocking(*work);
  }
  static void CALLBACK OnComplete(PTP_CALLBACK_INSTANCE, void* context,
                                  PTP_WAIT, TP_WAIT_RESULT) {
    auto* request = static_cast<WindowsLookup*>(context);
    auto pending = std::move(request->pending_);
    {
      auto lock = std::scoped_lock{request->mutex_};
      request->cancel_ = nullptr;
    }
    request->CompleteWith(
        WinsockAsync::Get().get_result(&request->overlapped_));
  }
  void CompleteWith(int status) {
    if (status == 0) {
      lookup_.complete(Decode(lookup_, addresses_));
    } else {
      lookup_.complete(Error{1});
    }
  }

  Lookup lookup_;
  SocketInitializer sockets_;
  std::wstring name_;
  ADDRINFOEXW hints_{};
  PADDRINFOEXW addresses_{};
  OVERLAPPED overlapped_{};
  PTP_WAIT wait_{};
  std::mutex mutex_;
  HANDLE cancel_{};
  std::shared_ptr<WindowsLookup> pending_;
};

Cancel StartSystem(Lookup lookup) {
  return WindowsLookup::Start(std::move(lookup));
}

#  elif defined(__APPLE__)
dispatch_queue_t DnsQueue() {
  static auto queue =
      dispatch_queue_create("io.aethernet.dns", DISPATCH_QUEUE_SERIAL);
  return queue;
}

class AppleLookup {
 public:
  explicit AppleLookup(Lookup value) : lookup_{std::move(value)} {}

  static Cancel Start(Lookup lookup) {
    auto request = std::make_shared<AppleLookup>(std::move(lookup));
    Dispatch(request, [](AppleLookup& operation) { operation.Begin(); });
    return [request] {
      Dispatch(request, [](AppleLookup& operation) { operation.Close(); });
    };
  }

 private:
  struct Family {
    AppleLookup* owner;
    DNSServiceRef service{};
    bool done{false};
  };

  template <typename F>
  static void Dispatch(std::shared_ptr<AppleLookup> request, F function) {
    using Work = std::pair<std::shared_ptr<AppleLookup>, F>;
    auto* work = new Work{std::move(request), function};
    dispatch_async_f(DnsQueue(), work, [](void* context) {
      std::unique_ptr<Work> item{static_cast<Work*>(context)};
      item->second(*item->first);
    });
  }

  void Begin() {
#    if AE_SUPPORT_IPV4
    BeginFamily(ip4_, kDNSServiceProtocol_IPv4);
#    else
    ip4_.done = true;
#    endif
#    if AE_SUPPORT_IPV6
    BeginFamily(ip6_, kDNSServiceProtocol_IPv6);
#    else
    ip6_.done = true;
#    endif
    FinishIfReady();
  }

  void BeginFamily(Family& family, DNSServiceProtocol protocol) {
    auto status =
        DNSServiceGetAddrInfo(&family.service, 0, 0, protocol,
                              lookup_.host.c_str(), OnResult, &family);
    if (status == kDNSServiceErr_NoError) {
      status = DNSServiceSetDispatchQueue(family.service, DnsQueue());
    }
    family.done = status != kDNSServiceErr_NoError;
  }

  static void DNSSD_API OnResult(DNSServiceRef, DNSServiceFlags flags,
                                 std::uint32_t, DNSServiceErrorType error,
                                 char const*, sockaddr const* address,
                                 std::uint32_t, void* context) {
    auto& family = *static_cast<Family*>(context);
    if (family.done) {
      return;
    }
    auto& operation = *family.owner;
    if (error == kDNSServiceErr_NoError && (flags & kDNSServiceFlagsAdd)) {
      AddEndpoint(operation.endpoints_, address, operation.lookup_.port,
                  operation.lookup_.protocol);
    }
    family.done = error != kDNSServiceErr_NoError ||
                  !(flags & kDNSServiceFlagsMoreComing);
    operation.FinishIfReady();
  }

  void FinishIfReady() {
    if (!ip4_.done || !ip6_.done) {
      return;
    }
    Close();
    if (endpoints_.empty()) {
      lookup_.complete(Error{1});
    } else {
      lookup_.complete(Ok{std::move(endpoints_)});
    }
  }

  void Close() {
    for (auto* family : {&ip4_, &ip6_}) {
      family->done = true;
      if (family->service) {
        DNSServiceRefDeallocate(std::exchange(family->service, nullptr));
      }
    }
  }

  Lookup lookup_;
  Family ip4_{this};
  Family ip6_{this};
  std::vector<Endpoint> endpoints_;
};

Cancel StartSystem(Lookup lookup) {
  return AppleLookup::Start(std::move(lookup));
}

#  else
// Scope each detached worker to a single lookup. The sender's completion state
// can outlive the scheduler and discards results after cancellation.
std::atomic_uint active_lookups{0};
constexpr unsigned kMaxActiveLookups = 16;

void* RunWorker(void* context) {
  std::unique_ptr<Lookup> lookup{static_cast<Lookup*>(context)};
  RunBlocking(*lookup);
  --active_lookups;
  return nullptr;
}

Cancel StartSystem(Lookup lookup) {
  if (active_lookups.fetch_add(1) >= kMaxActiveLookups) {
    --active_lookups;
    lookup.complete(Error{1});
    return {};
  }
  auto* work = new Lookup{std::move(lookup)};
  pthread_t worker{};
  if (pthread_create(&worker, nullptr, RunWorker, work) != 0) {
    --active_lookups;
    std::unique_ptr<Lookup> cleanup{work};
    cleanup->complete(Error{1});
  } else {
    pthread_detach(worker);
  }
  return {};
}
#  endif

struct SystemSender {
  using sender_concept = stdexec::sender_t;
  using completion_signatures =
      stdexec::completion_signatures<ex::set_value_t(std::vector<Endpoint>&&),
                                     ex::set_error_t(int)>;

  template <typename R>
  struct Operation {
    using Completion = cares_query_completion_internal::QueryCompletion<R>;
    Operation(R receiver, TaskScheduler& scheduler, NamedAddr value,
              std::uint16_t port, Protocol protocol)
        : completion{std::move(receiver), scheduler},
          lookup{std::move(value.name), port, protocol,
                 [state = completion.state()](Resolution result) {
                   Completion::Deliver(state, std::move(result));
                 }} {}
    ~Operation() {
      if (cancel) {
        cancel();
      }
    }

    void start() & noexcept { cancel = StartSystem(std::move(lookup)); }

    Completion completion;
    Lookup lookup;
    Cancel cancel;
  };

  template <typename R>
  auto connect(R&& receiver) && noexcept {
    return Operation<R>{std::forward<R>(receiver), *scheduler, std::move(host),
                        port, protocol};
  }

  TaskScheduler* scheduler;
  NamedAddr host;
  std::uint16_t port;
  Protocol protocol;
};

}  // namespace

// The serialized resolver class is shared with the c-ares backend. A SYSTEM
// build has no c-ares channel to allocate.
class AresImpl {};
DnsResolverCares::DnsResolverCares() = default;
#  if defined AE_DISTILLATION
DnsResolverCares::DnsResolverCares(ObjProp prop, ObjPtr<Aether> aether)
    : DnsResolver{prop}, aether_{std::move(aether)} {}
#  endif
DnsResolverCares::~DnsResolverCares() = default;

ResolveSender DnsResolverCares::Resolve(NamedAddr const& host,
                                        std::uint16_t port, Protocol protocol) {
  return SystemSender{&AeContext{*aether_.Load()}.scheduler(), host, port,
                      protocol};
}

}  // namespace ae
#endif
