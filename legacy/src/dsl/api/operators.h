//
// Created by Zero on 16/05/2022.
//

#pragma once

#include "core/stl.h"
#include "math/basic_types.h"
#include "../core/expr.h"
#include "ast/function.h"
#include "ast/op.h"

namespace horizon::dsl::detail {
using namespace horizon::core;
using namespace horizon::math;
using namespace horizon::ast;

template<typename T>
[[nodiscard]] inline DynamicArray<T>
eval_dynamic_array(const DynamicArray<T> &array) noexcept;// implement in dynamic_array.h

}// namespace horizon::dsl::detail

#define OC_MAKE_DSL_UNARY_OPERATOR(op, tag)                                                                          \
    template<typename T>                                                                                             \
    requires horizon::dsl::is_device_type_v<T>                                                                            \
    OC_NODISCARD inline auto                                                                                         \
    operator op(T &&expr) noexcept {                                                                                 \
        if constexpr (horizon::dsl::is_dynamic_array_v<T>) {                                                              \
            using element_t = std::remove_cvref_t<decltype(op std::declval<horizon::dsl::dynamic_array_element_t<T>>())>; \
            auto expression = horizon::dsl::Function::current()->unary(expr.type(),                                       \
                                                                  horizon::dsl::UnaryOp::tag,                             \
                                                                  expr.expression());                                \
            return horizon::dsl::detail::eval_dynamic_array(horizon::dsl::DynamicArray<element_t>(expr.size(), expression));   \
        } else {                                                                                                     \
            return []<typename Arg>(const Arg &arg) {                                                                \
                using Ret = std::remove_cvref_t<decltype(op std::declval<horizon::dsl::remove_device_t<Arg>>())>;         \
                return horizon::dsl::eval<Ret>(                                                                           \
                    horizon::dsl::Function::current()->unary(                                                             \
                        horizon::dsl::Type::of<Ret>(),                                                                    \
                        horizon::dsl::UnaryOp::tag,                                                                       \
                        OC_EXPR(arg)));                                                                              \
            }(horizon::dsl::decay_swizzle(OC_FORWARD(expr)));                                                             \
        }                                                                                                            \
    }

OC_MAKE_DSL_UNARY_OPERATOR(+, Positive)
OC_MAKE_DSL_UNARY_OPERATOR(-, Negative)
OC_MAKE_DSL_UNARY_OPERATOR(!, Not)
OC_MAKE_DSL_UNARY_OPERATOR(~, BitNot)

#undef OC_MAKE_DSL_UNARY_OPERATOR

#define OC_MAKE_DSL_BINARY_OPERATOR(op, tag, trait)                                                      \
    template<typename L, typename R>                                                                     \
    requires horizon::dsl::any_device_type_v<L, R> &&                                                         \
             horizon::dsl::is_general_basic_v<horizon::dsl::remove_device_t<L>> &&                                 \
             horizon::dsl::is_general_basic_v<horizon::dsl::remove_device_t<R>>                                    \
    [[nodiscard]] inline auto                                                                            \
    operator op(L &&lhs, R &&rhs) noexcept {                                                             \
        auto impl = []<typename Lhs, typename Rhs>(const Lhs &lhs, const Rhs &rhs) {                     \
            using namespace std::string_view_literals;                                                   \
            static constexpr bool is_logic_op = #op == "||"sv || #op == "&&"sv;                          \
            static constexpr bool is_bit_op = #op == "|"sv || #op == "&"sv || #op == "^"sv;              \
            static constexpr bool is_bool_lhs = horizon::dsl::is_boolean_v<horizon::dsl::remove_device_t<Lhs>>;    \
            static constexpr bool is_bool_rhs = horizon::dsl::is_boolean_v<horizon::dsl::remove_device_t<Rhs>>;    \
            using NormalRet = std::remove_cvref_t<                                                       \
                decltype(std::declval<horizon::dsl::remove_device_t<Lhs>>() op                                \
                             std::declval<horizon::dsl::remove_device_t<Rhs>>())>;                            \
            using Ret = std::conditional_t<is_bool_lhs && is_logic_op, bool, NormalRet>;                 \
            return horizon::dsl::eval<Ret>(horizon::dsl::Function::current()->binary(                              \
                horizon::dsl::Type::of<Ret>(),                                                                \
                horizon::dsl::BinaryOp::tag,                                                                  \
                OC_EXPR(lhs),                                                                            \
                OC_EXPR(rhs)));                                                                          \
        };                                                                                               \
        return impl(horizon::dsl::decay_swizzle(OC_FORWARD(lhs)),                                             \
                    horizon::dsl::decay_swizzle(OC_FORWARD(rhs)));                                            \
    }                                                                                                    \
                                                                                                         \
    template<typename T, typename U,                                                                     \
             typename NormalRet = std::remove_cvref_t<decltype(std::declval<T>() op std::declval<U>())>> \
    [[nodiscard]] inline auto operator op(const horizon::dsl::DynamicArray<T> &lhs,                           \
                                          const horizon::dsl::DynamicArray<U> &rhs) noexcept {                \
        using namespace std::string_view_literals;                                                       \
        static constexpr bool is_logic_op = #op == "||"sv || #op == "&&"sv;                              \
        static constexpr bool is_bit_op = #op == "|"sv || #op == "&"sv || #op == "^"sv;                  \
        static constexpr bool is_bool_lhs = horizon::dsl::is_boolean_expr_v<T>;                               \
        static constexpr bool is_bool_rhs = horizon::dsl::is_boolean_expr_v<U>;                               \
        OC_ASSERT(lhs.size() == rhs.size() || std::min(lhs.size(), rhs.size()) == 1);                    \
        auto size = std::max(lhs.size(), rhs.size());                                                    \
        using Ret = std::conditional_t<is_bool_lhs && is_logic_op, bool, NormalRet>;                     \
        auto expression = horizon::dsl::Function::current()->binary(horizon::dsl::DynamicArray<Ret>::type(size),   \
                                                               horizon::dsl::BinaryOp::tag, lhs.expression(), \
                                                               rhs.expression());                        \
        return horizon::dsl::detail::eval_dynamic_array(horizon::dsl::DynamicArray<Ret>(size, expression));        \
    }                                                                                                    \
    template<typename T, typename U>                                                                     \
    requires horizon::dsl::is_scalar_v<horizon::dsl::expr_value_t<U>>                                              \
    [[nodiscard]] inline auto operator op(const horizon::dsl::DynamicArray<T> &lhs, U &&rhs) noexcept {       \
        horizon::dsl::DynamicArray<horizon::dsl::expr_value_t<U>> arr(1u);                                         \
        arr[0] = OC_FORWARD(rhs);                                                                        \
        return lhs op arr;                                                                               \
    }                                                                                                    \
                                                                                                         \
    template<typename T, typename U>                                                                     \
    requires horizon::dsl::is_scalar_v<horizon::dsl::expr_value_t<T>>                                              \
    [[nodiscard]] inline auto operator op(T &&lhs, const horizon::dsl::DynamicArray<U> &rhs) noexcept {       \
        horizon::dsl::DynamicArray<horizon::dsl::expr_value_t<U>> arr(1u);                                         \
        arr[0] = OC_FORWARD(lhs);                                                                        \
        return arr op rhs;                                                                               \
    }

OC_MAKE_DSL_BINARY_OPERATOR(+, Add, add)
OC_MAKE_DSL_BINARY_OPERATOR(-, Sub, sub)
OC_MAKE_DSL_BINARY_OPERATOR(*, Mul, mul)
OC_MAKE_DSL_BINARY_OPERATOR(/, Div, div)
OC_MAKE_DSL_BINARY_OPERATOR(%, Mod, mod)
OC_MAKE_DSL_BINARY_OPERATOR(&, BitAnd, bit_and)
OC_MAKE_DSL_BINARY_OPERATOR(|, BitOr, bit_or)
OC_MAKE_DSL_BINARY_OPERATOR(^, BitXor, bit_xor)
OC_MAKE_DSL_BINARY_OPERATOR(<<, Shl, shl)
OC_MAKE_DSL_BINARY_OPERATOR(>>, Shr, shr)
OC_MAKE_DSL_BINARY_OPERATOR(&&, And, and_op)
OC_MAKE_DSL_BINARY_OPERATOR(||, Or, or_op)
OC_MAKE_DSL_BINARY_OPERATOR(==, Equal, equal)
OC_MAKE_DSL_BINARY_OPERATOR(!=, NotEqual, not_equal)
OC_MAKE_DSL_BINARY_OPERATOR(<, Less, less)
OC_MAKE_DSL_BINARY_OPERATOR(<=, LessEqual, less_eq)
OC_MAKE_DSL_BINARY_OPERATOR(>, Greater, greater)
OC_MAKE_DSL_BINARY_OPERATOR(>=, GreaterEqual, greater_equal)

#undef OC_MAKE_DSL_BINARY_OPERATOR

#define OC_MAKE_DSL_ASSIGN_OP(op)                                                   \
    template<typename Lhs, typename Rhs>                                            \
    requires requires {                                                             \
        std::declval<Lhs &>() op## = std::declval<horizon::dsl::remove_device_t<Rhs>>(); \
    }                                                                               \
    void operator op## = (const horizon::dsl::Var<Lhs> &lhs, Rhs &&rhs) {                \
        auto x = lhs op OC_FORWARD(rhs);                                            \
        horizon::dsl::Function::current()->assign(lhs.expression(), x.expression());     \
    }                                                                               \
                                                                                    \
    template<typename Lhs, typename Rhs>                                            \
    requires(horizon::dsl::is_device_swizzle_v<Lhs, 0>)                                  \
    void operator op## = (Lhs & lhs, Rhs && rhs) {                                  \
        auto x = lhs.decay() op OC_FORWARD(rhs);                                    \
        lhs = x;                                                                    \
    }                                                                               \
                                                                                    \
    template<typename T, typename U>                                                \
    requires horizon::dsl::is_dynamic_array_v<U> ||                                      \
                 horizon::dsl::is_scalar_v<horizon::dsl::expr_value_t<U>>                     \
    void operator op## = (const horizon::dsl::DynamicArray<T> &lhs, U &&rhs) noexcept {  \
        auto x = lhs op OC_FORWARD(rhs);                                            \
        horizon::dsl::Function::current()->assign(lhs.expression(), x.expression());     \
    }

OC_MAKE_DSL_ASSIGN_OP(+)
OC_MAKE_DSL_ASSIGN_OP(-)
OC_MAKE_DSL_ASSIGN_OP(*)
OC_MAKE_DSL_ASSIGN_OP(/)
OC_MAKE_DSL_ASSIGN_OP(|)
OC_MAKE_DSL_ASSIGN_OP(%)
OC_MAKE_DSL_ASSIGN_OP(&)
OC_MAKE_DSL_ASSIGN_OP(>>)
OC_MAKE_DSL_ASSIGN_OP(<<)
OC_MAKE_DSL_ASSIGN_OP(^)

#undef OC_MAKE_DSL_ASSIGN_OP