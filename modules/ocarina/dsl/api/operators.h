//
// Created by Zero on 16/05/2022.
//

#pragma once

#include "core/stl.h"
#include "math/basic_types.h"
#include "../core/expr.h"
#include "ast/function.h"
#include "ast/op.h"

namespace ocarina::detail {

template<typename T>
[[nodiscard]] inline DynamicArray<T>
eval_dynamic_array(const DynamicArray<T> &array) noexcept;// implement in dynamic_array.h

}// namespace ocarina::detail

#define OC_MAKE_DSL_UNARY_OPERATOR(op, tag)                                                                          \
    template<typename T>                                                                                             \
    requires ocarina::is_device_type_v<T>                                                                            \
    OC_NODISCARD inline auto                                                                                         \
    operator op(T &&expr) noexcept {                                                                                 \
        if constexpr (ocarina::is_dynamic_array_v<T>) {                                                              \
            using element_t = std::remove_cvref_t<decltype(op std::declval<ocarina::dynamic_array_element_t<T>>())>; \
            auto expression = ocarina::Function::current()->unary(expr.type(),                                       \
                                                                  ocarina::UnaryOp::tag,                             \
                                                                  expr.expression());                                \
            return ocarina::detail::eval_dynamic_array(ocarina::DynamicArray<element_t>(expr.size(), expression));   \
        } else {                                                                                                     \
            return []<typename Arg>(const Arg &arg) {                                                                \
                using Ret = std::remove_cvref_t<decltype(op std::declval<ocarina::remove_device_t<Arg>>())>;         \
                return ocarina::eval<Ret>(                                                                           \
                    ocarina::Function::current()->unary(                                                             \
                        ocarina::Type::of<Ret>(),                                                                    \
                        ocarina::UnaryOp::tag,                                                                       \
                        OC_EXPR(arg)));                                                                              \
            }(ocarina::decay_swizzle(OC_FORWARD(expr)));                                                             \
        }                                                                                                            \
    }

OC_MAKE_DSL_UNARY_OPERATOR(+, POSITIVE)
OC_MAKE_DSL_UNARY_OPERATOR(-, NEGATIVE)
OC_MAKE_DSL_UNARY_OPERATOR(!, NOT)
OC_MAKE_DSL_UNARY_OPERATOR(~, BIT_NOT)

#undef OC_MAKE_DSL_UNARY_OPERATOR

#define OC_MAKE_DSL_BINARY_OPERATOR(op, tag, trait)                                                      \
    template<typename L, typename R>                                                                     \
    requires ocarina::any_device_type_v<L, R> &&                                                         \
             ocarina::is_general_basic_v<ocarina::remove_device_t<L>> &&                                 \
             ocarina::is_general_basic_v<ocarina::remove_device_t<R>>                                    \
    [[nodiscard]] inline auto                                                                            \
    operator op(L &&lhs, R &&rhs) noexcept {                                                             \
        auto impl = []<typename Lhs, typename Rhs>(const Lhs &lhs, const Rhs &rhs) {                     \
            using namespace std::string_view_literals;                                                   \
            static constexpr bool is_logic_op = #op == "||"sv || #op == "&&"sv;                          \
            static constexpr bool is_bit_op = #op == "|"sv || #op == "&"sv || #op == "^"sv;              \
            static constexpr bool is_bool_lhs = ocarina::is_boolean_v<ocarina::remove_device_t<Lhs>>;    \
            static constexpr bool is_bool_rhs = ocarina::is_boolean_v<ocarina::remove_device_t<Rhs>>;    \
            using NormalRet = std::remove_cvref_t<                                                       \
                decltype(std::declval<ocarina::remove_device_t<Lhs>>() op                                \
                             std::declval<ocarina::remove_device_t<Rhs>>())>;                            \
            using Ret = std::conditional_t<is_bool_lhs && is_logic_op, bool, NormalRet>;                 \
            return ocarina::eval<Ret>(ocarina::Function::current()->binary(                              \
                ocarina::Type::of<Ret>(),                                                                \
                ocarina::BinaryOp::tag,                                                                  \
                OC_EXPR(lhs),                                                                            \
                OC_EXPR(rhs)));                                                                          \
        };                                                                                               \
        return impl(ocarina::decay_swizzle(OC_FORWARD(lhs)),                                             \
                    ocarina::decay_swizzle(OC_FORWARD(rhs)));                                            \
    }                                                                                                    \
                                                                                                         \
    template<typename T, typename U,                                                                     \
             typename NormalRet = std::remove_cvref_t<decltype(std::declval<T>() op std::declval<U>())>> \
    [[nodiscard]] inline auto operator op(const ocarina::DynamicArray<T> &lhs,                           \
                                          const ocarina::DynamicArray<U> &rhs) noexcept {                \
        using namespace std::string_view_literals;                                                       \
        static constexpr bool is_logic_op = #op == "||"sv || #op == "&&"sv;                              \
        static constexpr bool is_bit_op = #op == "|"sv || #op == "&"sv || #op == "^"sv;                  \
        static constexpr bool is_bool_lhs = ocarina::is_boolean_expr_v<T>;                               \
        static constexpr bool is_bool_rhs = ocarina::is_boolean_expr_v<U>;                               \
        OC_ASSERT(lhs.size() == rhs.size() || std::min(lhs.size(), rhs.size()) == 1);                    \
        auto size = std::max(lhs.size(), rhs.size());                                                    \
        using Ret = std::conditional_t<is_bool_lhs && is_logic_op, bool, NormalRet>;                     \
        auto expression = ocarina::Function::current()->binary(ocarina::DynamicArray<Ret>::type(size),   \
                                                               ocarina::BinaryOp::tag, lhs.expression(), \
                                                               rhs.expression());                        \
        return ocarina::detail::eval_dynamic_array(ocarina::DynamicArray<Ret>(size, expression));        \
    }                                                                                                    \
    template<typename T, typename U>                                                                     \
    requires ocarina::is_scalar_v<ocarina::expr_value_t<U>>                                              \
    [[nodiscard]] inline auto operator op(const ocarina::DynamicArray<T> &lhs, U &&rhs) noexcept {       \
        ocarina::DynamicArray<ocarina::expr_value_t<U>> arr(1u);                                         \
        arr[0] = OC_FORWARD(rhs);                                                                        \
        return lhs op arr;                                                                               \
    }                                                                                                    \
                                                                                                         \
    template<typename T, typename U>                                                                     \
    requires ocarina::is_scalar_v<ocarina::expr_value_t<T>>                                              \
    [[nodiscard]] inline auto operator op(T &&lhs, const ocarina::DynamicArray<U> &rhs) noexcept {       \
        ocarina::DynamicArray<ocarina::expr_value_t<U>> arr(1u);                                         \
        arr[0] = OC_FORWARD(lhs);                                                                        \
        return arr op rhs;                                                                               \
    }

OC_MAKE_DSL_BINARY_OPERATOR(+, ADD, add)
OC_MAKE_DSL_BINARY_OPERATOR(-, SUB, sub)
OC_MAKE_DSL_BINARY_OPERATOR(*, MUL, mul)
OC_MAKE_DSL_BINARY_OPERATOR(/, DIV, div)
OC_MAKE_DSL_BINARY_OPERATOR(%, MOD, mod)
OC_MAKE_DSL_BINARY_OPERATOR(&, BIT_AND, bit_and)
OC_MAKE_DSL_BINARY_OPERATOR(|, BIT_OR, bit_or)
OC_MAKE_DSL_BINARY_OPERATOR(^, BIT_XOR, bit_xor)
OC_MAKE_DSL_BINARY_OPERATOR(<<, SHL, shl)
OC_MAKE_DSL_BINARY_OPERATOR(>>, SHR, shr)
OC_MAKE_DSL_BINARY_OPERATOR(&&, AND, and_op)
OC_MAKE_DSL_BINARY_OPERATOR(||, OR, or_op)
OC_MAKE_DSL_BINARY_OPERATOR(==, EQUAL, equal)
OC_MAKE_DSL_BINARY_OPERATOR(!=, NOT_EQUAL, not_equal)
OC_MAKE_DSL_BINARY_OPERATOR(<, LESS, less)
OC_MAKE_DSL_BINARY_OPERATOR(<=, LESS_EQUAL, less_eq)
OC_MAKE_DSL_BINARY_OPERATOR(>, GREATER, greater)
OC_MAKE_DSL_BINARY_OPERATOR(>=, GREATER_EQUAL, greater_equal)

#undef OC_MAKE_DSL_BINARY_OPERATOR

#define OC_MAKE_DSL_ASSIGN_OP(op)                                                   \
    template<typename Lhs, typename Rhs>                                            \
    requires requires {                                                             \
        std::declval<Lhs &>() op## = std::declval<ocarina::remove_device_t<Rhs>>(); \
    }                                                                               \
    void operator op## = (const ocarina::Var<Lhs> &lhs, Rhs &&rhs) {                \
        auto x = lhs op OC_FORWARD(rhs);                                            \
        ocarina::Function::current()->assign(lhs.expression(), x.expression());     \
    }                                                                               \
                                                                                    \
    template<typename Lhs, typename Rhs>                                            \
    requires(ocarina::is_device_swizzle_v<Lhs, 0>)                                  \
    void operator op## = (Lhs & lhs, Rhs && rhs) {                                  \
        auto x = lhs.decay() op OC_FORWARD(rhs);                                    \
        lhs = x;                                                                    \
    }                                                                               \
                                                                                    \
    template<typename T, typename U>                                                \
    requires ocarina::is_dynamic_array_v<U> ||                                      \
                 ocarina::is_scalar_v<ocarina::expr_value_t<U>>                     \
    void operator op## = (const ocarina::DynamicArray<T> &lhs, U &&rhs) noexcept {  \
        auto x = lhs op OC_FORWARD(rhs);                                            \
        ocarina::Function::current()->assign(lhs.expression(), x.expression());     \
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