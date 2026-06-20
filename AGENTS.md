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
- To control task lifetime use `TaskSubscription` a RAII object which automatically resets task if subscribers die.
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

 To run smoke test, run `<build-dir>/aether-client-cpp-cloud`.

## Operational Rules
- Do not analyze logs until everything is working fine.
