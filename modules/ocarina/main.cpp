#include <tuple>
#include <iostream>
#include <array>
#include <utility>
#include "ast/op.h"
#include "core/concepts.h"
#include "math/basic_traits.h"
#include "typeinfo"
#include "core/util/hash.h"
#include "core/util/string_util.h"
#include "core/type_system/type_desc.h"
#include "dsl/dsl.h"

using namespace ocarina;

template<typename T, size_t N, size_t... i>
auto array_to_tuple(ocarina::array<T, N> arr, std::index_sequence<i...>) {
    //    ((cout << arr[i]),...);
    auto ret = ocarina::tuple<std::remove_cvref_t<decltype(arr[i])>...>(arr[i]...);
    return ret;
}

const vector<const Type *> _members;

ocarina::span<const Type *const> members() noexcept {
    return ocarina::span(_members);
}

int main() {
    using namespace ocarina;
    ocarina::array<float, 2> arr = {1, 2};
    ocarina::tuple<float, float> tp = array_to_tuple(arr, std::make_index_sequence<2>());
    //    ocarina::tuple<float, float> tp = ocarina::tuple<float, float>(1,5);
    //    cout << typeid(tp).name() << endl;
    //    cout << typeid(struct_member_tuple<ocarina::array<float, 2>>::type).name();
    cout << TypeDesc<decltype(tp)>::description() << endl;
    //    cout << TypeDesc<TriangleHit>::description() << endl;
    //    cout << typeid(ocarina::tuple_join_t<tuple<int, float, int>, tuple<int, float, uint>, int>).name() << endl;
    //    cout << typeid(canonical_layout<float2x2>::type).name() << endl;
}