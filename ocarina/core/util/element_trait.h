//
// Created by Zero on 2023/7/27.
//

#pragma once

#include "core/stl.h"

namespace ocarina {

namespace detail {

template<typename T>
struct element_impl {
    using type = T;
};

template<typename T>
struct element_impl<vector<T>> {
    using type = T;
};

template<typename T>
struct element_impl<list<T>> {
    using type = T;
};

template<typename T>
struct element_impl<std::stack<T>> {
    using type = T;
};

template<typename T>
struct element_impl<std::deque<T>> {
    using type = T;
};

template<typename T>
struct element_impl<std::queue<T>> {
    using type = T;
};

template<typename T>
struct element_impl<unique_ptr<T>> {
    using type = T;
};

template<typename T>
struct element_impl<shared_ptr<T>> {
    using type = T;
};

}// namespace detail

template<typename T>
using element_t = typename detail::element_impl<std::remove_cvref_t<T>>::type;

namespace detail {
template<typename T>
struct ptr_impl {
    static_assert(always_false_v<T>);
    using type = T;
};

template<typename T>
struct ptr_impl<T *> {
    using type = std::remove_cvref_t<T>;
};

template<typename T>
struct ptr_impl<unique_ptr<T>> {
    using type = T;
};

template<typename T, typename U>
struct ptr_impl<unique_ptr<T, U>> {
    using type = T;
};

template<typename T>
struct ptr_impl<shared_ptr<T>> {
    using type = T;
};

template<typename T>
requires std::is_pointer_v<std::remove_cvref_t<decltype(std::declval<T>().get())>>
struct ptr_impl<T> {
    using type = std::remove_pointer_t<std::remove_cvref_t<decltype(std::declval<T>().get())>>;
};

template<typename T>
struct is_ptr_impl : std::false_type {};

template<typename T>
struct is_ptr_impl<T *> : std::true_type {};

template<typename T>
struct is_ptr_impl<unique_ptr<T>> : std::true_type {};

template<typename T>
struct is_ptr_impl<shared_ptr<T>> : std::true_type {};

template<typename T>
requires std::is_pointer_v<std::remove_cvref_t<decltype(std::declval<T>().get())>>
struct is_ptr_impl<T> : std::true_type {};

}// namespace detail

template<typename T>
using ptr_t = detail::ptr_impl<std::remove_cvref_t<T>>::type;

template<typename T>
static constexpr bool is_ptr_v = detail::is_ptr_impl<std::remove_cvref_t<T>>::value;

template<typename Arg>
requires is_ptr_v<std::remove_cvref_t<Arg>>
[[nodiscard]] auto raw_ptr(Arg &&arg) {
    if constexpr (std::is_pointer_v<std::remove_cvref_t<Arg>>) {
        return arg;
    } else {
        return arg.get();
    }
}

enum class PointerCategory {
    Raw,
    Shared,
    Unique,
    NotAPointer
};

namespace detail {
template<typename T>
struct pointer_category_impl {
    static constexpr PointerCategory value = std::is_pointer_v<T> ?
                                                 PointerCategory::Raw :
                                                 PointerCategory::NotAPointer;
};

template<typename... Ts>
struct pointer_category_impl<std::unique_ptr<Ts...>> {
    static constexpr PointerCategory value = PointerCategory::Unique;
};

template<typename T>
struct pointer_category_impl<std::shared_ptr<T>> {
    static constexpr PointerCategory value = PointerCategory::Shared;
};
}// namespace detail

template<typename T>
using pointer_category = detail::pointer_category_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(pointer_category)

}// namespace ocarina