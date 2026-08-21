// Standalone verification probe. It is intentionally not part of Horizon's
// CMake targets because Horizon does not depend on EASTL.
#include <EASTL/tuple.h>

#include <cstddef>
#include <cstdio>

namespace {

struct Empty {};

struct PlainStruct {
    char first;
    int second;
    double third;
};

struct EmptyFirstStruct {
    Empty first;
    char second;
    int third;
    double fourth;
};

struct EmptyMiddleStruct {
    char first;
    Empty second;
    int third;
    double fourth;
};

struct EmptyLastStruct {
    char first;
    int second;
    double third;
    Empty fourth;
};

template<typename T>
std::ptrdiff_t offset_of(T &object, const auto &member) {
    return reinterpret_cast<const std::byte *>(&member) -
           reinterpret_cast<const std::byte *>(&object);
}

}// namespace

int main() {
    eastl::tuple<char, int, double> plain{'a', 7, 2.5};
    eastl::tuple<Empty, char, int, double> empty_first{Empty{}, 'a', 7, 2.5};
    eastl::tuple<char, Empty, int, double> with_empty{'a', Empty{}, 7, 2.5};
    eastl::tuple<char, int, double, Empty> empty_last{'a', 7, 2.5, Empty{}};

    const bool plain_matches =
        sizeof(plain) == sizeof(PlainStruct) &&
        alignof(decltype(plain)) == alignof(PlainStruct) &&
        offset_of(plain, eastl::get<0>(plain)) == offsetof(PlainStruct, first) &&
        offset_of(plain, eastl::get<1>(plain)) == offsetof(PlainStruct, second) &&
        offset_of(plain, eastl::get<2>(plain)) == offsetof(PlainStruct, third);

    const bool empty_middle_matches =
        sizeof(with_empty) == sizeof(EmptyMiddleStruct) &&
        alignof(decltype(with_empty)) == alignof(EmptyMiddleStruct) &&
        offset_of(with_empty, eastl::get<0>(with_empty)) == offsetof(EmptyMiddleStruct, first) &&
        offset_of(with_empty, eastl::get<1>(with_empty)) == offsetof(EmptyMiddleStruct, second) &&
        offset_of(with_empty, eastl::get<2>(with_empty)) == offsetof(EmptyMiddleStruct, third) &&
        offset_of(with_empty, eastl::get<3>(with_empty)) == offsetof(EmptyMiddleStruct, fourth);

    const bool empty_first_matches =
        sizeof(empty_first) == sizeof(EmptyFirstStruct) &&
        alignof(decltype(empty_first)) == alignof(EmptyFirstStruct) &&
        offset_of(empty_first, eastl::get<0>(empty_first)) == offsetof(EmptyFirstStruct, first) &&
        offset_of(empty_first, eastl::get<1>(empty_first)) == offsetof(EmptyFirstStruct, second) &&
        offset_of(empty_first, eastl::get<2>(empty_first)) == offsetof(EmptyFirstStruct, third) &&
        offset_of(empty_first, eastl::get<3>(empty_first)) == offsetof(EmptyFirstStruct, fourth);

    const bool empty_last_matches =
        sizeof(empty_last) == sizeof(EmptyLastStruct) &&
        alignof(decltype(empty_last)) == alignof(EmptyLastStruct) &&
        offset_of(empty_last, eastl::get<0>(empty_last)) == offsetof(EmptyLastStruct, first) &&
        offset_of(empty_last, eastl::get<1>(empty_last)) == offsetof(EmptyLastStruct, second) &&
        offset_of(empty_last, eastl::get<2>(empty_last)) == offsetof(EmptyLastStruct, third) &&
        offset_of(empty_last, eastl::get<3>(empty_last)) == offsetof(EmptyLastStruct, fourth);

    std::printf("plain: sizeof=%zu alignof=%zu offsets=[%td,%td,%td] matches=%s\n",
                sizeof(plain), alignof(decltype(plain)),
                offset_of(plain, eastl::get<0>(plain)),
                offset_of(plain, eastl::get<1>(plain)),
                offset_of(plain, eastl::get<2>(plain)),
                plain_matches ? "true" : "false");
    std::printf("empty-first: sizeof=%zu alignof=%zu offsets=[%td,%td,%td,%td] matches=%s\n",
                sizeof(empty_first), alignof(decltype(empty_first)),
                offset_of(empty_first, eastl::get<0>(empty_first)),
                offset_of(empty_first, eastl::get<1>(empty_first)),
                offset_of(empty_first, eastl::get<2>(empty_first)),
                offset_of(empty_first, eastl::get<3>(empty_first)),
                empty_first_matches ? "true" : "false");
    std::printf("empty-middle: sizeof=%zu alignof=%zu offsets=[%td,%td,%td,%td] matches=%s\n",
                sizeof(with_empty), alignof(decltype(with_empty)),
                offset_of(with_empty, eastl::get<0>(with_empty)),
                offset_of(with_empty, eastl::get<1>(with_empty)),
                offset_of(with_empty, eastl::get<2>(with_empty)),
                offset_of(with_empty, eastl::get<3>(with_empty)),
                empty_middle_matches ? "true" : "false");
    std::printf("empty-last: sizeof=%zu alignof=%zu offsets=[%td,%td,%td,%td] matches=%s\n",
                sizeof(empty_last), alignof(decltype(empty_last)),
                offset_of(empty_last, eastl::get<0>(empty_last)),
                offset_of(empty_last, eastl::get<1>(empty_last)),
                offset_of(empty_last, eastl::get<2>(empty_last)),
                offset_of(empty_last, eastl::get<3>(empty_last)),
                empty_last_matches ? "true" : "false");

    return plain_matches && !empty_first_matches && empty_middle_matches && !empty_last_matches ? 0 : 1;
}
