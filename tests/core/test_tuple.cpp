#include "core/tuple.h"

#include <cstddef>
#include <iostream>
#include <tuple>
#include <utility>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Empty {};

struct MixedLayout {
    char first;
    int second;
    double third;
};

struct LateAlignmentLayout {
    int first;
    char second;
    double third;
    Empty fourth;
};

using MixedTuple = ocarina::tuple_storage::tuple<char, int, double>;
using LateAlignmentTuple = ocarina::tuple_storage::tuple<int, char, double, Empty>;

static_assert(sizeof(MixedTuple) == sizeof(MixedLayout));
static_assert(alignof(MixedTuple) == alignof(MixedLayout));
static_assert(sizeof(LateAlignmentTuple) == sizeof(LateAlignmentLayout));
static_assert(alignof(LateAlignmentTuple) == alignof(LateAlignmentLayout));
static_assert(std::tuple_size_v<MixedTuple> == 3u);
static_assert(std::is_same_v<std::tuple_element_t<1u, MixedTuple>, int>);
static_assert(std::is_same_v<decltype(ocarina::tuple_storage::get<1u>(std::declval<const MixedTuple &>())), const int &>);
static_assert([] {
    MixedTuple value;
    return ocarina::tuple_storage::get<0u>(value) == '\0' && ocarina::tuple_storage::get<1u>(value) == 0 &&
           ocarina::tuple_storage::get<2u>(value) == 0.0;
}(), "default construction value-initializes scalar elements");

template<typename T>
std::ptrdiff_t offset_of(T &object, const auto &member) {
    return reinterpret_cast<const std::byte *>(&member) -
           reinterpret_cast<const std::byte *>(&object);
}

void test_tuple_matches_mirror_struct_layout() {
    MixedTuple mixed{'a', 7, 2.5};
    LateAlignmentTuple late{3, 'b', 4.5, Empty{}};

    expect(offset_of(mixed, ocarina::tuple_storage::get<0u>(mixed)) == offsetof(MixedLayout, first) &&
               offset_of(mixed, ocarina::tuple_storage::get<1u>(mixed)) == offsetof(MixedLayout, second) &&
               offset_of(mixed, ocarina::tuple_storage::get<2u>(mixed)) == offsetof(MixedLayout, third),
           "tuple member offsets match a mixed-alignment mirror struct");
    expect(offset_of(late, ocarina::tuple_storage::get<0u>(late)) == offsetof(LateAlignmentLayout, first) &&
               offset_of(late, ocarina::tuple_storage::get<1u>(late)) == offsetof(LateAlignmentLayout, second) &&
               offset_of(late, ocarina::tuple_storage::get<2u>(late)) == offsetof(LateAlignmentLayout, third) &&
               offset_of(late, ocarina::tuple_storage::get<3u>(late)) == offsetof(LateAlignmentLayout, fourth),
           "tuple preserves individual alignment when a later member has stricter alignment");
}

void test_tuple_exposes_standard_value_access_and_utilities() {
    MixedTuple value{'x', 11, 3.5};
    auto &[character, number, decimal] = value;
    character = 'y';
    number += 1;

    expect(ocarina::tuple_storage::get<0u>(value) == 'y' && ocarina::tuple_storage::get<int>(value) == 12 &&
               ocarina::tuple_storage::get<double>(value) == 3.5 && decimal == 3.5,
           "indexed and type-based get support structured bindings and mutable access");

    auto made = ocarina::tuple_storage::make_tuple(2, 3);
    expect(ocarina::tuple_storage::apply([](int lhs, int rhs) { return lhs + rhs; }, made) == 5,
           "make_tuple and apply preserve values and argument order");

    int lhs = 0;
    int rhs = 0;
    auto references = ocarina::tuple_storage::tie(lhs, rhs);
    ocarina::tuple_storage::get<0u>(references) = 4;
    ocarina::tuple_storage::get<1u>(references) = 9;
    expect(lhs == 4 && rhs == 9, "tie exposes writable references to its inputs");

    auto concatenated = ocarina::tuple_storage::tuple_cat(ocarina::tuple_storage::make_tuple(1), ocarina::tuple_storage::make_tuple(2, 3));
    expect(ocarina::tuple_storage::get<0u>(concatenated) == 1 && ocarina::tuple_storage::get<1u>(concatenated) == 2 &&
               ocarina::tuple_storage::get<2u>(concatenated) == 3,
           "tuple_cat concatenates Core tuples in argument order");

    auto forwarded = ocarina::tuple_storage::forward_as_tuple(lhs);
    ocarina::tuple_storage::get<0u>(forwarded) = 12;
    auto first = ocarina::tuple_storage::make_tuple(1, 2);
    auto second = ocarina::tuple_storage::make_tuple(3, 4);
    ocarina::tuple_storage::swap(first, second);
    expect(lhs == 12 && ocarina::tuple_storage::get<0u>(first) == 3 && ocarina::tuple_storage::get<1u>(second) == 2,
           "forward_as_tuple preserves references and swap exchanges Core tuple values");
}

template<size_t... Indices>
auto make_wide_tuple(std::index_sequence<Indices...>) {
    using WideTuple = ocarina::tuple_storage::tuple<std::conditional_t<true, int, std::integral_constant<size_t, Indices>>...>;
    return WideTuple{static_cast<int>(Indices)...};
}

void test_tuple_supports_large_reflected_structures() {
    auto value = make_wide_tuple(std::make_index_sequence<72u>{});

    expect(ocarina::tuple_storage::get<0u>(value) == 0 && ocarina::tuple_storage::get<17u>(value) == 17 &&
               ocarina::tuple_storage::get<31u>(value) == 31 && ocarina::tuple_storage::get<32u>(value) == 32 &&
               ocarina::tuple_storage::get<63u>(value) == 63 && ocarina::tuple_storage::get<64u>(value) == 64 &&
               ocarina::tuple_storage::get<71u>(value) == 71,
           "the maximum supported arity retains both first and last values");
}

}// namespace

int main() {
    test_tuple_matches_mirror_struct_layout();
    test_tuple_exposes_standard_value_access_and_utilities();
    test_tuple_supports_large_reflected_structures();
    return failures == 0 ? 0 : 1;
}
