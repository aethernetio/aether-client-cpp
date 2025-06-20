/*
 * Copyright 2025 Aethernet Inc.
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

#include <unity.h>

#include "aether/types/type_list.h"

#if defined __GNUC__
#  define AE_FUNC_NAME __PRETTY_FUNCTION__
#elif defined _MSC_VER
#  define AE_FUNC_NAME __FUNCSIG__
#endif

namespace ae::test_type_list {
template <std::size_t Step, std::size_t... Is>
constexpr auto MakeIndexSequenceWithStepImpl(std::index_sequence<Is...>) {
  return std::index_sequence<(Is * Step)...>{};
}

template <std::size_t Count, std::size_t Step>
constexpr auto MakeIndexSequenceWithStep() {
  return MakeIndexSequenceWithStepImpl<Step>(std::make_index_sequence<Count>());
}

template <std::size_t I>
struct NType {
  static constexpr std::size_t value = I;
};

template <std::size_t... Is>
static constexpr auto MakeTypeListImpl(std::index_sequence<Is...>)
    -> TypeList<NType<Is>...> {
  return {};
}

template <std::size_t N>
static constexpr auto MakeTypeList() {
  return MakeTypeListImpl(std::make_index_sequence<N>{});
}

template <typename TWorker, std::size_t... Is>
void ForTypesImpl(TWorker&& worker, std::index_sequence<Is...>) {
  (..., std::forward<TWorker>(worker).template work<Is>());
}

template <std::size_t IndexStep, typename TWorker, typename... Ts>
void ForTypes(TWorker&& worker, TypeList<Ts...>) {
  ForTypesImpl(
      std::forward<TWorker>(worker),
      MakeIndexSequenceWithStep<sizeof...(Ts) / IndexStep, IndexStep>());
}

template <typename TL>
struct Worker {
  template <std::size_t I>
  void work() {
    auto const* old_test_name = Unity.CurrentTestName;
    Unity.CurrentTestName = AE_FUNC_NAME;
    using type_at = TypeAtT<I, TL>;
    TEST_ASSERT_TRUE((std::is_same_v<NType<I>, type_at>));

    Unity.CurrentTestName = old_test_name;
  }
};

template <typename TL>
struct WorkerReverse {
  template <std::size_t I>
  void work() {
    auto const* old_test_name = Unity.CurrentTestName;
    Unity.CurrentTestName = AE_FUNC_NAME;
    constexpr auto kLastN = TypeListSize<TL> - 1;
    using type_at = TypeAtT<I, TL>;
    TEST_ASSERT_TRUE((std::is_same_v<NType<kLastN - I>, type_at>));

    Unity.CurrentTestName = old_test_name;
  }
};

template <std::size_t N>
void TestNTypes() {
  auto type_list = MakeTypeList<N>();
  using TL = decltype(type_list);

  TEST_ASSERT_EQUAL(N, TypeListSize<TL>);
  ForTypes<(N / 20) + 1>(Worker<TL>{}, type_list);
  Worker<TL>{}.template work<N - 1>();
}

template <std::size_t N>
void TestNTypesReverse() {
  auto type_list = MakeTypeList<N>();
  using TL = decltype(type_list);
  using RTL = typename ReversTypeList<TL>::type;

  TEST_ASSERT_EQUAL(N, TypeListSize<RTL>);
  ForTypes<(N / 20) + 1>(WorkerReverse<RTL>{}, type_list);
  Worker<TL>{}.template work<N - 1>();
}

void test_Ntypes() {
  RUN_TEST(TestNTypes<1>);
  RUN_TEST(TestNTypes<10>);
  RUN_TEST(TestNTypes<100>);
  RUN_TEST(TestNTypes<499>);

  RUN_TEST(TestNTypesReverse<1>);
  RUN_TEST(TestNTypesReverse<10>);
  RUN_TEST(TestNTypesReverse<100>);
  RUN_TEST(TestNTypesReverse<300>);  // MSVC has lower recursion limit
}

}  // namespace ae::test_type_list

int test_type_list() {
  UNITY_BEGIN();
  ae::test_type_list::test_Ntypes();
  return UNITY_END();
}
