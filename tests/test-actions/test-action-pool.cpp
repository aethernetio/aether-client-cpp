#include <unity.h>

#include <array>
#include <variant>

#include "aether/actions/action_pool.h"
#include "aether/tasks/manual_task_scheduler.h"

namespace ae::test_action_pool {
using Scheduler = ManualTaskScheduler<TaskManagerConf<50>>;
struct Context {
  Scheduler& scheduler() const { return *tasks; }
  EventSystem& event_system() const { return *events; }
  Scheduler* tasks;
  EventSystem* events;
};
class Operation : public Action {
 public:
  Operation(Context const& context, int& destroyed)
      : Action{context}, destroyed_{destroyed} {}
  ~Operation() override { ++destroyed_; }

 private:
  int& destroyed_;
};
class OtherOperation final : public Operation {
 public:
  using Operation::Operation;
};

template <bool Variant>
void ConcurrentCompletion() {
  Scheduler tasks;
  EventSystem events;
  Context context{&tasks, &events};
  int destroyed = 0;
  using Types =
      std::conditional_t<Variant, std::variant<Operation, OtherOperation>,
                         Operation>;
  {
    ActionPool<Context, Types, 5> pool{context};
    for (int round = 0; round != 20; ++round) {
      std::array<Operation*, 5> actions{};
      for (auto& action : actions) {
        if constexpr (Variant) {
          action = pool.template Create<OtherOperation>(context, destroyed);
        } else {
          action = pool.Create(context, destroyed);
        }
        TEST_ASSERT_NOT_NULL(action);
      }
      for (auto* action : actions) action->Finish();
      TEST_ASSERT_EQUAL_INT(round * 5, destroyed);
      tasks.Update();
      TEST_ASSERT_EQUAL_INT((round + 1) * 5, destroyed);
    }
    // Destroying the owner must cancel every queued reclamation callback.
    for (int i = 0; i != 5; ++i) {
      if constexpr (Variant) {
        pool.template Create<OtherOperation>(context, destroyed)->Finish();
      } else {
        pool.Create(context, destroyed)->Finish();
      }
    }
  }
  TEST_ASSERT_EQUAL_INT(105, destroyed);
  tasks.Update();
  TEST_ASSERT_EQUAL_INT(105, destroyed);
}
void SinglePool() { ConcurrentCompletion<false>(); }
void VariantPool() { ConcurrentCompletion<true>(); }
}  // namespace ae::test_action_pool

int test_action_pool() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_action_pool::SinglePool);
  RUN_TEST(ae::test_action_pool::VariantPool);
  return UNITY_END();
}
