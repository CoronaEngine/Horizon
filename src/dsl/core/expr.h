//
// Created by Zero on 06/05/2022.
//

#pragma once

#include "math/basic_types.h"
#include "type_trait.h"
#include "ref.h"
#include "ast/function.h"

namespace ocarina {

struct Expression;

namespace detail {

template<typename T>
[[nodiscard]] decltype(auto) extract_expression(T &&v) noexcept {
    if constexpr (is_dsl_v<T>) {
        return std::forward<T>(v).expression();
    } else {
        return ocarina::Function::current()->literal(Type::of<T>(), std::forward<T>(v));
    }
}

}// namespace detail

template<typename T>
struct Expr : public detail::Ref<T> {
public:
    using org_type = T;
    explicit Expr(const Expression *expression) noexcept
        : detail::Ref<T>(expression) {}
    Expr() = default;
    template<typename Arg>
    requires concepts::non_pointer<std::remove_cvref_t<Arg>> && concepts::different<Expr<T>, std::remove_cvref_t<Arg>>
    explicit Expr(Arg &&arg) : Expr(detail::extract_expression(std::forward<Arg>(arg))) {}
    OC_MAKE_GET_PROXY
    Expr(const Expr &) = delete;
    Expr &operator=(const Expr &) = delete;
    Expr &operator=(Expr &&) = delete;
};

template<typename T>
Expr(Expr<T>) -> Expr<T>;

template<typename T>
Expr(const Var<T> &) -> Expr<T>;

template<typename T>
Expr(const Buffer<T> &) -> Expr<Buffer<T>>;

}// namespace ocarina