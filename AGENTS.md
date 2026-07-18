# Aethernet Agent Rules

## Coding Style

- Follow **Google C++ Style Guide**.
- **Exception to IWYU:** 
    - If `foo.cpp` implements `foo.h`, and `foo.h` already includes a header, do NOT re-include it in `foo.cpp`.
    - If `base.h` includes a header, `derived.h` should NOT re-include it.
- Always wrap `if` and `for` bodies in `{}` even if it's short one-line statement.
- Initialize variables and objects using `{}` to distinguish from function call.
  - *Exception vector initialization*: In case vector must be created with size use `()` initialization to distinguish from initializer_list constructor.
- Always there is possible use `auto` for variables with respect for references `auto&` and pointers `auto*`.
- Check if pointer is null by comparing it to `nullptr` instead of using implicit conversion to `bool`.
- If function arguments is not used, there are two options. If it's never used, just ommit the argument name. If it's used on some configurations use `[[maybe_unused]]` attribute.
- Immediate lambda call pattern should be implemented by using `std::invoke`.

## Architecture & Object System

- **Core Objects (`Obj`):** Main entities (`Aether`, `Client`, `Server`, `Channel`) must inherit from `ae::Obj` and use the `AE_OBJECT` macro.
- **Smart Pointers:** Use `ae::ObjPtr<T>` for `Obj`-based classes. Use `ae::PtrView<T>` or raw pointers for weak references from runtime logic to `Obj` instances.
- **Persistence:** Use `Load`/`Save` methods and `AE_OBJECT_REFLECT` for persistent state in `Obj` classes.
- **Business Logic:** Async logic, streams, and drivers must NOT be `Obj`-based. They are "runtime" classes.

## Events and Callbacks

- Use events and callbacks to notify external code about state changes.
- If possible use callbacks set up by the constructor. If not, use event-based notifications.
- Try to pass callbacks as template parameters for better optimization.
- If not possible use SmallFunction.
- If not possible use std::function. Notice std::function makes heap allocation we trying to avoid.
- Events uses SmallFunction as a callback storage.
- Prefer setup SmallFunction by MethodPtr<&Class::method>{this} instead of lambda.
- Event subscriptions should be stored in Subscription or MultiSubscriptions class members to control subscription lifetime.
- If the class makes subscription controls lifetime of the object with event, subscription objects maybe ommited.

## Asynchrony & Tasks

### Task System

- Use task system to run tasks asynchronously.
- There is a `ManualTaskScheduler` with methods to define a single `Task` and `DelayedTask` with duration after which the task will be executed.
- To access this scheduler use `ae_context.scheduler()` method.
- Tasks should be lightweight as possible because it stored in fixed-size object pool.
- To control task lifetime use `TaskSubscription` a RAII object which automatically resets task if subscribers die. This guarantees task would not be invoked if subscription dead.
- In rare cases class can guarantee its lifetime, subscription may be omitted.

### Stdexec

- `stdexec` library is used for chaining low-level async operations.
- whole library connected through one header `aether/executors/executors.h` and defines ex namespace alias.
- all senders should be scheduled on our Task System though ex::SchedulerOnTasks if required.
- there is three types of objects in stdexec: constructors, adapters and consumers.
- constructors are `ex::just()`, `ex::create()` or some custom senders created for specific logic.
- adapters are `ex::then()`, `ex::let_value()`, `ex::let_error()`, `ex::upon_error()` and others that transform or combine senders.
- consumers are `ex::sync_wait`, but preferred to use `ex::AnyWaiter` or `ex:AsyncWaiter`.
- se `ex::create` to create a sender from a functor to integrate c-style callbacks or other non sender logic into the executor.
- use `ex::create` if logic must test the value and set either error or value during runtime
- use `ex::variant_sender` to conditionally return either on or another sender based on some value.
- use `ex::then` `ex::upon_error` to transform from one value to another or from error to value.
- use `ex::let_value` to run new sender in chain in case of set_value.
- use `ex::let_error` to run new sender in chain in case of set_error.
- `ex::for_range` to iterate over a range of values. Internal sender must return a value, an error or stopped to end loop. To cont
- `ex::with_timeout` to add timeout to a sender chain.
- senders and adapters chained together by `|` operator.

### Actions

- Use actions to encapsulate high-level async operations.
- The `Action` maybe inherited from `Action` class which adds `is_finished()` and `finished_event()` methods.
- Each action should provide events to notify about operation progress and completion. Usually just `result_event()`.
- Actions may use senders inside to define their logic or just subscribe to another action's events.
- Actions should be managed by the class that creates them and returned by reference or pointers in case of possible null.
- If action `is_finished()` or after `finished_event()` is emitted non should have access to the action except the owner.
- Actions may be created as class members with optional or `ActionPool` or `std::unique_ptr`. But prefer to avoid allocations.


## Specific modules implementation

### AT Commands

- `at::MakeRequest(at_support, cmd, waits)` returns a pipeable adaptor — use mid-chain.
- `at::MakeRequest(ex::just(), at_support, cmd, waits)` returns a sender — use as first element or inside `ex::let_value`.
- `at::Wait("<expected>")` — build waits for the string.
- `at::Wait("<trigger>", [](auto& buffer, auto pos) { Handlers for response, must return bool })` — build waits for the string and custom handler.
- Timeouts should be implemented using `ex::with_timeout`.

## Build & Testing

 Project uses cmake, tests run with `ctest`.
 Project build configured in `build-clang` (`<build-dir>`) with ninja.
 To build the project go into `<build-dir>/` and run `cmake --build . --parallel` or `ninja`.

 To run tests, go into `<build-dir>` and run `ctest . --progress -j -E "((sodium)|(hydro)|(bcrypt)).*" --output-on-failure`.
 Or run specific test by name from `<build-dir>/tests/run/<test-name>`.

### Smoke test

 The first smoke test is a `<build-dir>/aether-client-cpp-cloud`.
 To run it simply `cd <build-dir>` and ran the `./aether-client-cpp-cloud` binary. 
 Notice! Run `aether-client-cpp-cloud` generates `state` dir there object state is saved. 
 Remove this `state` dir before run to make clean run. Keep it to run with previous state.

## Operational Rules

- Do not analyze logs until everything is working fine.

## Cursor Cloud specific instructions

These notes complement the `Build & Testing` and `Smoke test` sections above; they capture non-obvious, environment-specific gotchas for the Cursor Cloud VM. Standard build/test/smoke commands are already documented above.

### Toolchain

- The concrete `<build-dir>` used in the cloud VM is `build-clang`, built with `clang++` + Ninja.
- `clang` selects the gcc-14 toolchain for its C++ runtime, so `libstdc++-14-dev` must be installed (the update script installs it together with `ninja-build`). Without it, even the CMake compiler check fails with `cannot find -lstdc++`. `cmake`, `clang-18`, and `g++-13` are already present in the base image.
- Configure command that works here (the `-C .github/workflows/linux_initial_cache.txt` seed pre-answers c-ares feature probes):
  `cmake -B build-clang -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -C .github/workflows/linux_initial_cache.txt -S .`

### Dependencies (CPM)

- There is no vendored dependency tree: all third-party libs (libsodium, libhydrogen, libbcrypt, c-ares, etl, stdexec, numeric, aether-miscpp, gcem, ini.h) are fetched from GitHub at **cmake configure time** by CPM. Configuring therefore needs network + git.
- Export `CPM_SOURCE_CACHE=$HOME/.cache/CPM` before configuring so re-configures reuse the download cache instead of re-cloning into the build dir. This is optional but much faster.

### Lint

- The library builds with `-Werror -Wall -Wextra -Wconversion`, so a clean build is the primary lint gate.
- The additional static-analysis lint is cppcheck 2.21.0, run exactly as in `.github/workflows/ci-cppcheck.yml` (build cppcheck from source, configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then run cppcheck against `compile_commands.json`). Any findings live only in third-party CPM sources; the project's own `aether/`, `examples/`, and `tools/` code is clean.

### Smoke test caveat (important)

- `aether-client-cpp-cloud` reliably completes the **registration** phase from the cloud VM: both clients A and B register with the registration cloud (`Registration confirmed` / `Client registered`) and a persistent `state/` dir is written.
- The subsequent **peer-to-peer messaging** phase does not complete here: the assigned work-cloud relay servers (e.g. `34.60.244.148:9021`) accept TCP but never answer the UDP ping/relay traffic, so `received_count` stays `0`. The binary then crashes on that connection-failure path. This is an external work-cloud reachability limitation (not a build/env problem), so treat successful registration + `state/` creation as the working end-to-end signal in this environment rather than a clean exit 0.
