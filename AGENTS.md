# Aether Client C++ Guide

## Modes and Persistence

`aether` has three persistence behaviors:

- **Distillation** (`AE_DISTILLATION=On`): create every object from scratch,
  even when persistent state already exists. Use it during development and as
  preparation for production.
- **Filtration** (`AE_FILTRATION=On`): load an object when state exists and
  create it when it does not. Filtration also enables the distillation code
  paths at compile time. It is useful when applications should tolerate both
  existing and missing state.
- **Production**: both `AE_DISTILLATION` and `AE_FILTRATION` are disabled or
  undefined. Main persistent objects, such as `Aether`, adapters, clients, and
  clouds, must already exist. Some argument-taking constructors are disabled;
  objects must be loaded from the domain or copied from prefab objects.

Production is descriptive terminology, not a separate build option.

`FS_INIT` may provide generated or static persisted-state maps. Persistence is
not implied by mutation; use the established application save path when state
must survive shutdown.

## Project Model

`aether` is a C++20 static library for persistent state, asynchronous actions
and tasks, transport, streams, protocol APIs, cloud/server connections,
cryptography, and platform adapters.

Persistent objects represent durable identity, configuration, and state. Runtime
objects perform transient asynchronous work, connection management, stream
processing, and transport operations. Keeping this distinction prevents runtime
state from entering the persisted object graph and prevents durable objects from
being managed as temporary operations.

Actions, streams, and connection/transport logic are runtime objects, not
persistent `Obj` types.

## Persistent Objects and Ownership

- Persistent entities derive from `ae::Obj`, use `AE_OBJECT` and reflection,
  and implement the established `Load`/`Save` patterns.
- Use `ae::ObjPtr<T>` for strong references to persistent objects.
- Use `ae::Ptr<T>` for shared ownership of non-`Obj` objects.
- `ae::PtrView<T>` is a weak, nullable view. Lock/load it before retaining or
  dereferencing the object.
- A valid `ObjPtr` may still refer to an unloaded object. Load it before use
  and retain the loaded pointer while using it.

## Runtime Context

`ae::AeContext` is a non-owning view of `Aether` and its task scheduler. Runtime
components use `ae_context.scheduler()` to schedule work. The context does not
extend the lifetime of `Aether`, the scheduler, or objects captured by callbacks.

Accept a context or operation inputs in a constructor only when the operation
requires them. Follow the surrounding component pattern for constructor-started
work versus an explicit `Start()` method.

## Events and Subscriptions

`Event` owns its handlers. `EventSubscriber` is a non-owning façade used to
subscribe and emit through an owning object.

`Subscribe()` returns `EventHandlerDeleter`. It is lightweight and is **not** an
RAII object: destroying or discarding it does not unsubscribe the handler. Call
Use `Subscription` to control the handler lifetime; do not manage the returned
deleter directly.

Use RAII lifetime control when a callback should have an owner lifetime:

- `Subscription` owns one `EventHandlerDeleter`; destruction/reset unsubscribes.
- `MultiSubscription` owns several deleters and unsubscribes them together.
- A temporary `Subscription` unsubscribes at the end of its scope.
- Retain the subscription for as long as its callback may run.

Define events with a private `Event<void(...)>` member and expose a subscriber:

```cpp
using ChangedEvent = Event<void(Value const&)>;
ChangedEvent::Subscriber changed_event();

private:
ChangedEvent changed_event_;
```

Return `EventSubscriber{changed_event_}` from the accessor and emit from the
owner with `changed_event_.Emit(value)`.

## Actions

`Action` is a move-only runtime operation with completion state and
`finished_event()`. Define a concrete action by deriving from `Action`, adding
only the context, inputs, events, and subscriptions it needs, and following the
existing constructor or `Start()` pattern.

Expose typed result or progress events where callers need them. Always emit the
terminal result before calling `Finish()`:

```cpp
result_event_.Emit(result);
Finish();
```

`Finish()` marks the action finished and emits `finished_event()`. An action may
be deleted after `Finish()`, so do not access it or perform action-dependent work
after that call.

The action owner keeps the action alive until `action.is_finished()` is true.
Share an action by reference when it is guaranteed to exist, or by raw pointer
when it may be absent. Shared action access is non-owning: do not use smart
pointers or wrapper types to share actions with callers.

## ActionPool and ActionsQueue

`ActionPool` provides fixed-capacity storage for actions that must survive the
initiating call. `Create()` returns a non-owning raw pointer and may return
`nullptr` when capacity is exhausted. The pool observes `finished_event()` and
schedules destruction.

`ActionsQueue` sequences operations. It runs stages in
FIFO order, starts the next stage after the current action finishes, observes
`finished_event()`, supports stopping the current action when it provides
`Stop()`, and allows stages to be added dynamically.

## Tasks and Executors

`ManualTaskScheduler` is driven by the application loop: call `Update()`, then
`WaitUntil()` with the returned wake-up time. Tasks must be lightweight and
non-blocking; use delayed tasks for time-based work.

`TaskSubscription` is move-only RAII control for a task. Retain it while its
callback may run. Resetting or destroying it cancels the task; a temporary
subscription therefore cancels work immediately. After execution, the task
invalidates its subscription.

Task storage has static capacity shared by the task queues. Allocation or queue
exhaustion is an exceptional fixed-resource failure, generally not recoverable.
Code may check the returned subscription when that distinction matters; if the
failure is detected, log it and use `assert(false && "Task allocation failed")`.

Include `aether/executors/executors.h` for the stdexec and project executor API.
Use `SchedulerOnTasks` to run sender work on the task scheduler, compose work
with the provided senders and adapters, and complete it through `AsyncWaiter`,
`SyncWaiter`, or `AnyWaiter` as appropriate. Use `WithTimeout` for bounded
operations. The first completion wins; retain the waiter, operation, and
captured state until completion or timeout, and handle timeout separately from
ordinary errors.

## API

- API protocol and server APIs operate over runtime streams.
- API classes derive from `ApiClass` and receive a `ProtocolContext`.
- Client-side API methods are data members of type
  `Method<MessageId, Signature>`. Use `void(Args...)` for fire-and-forget
  methods and `ApiPromise<Result>(Args...)` for methods that return a value or
  an error.
- A return-value method generates a request ID, sends the packed request, and
  returns `ApiPromise<Result>`; callers must use the normal promise/sender/
  waiter path to observe its result or error.
- Define an API class with explicit method IDs and signatures, then initialize
  its methods with the class `ProtocolContext`. Keep message IDs stable and
  unique within the API.
- For server-side dispatch, derive from `ApiClassImpl<ConcreteApi>`, implement
  methods with matching signatures, and register them with `AE_METHODS`:

  ```cpp
  class ExampleApi : public ApiClassImpl<ExampleApi> {
   public:
    explicit ExampleApi(ProtocolContext& protocol_context);

    void Handle(DataBuffer data);
    AE_METHODS(RegMethod<3, &ExampleApi::Handle>);
  };
  ```

- Use `SubApi<T>` and the existing API context/parser patterns for nested API
  calls instead of inventing a separate packet format.

## Streams

- Streams publish state and data through events; writes return actions.
- Use `stream_info()` instead of assuming writability, reliability, link state,
  or supported element sizes.
- Keep linked stream objects alive while links and subscriptions are active;
  unlink them before destruction.

## Cloud and Server Connections

- Cloud connections coordinate server connections and connection policies.
- Server connections manage channels and failover. Determine health from
  connection/stream state, not object existence alone.
- Subscribe to asynchronous result/error events before starting an operation
  and handle both request failures and result-level errors.

## Tele

Tele is the public telemetry facility from the `aether-tele` dependency,
configured through `aether/tele.h`.

- Use regular logs such as `AE_TELED_DEBUG`, `AE_TELED_INFO`, and
  `AE_TELED_ERROR`.
- Register a module tag when tagged logging is needed.
- Use registered tags with `AE_TELE_<LEVEL>(kTag, ...)`.

## C++ Coding Rules

- Follow the Google C++ Style Guide.
- Raw pointers are not an anti-pattern in this project. Use them to express a
  nullable value or a non-owning reference.
- A nullable raw pointer may be checked against `nullptr` before use.
- When a class requires a non-owning reference, accept it as a reference in the
  constructor and store its address as a raw pointer. This expresses the
  non-null requirement in the constructor contract; the referenced object must
  outlive the class that stores the pointer.
- Raw pointers never express ownership. Do not retain them across asynchronous
  boundaries unless the owning lifetime is explicitly guaranteed.
- Make single-argument constructors `explicit` unless implicit conversion is
  intentional, documented, and accompanied by an explanatory `NOLINT`.
- Brace `if` and `for` bodies. Prefer brace initialization; use parentheses for
  a vector size constructor when that is the intended form.
- Prefer `auto` when it preserves the required value, reference, or pointer
  type. Compare raw pointers with `nullptr`.
- Omit permanently unused parameter names; use `[[maybe_unused]]` when usage
  depends on configuration.
- Use `std::invoke` for immediately invoked lambdas.
- Give assertions explanatory messages, for example
  `assert(condition && "reason")`.
- Name internal namespaces `<file_name>_internal`.
- Follow IWYU. Preserve intentional public umbrella/transitive includes with
  an IWYU `keep` pragma or exported include block.

## Tests

- Use Unity and organize tests by subsystem under `tests/`.
- Put tests in `ae::test_<feature>` namespaces, normally matching the test
  file name.
- Name individual tests `test_<PascalCase>`.
- Define the module suite entry in the global namespace as
  `int test_<suite>()`; group entries dispatch suite entries.
- A `using namespace` directive is forbidden except where needed in a suite
  entry, where it requires an explanatory `// NOLINT`.
- Avoid Unity assertions specialized for `uint64_t`/`int64_t` and `double`;
  those types or assertion macros are not portable across all targets. Prefer
  portable values and assertions.
- Configure and run the corresponding CTest/Unity tests; a successful CMake
  configure is not test validation.

## Examples and Smoke Tests

Organize examples by feature. Put shared construction and platform helpers under
`examples/common`. The cloud and A/B message-exchange examples are smoke tests;
benchmarks are not unit tests.

Run smoke tests from the build directory in this order:

1. Remove persisted state: `rm -rf ./state`.
2. Run `./ab-message-exchange`; require exit code `0`.
3. Wait at least six seconds so the server forgets previous connections.
4. Run `./ab-message-exchange` again with the preserved state; require exit code
   `0`.
5. Remove `./state` again.
6. Run `./aether-client-cpp-cloud`; require exit code `0`.
7. Wait at least six seconds.
8. Run `./aether-client-cpp-cloud` again with the preserved state; require exit
   code `0`.

Do not read or analyze logs until these runs succeed unless log analysis is
explicitly requested to prove specific behavior.

## Build and Configuration

Use the regular root CMake project, enable the required `AE_BUILD_*` options,
build the requested targets, and run their tests from the same build directory.
Run clang-tidy on changed C++ files using that build's matching
`compile_commands.json`; regenerate it when configuration flags change.

`USER_CONFIG` selects the compile-time user-configuration header. The prescribed
operational default is `./config/user_config_hydrogen.h`.

For ESP-IDF, use the covered project at
`projects/xtensa_lx6/vscode/aether-client-cpp`. Select the appropriate ESP32
target and select the component through `COMPILE_EXAMPLE`. Preserve the required
component names `cloud`, `oddity`, and `send_message_delays`; do not rename
them. The Aether component requires the IDF targets `idf::esp_wifi`,
`idf::esp_netif`, `idf::nvs_flash`, `idf::spiffs`, and
`idf::esp_driver_uart`.

## Dependencies and Change Boundaries

- Manage dependencies through CPM in the root `CMakeLists.txt`.
- Do not add Conan, vcpkg, submodules, or vendored dependency copies unless
  explicitly requested.
- Use `CPM_SOURCE_CACHE` for repeated downloads and
  `CPM_<dependency name>_SOURCE` or `CPM_USE_LOCAL_PACKAGES` for local
  development; do not edit or copy dependency sources.
- Preserve dependency pins and required patches. Update a patch only when the
  dependency revision requires it.
- Keep CPM dependencies `EXCLUDE_FROM_ALL FALSE` and propagate install options
  so installation remains complete with `AE_INSTALL`.
- Unity is test-only, c-ares is desktop-only, and ESP-IDF dependencies are
  supplied by IDF rather than CPM.

## Optional AT Commands

- Build AT operations with `at::MakeRequest` and provide an `at::Wait` trigger
  for every expected response.
- Start requests through the normal sender/consumer/waiter path; do not send a
  command separately.
- Bound every request with `WithTimeout`.
- Handle modem `ERROR` and timeout before issuing dependent commands.
