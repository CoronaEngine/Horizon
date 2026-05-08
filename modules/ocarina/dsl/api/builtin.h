//
// Created by Zero on 03/05/2022.
//

#pragma once

#include "core/stl.h"
#include "../core/expr.h"
#include "operators.h"
#include "core/concepts.h"
#include "math/base.h"
#include "ast/expression.h"
#include "../core/var.h"
#include "../data/dynamic_array.h"

namespace ocarina {

#define OC_MAKE_BUILTIN_FUNC(func, type)                \
    [[nodiscard]] inline auto func() noexcept {         \
        return eval<type>(Function::current()->func()); \
    }
OC_MAKE_BUILTIN_FUNC(dispatch_idx, uint3)
OC_MAKE_BUILTIN_FUNC(block_idx, uint3)
OC_MAKE_BUILTIN_FUNC(thread_id, uint)
OC_MAKE_BUILTIN_FUNC(dispatch_id, uint)
OC_MAKE_BUILTIN_FUNC(thread_idx, uint3)
OC_MAKE_BUILTIN_FUNC(dispatch_dim, uint3)

template<typename DispatchIdx>
requires concepts::all_integral<vector_element_t<expr_value_t<DispatchIdx>>>
[[nodiscard]] auto dispatch_id(DispatchIdx &&idx) {
    if constexpr (is_vector2_expr_v<DispatchIdx>) {
        Uint3 dim = dispatch_dim();
        return idx.y * dim.x + idx.x;
    } else if constexpr (is_vector3_expr_v<DispatchIdx>) {
        Uint3 dim = dispatch_dim();
        return (dim.x * dim.y) * idx.z + dim.x * idx.y + idx.x;
    } else {
        static_assert(always_false_v<DispatchIdx>);
    }
}

template<typename DispatchId>
requires ocarina::is_integral_expr_v<DispatchId>
[[nodiscard]] auto dispatch_idx(DispatchId &&id) {
    Uint2 dim = dispatch_dim().xy();
    return make_uint2(id % dim.x, id / dim.x);
}

#undef OC_MAKE_BUILTIN_FUNC

namespace detail {

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] DynamicArray<T> expand_to_array(const T &t, uint size) noexcept;

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] DynamicArray<T> expand_to_array(const Var<T> &t, uint size) noexcept;

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] DynamicArray<T> expand_to_array(DynamicArray<T> arr, uint size) noexcept;

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] uint mix_size(const Var<T> &t) noexcept;

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] uint mix_size(const T &t) noexcept;

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] uint mix_size(const DynamicArray<T> &t) noexcept;

}// namespace detail

namespace detail {

template<typename T>
struct match_dsl_unary_func_impl : std::false_type {};

template<typename T>
struct match_dsl_unary_func_impl<Ref<T>> : std::true_type {};

template<typename T>
struct match_dsl_unary_func_impl<Var<T>> : std::true_type {};

template<typename T>
struct match_dsl_unary_func_impl<Expr<T>> : std::true_type {};

template<typename T, size_t N, size_t... Indices>
struct match_dsl_unary_func_impl<Swizzle<T, N, Indices...>> : std::true_type {};

}// namespace detail

template<typename T>
using match_dsl_unary_func = detail::match_dsl_unary_func_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(match_dsl_unary_func)

namespace detail {
template<typename T>
struct deduce_var_impl {};

template<typename T>
struct deduce_var_impl<Ref<T>> {
    using type = Var<T>;
};

template<typename T>
struct deduce_var_impl<Expr<T>> {
    using type = Var<T>;
};

template<typename T>
struct deduce_var_impl<Var<T>> {
    using type = Var<T>;
};

template<typename T, size_t N, size_t... Indices>
struct deduce_var_impl<Swizzle<Var<T>, N, Indices...>> {
    using type = typename Swizzle<Var<T>, N, Indices...>::vec_type;
};

}// namespace detail

template<typename T>
using deduce_var = detail::deduce_var_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_TYPE(deduce_var)

#define OC_MAKE_DSL_UNARY_FUNC(func, tag)                                              \
    template<typename T>                                                               \
    requires match_dsl_unary_func_v<T>                                                 \
    OC_NODISCARD auto func(const T &arg) noexcept {                                    \
        return MemberAccessor::func<deduce_var_t<T>>(arg);                             \
    }                                                                                  \
    template<typename T>                                                               \
    requires is_basic_v<T>                                                             \
    OC_NODISCARD DynamicArray<T> func(const DynamicArray<T> &t) noexcept {             \
        auto expr = Function::current()->call_builtin(DynamicArray<T>::type(t.size()), \
                                                      CallOp::tag, {OC_EXPR(t)});      \
        return eval_dynamic_array(DynamicArray<T>(t.size(), expr));                    \
    }

OC_MAKE_DSL_UNARY_FUNC(all, ALL)
OC_MAKE_DSL_UNARY_FUNC(any, ANY)
OC_MAKE_DSL_UNARY_FUNC(none, NONE)
OC_MAKE_DSL_UNARY_FUNC(rcp, RCP)
OC_MAKE_DSL_UNARY_FUNC(abs, ABS)
OC_MAKE_DSL_UNARY_FUNC(sign, SIGN)
OC_MAKE_DSL_UNARY_FUNC(sqr, SQR)
OC_MAKE_DSL_UNARY_FUNC(normalize, NORMALIZE)
OC_MAKE_DSL_UNARY_FUNC(length, LENGTH)
OC_MAKE_DSL_UNARY_FUNC(length_squared, LENGTH_SQUARED)

OC_MAKE_DSL_UNARY_FUNC(exp, EXP)
OC_MAKE_DSL_UNARY_FUNC(exp2, EXP2)
OC_MAKE_DSL_UNARY_FUNC(exp10, EXP10)
OC_MAKE_DSL_UNARY_FUNC(log, LOG)
OC_MAKE_DSL_UNARY_FUNC(log2, LOG2)
OC_MAKE_DSL_UNARY_FUNC(log10, LOG10)
OC_MAKE_DSL_UNARY_FUNC(cos, COS)
OC_MAKE_DSL_UNARY_FUNC(sin, SIN)
OC_MAKE_DSL_UNARY_FUNC(tan, TAN)
OC_MAKE_DSL_UNARY_FUNC(cosh, COSH)
OC_MAKE_DSL_UNARY_FUNC(sinh, SINH)
OC_MAKE_DSL_UNARY_FUNC(tanh, TANH)
OC_MAKE_DSL_UNARY_FUNC(acos, ACOS)
OC_MAKE_DSL_UNARY_FUNC(asin, ASIN)
OC_MAKE_DSL_UNARY_FUNC(atan, ATAN)
OC_MAKE_DSL_UNARY_FUNC(asinh, ASINH)
OC_MAKE_DSL_UNARY_FUNC(acosh, ACOSH)
OC_MAKE_DSL_UNARY_FUNC(atanh, ATANH)
OC_MAKE_DSL_UNARY_FUNC(degrees, DEGREES)
OC_MAKE_DSL_UNARY_FUNC(radians, RADIANS)
OC_MAKE_DSL_UNARY_FUNC(ceil, CEIL)
OC_MAKE_DSL_UNARY_FUNC(round, ROUND)
OC_MAKE_DSL_UNARY_FUNC(floor, FLOOR)
OC_MAKE_DSL_UNARY_FUNC(sqrt, SQRT)
OC_MAKE_DSL_UNARY_FUNC(rsqrt, RSQRT)
OC_MAKE_DSL_UNARY_FUNC(isinf, IS_INF)
OC_MAKE_DSL_UNARY_FUNC(isnan, IS_NAN)
OC_MAKE_DSL_UNARY_FUNC(fract, FRACT)
OC_MAKE_DSL_UNARY_FUNC(saturate, SATURATE)

OC_MAKE_DSL_UNARY_FUNC(determinant, DETERMINANT)
OC_MAKE_DSL_UNARY_FUNC(transpose, TRANSPOSE)
OC_MAKE_DSL_UNARY_FUNC(inverse, INVERSE)

#undef OC_MAKE_DSL_UNARY_FUNC

[[nodiscard]] inline Half float2half(const Float &arg) {
    const CallExpr *expr = Function::current()->call_builtin(Type::of<half>(),
                                                             CallOp::FLOAT2HALF,
                                                             {OC_EXPR(arg)});
    return eval<half>(expr);
}

[[nodiscard]] inline Float half2float(const Float &arg) {
    const CallExpr *expr = Function::current()->call_builtin(Type::of<half>(),
                                                             CallOp::HALF2FLOAT,
                                                             {OC_EXPR(arg)});
    return eval<float>(expr);
}

template<typename... Ts>
using match_dsl_basic_func = std::conjunction<any_device_type<Ts...>,
                                              match_basic_func<remove_device_t<Ts>...>>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(match_dsl_basic_func)

#define OC_MAKE_DSL_BINARY_FUNC(func, tag)                                        \
    template<typename Lhs, typename Rhs>                                          \
    requires match_dsl_basic_func_v<Lhs, Rhs>                                     \
    OC_NODISCARD auto func(const Lhs &lhs, const Rhs &rhs) noexcept {             \
        static constexpr auto dimension = type_dimension_v<remove_device_t<Lhs>>; \
        using scalar_type = type_element_t<remove_device_t<Lhs>>;                 \
        using var_type = Var<general_vector_t<scalar_type, dimension>>;           \
        return MemberAccessor::func<var_type>(decay_swizzle(lhs),                 \
                                              decay_swizzle(rhs));                \
    }

OC_MAKE_DSL_BINARY_FUNC(max, MAX)
OC_MAKE_DSL_BINARY_FUNC(min, MIN)
OC_MAKE_DSL_BINARY_FUNC(pow, POW)
OC_MAKE_DSL_BINARY_FUNC(fmod, FMOD)
OC_MAKE_DSL_BINARY_FUNC(mod, MOD)
OC_MAKE_DSL_BINARY_FUNC(copysign, COPYSIGN)
OC_MAKE_DSL_BINARY_FUNC(atan2, ATAN2)

OC_MAKE_DSL_BINARY_FUNC(cross, CROSS)
OC_MAKE_DSL_BINARY_FUNC(dot, DOT)
OC_MAKE_DSL_BINARY_FUNC(distance, DISTANCE)
OC_MAKE_DSL_BINARY_FUNC(distance_squared, DISTANCE_SQUARED)

#undef OC_MAKE_DSL_BINARY_FUNC

#define OC_MAKE_DSL_TRIPLE_FUNC(func, tag)                                                  \
    template<typename A, typename B, typename C>                                            \
    requires any_device_type_v<A, B, C> && requires {                                       \
        func(remove_device_t<A>{}, remove_device_t<B>{}, remove_device_t<B>{});             \
    }                                                                                       \
    [[nodiscard]] auto func(const A &a, const B &b, const C &c) noexcept {                  \
        static constexpr auto dimension = std::max({type_dimension_v<remove_device_t<A>>,   \
                                                    type_dimension_v<remove_device_t<B>>,   \
                                                    type_dimension_v<remove_device_t<C>>}); \
        using scalar_type = type_element_t<remove_device_t<A>>;                             \
        using var_type = Var<general_vector_t<scalar_type, dimension>>;                     \
        return MemberAccessor::func<var_type>(to_general_vector<dimension>(a),              \
                                              to_general_vector<dimension>(b),              \
                                              to_general_vector<dimension>(c));             \
    }

OC_MAKE_DSL_TRIPLE_FUNC(clamp, CLAMP)
OC_MAKE_DSL_TRIPLE_FUNC(lerp, LERP)
OC_MAKE_DSL_TRIPLE_FUNC(inverse_lerp, INVERSE_LERP)
OC_MAKE_DSL_TRIPLE_FUNC(fma, FMA)

#undef OC_MAKE_TRIPLE_FUNC

template<typename U, typename T, typename F>
requires any_device_type_v<U, T, F> &&
         (type_dimension_v<remove_device_t<T>> == type_dimension_v<remove_device_t<F>>) &&
         (type_dimension_v<remove_device_t<U>> == 1 ||
          type_dimension_v<remove_device_t<U>> == type_dimension_v<remove_device_t<T>>) &&
         is_all_basic_v<swizzle_decay_t<remove_device_t<U>>,
                        swizzle_decay_t<remove_device_t<T>>,
                        swizzle_decay_t<remove_device_t<F>>> &&
         requires {
             select(swizzle_decay_t<remove_device_t<U>>{},
                    swizzle_decay_t<remove_device_t<T>>{},
                    swizzle_decay_t<remove_device_t<F>>{});
         }
[[nodiscard]] auto select(const U &u, const T &t, const F &f) noexcept {
    static constexpr auto dimension = type_dimension_v<remove_device_t<T>>;
    using scalar_type = decltype(select(type_element_t<remove_device_t<U>>{},
                                        type_element_t<remove_device_t<T>>{},
                                        type_element_t<remove_device_t<F>>{}));
    using var_type = Var<general_vector_t<scalar_type, dimension>>;
    return MemberAccessor::select<var_type>(decay_swizzle(u),
                                            decay_swizzle(t),
                                            decay_swizzle(f));
}

/// used for dsl structure
template<typename U, typename T, typename F>
requires(std::is_same_v<expr_value_t<U>, bool> &&
         is_dsl_v<U> && any_dsl_v<T, F> && !is_basic_v<expr_value_t<F>> && !is_basic_v<expr_value_t<T>> &&
         std::is_same_v<expr_value_t<T>, expr_value_t<F>>) &&
        none_dynamic_array_v<U, T, F>
OC_NODISCARD auto select(U &&pred, T &&t, F &&f) noexcept {
    auto expr = Function::current()->conditional(Type::of<expr_value_t<T>>(),
                                                 OC_EXPR(pred), OC_EXPR(t), OC_EXPR(f));
    return eval<T>(expr);
}

/// used for dynamic array start
template<typename P, typename T>
[[nodiscard]] DynamicArray<T> select_array(const DynamicArray<P> &pred, const DynamicArray<T> &t,
                                           const DynamicArray<T> &f) noexcept {
    OC_ASSERT(pred.size() == t.size() && t.size() == f.size());
    auto expr = Function::current()->call_builtin(Type::of<expr_value_t<T>>(),
                                                  CallOp::SELECT,
                                                  {OC_EXPR(pred), OC_EXPR(t), OC_EXPR(f)});
    return detail::eval_dynamic_array<T>(DynamicArray<T>(pred.size(), expr));
}

template<typename P, typename T, typename F>
requires requires {
    detail::expand_to_array(std::declval<P>(), 3);
    detail::expand_to_array(std::declval<T>(), 3);
    detail::expand_to_array(std::declval<F>(), 3);
} && any_dynamic_array_v<P, T, F>
[[nodiscard]] auto select(P &&p, T &&t, F &&f) noexcept {
    using namespace detail;
    uint size0 = detail::mix_size(p);
    uint size1 = detail::mix_size(t);
    uint size2 = detail::mix_size(f);
    OC_ASSERT(size0 == size1 || size0 == size2 || size1 == size2);
    uint max_size = std::max({size0, size1, size2});
    return select_array(expand_to_array(OC_FORWARD(p), max_size),
                        expand_to_array(OC_FORWARD(t), max_size),
                        expand_to_array(OC_FORWARD(f), max_size));
}
/// used for dynamic array end

template<typename... Args>
requires(any_device_type_v<Args...>)
OC_NODISCARD auto face_forward(Args &&...args) noexcept {
    return MemberAccessor::face_forward<Float3>(decay_swizzle(OC_FORWARD(args))...);
}

template<typename A>
requires(is_all_float_vector3_v<expr_value_t<A>>)
void coordinate_system(const A &a, Var<float3> &b, Var<float3> &c) noexcept {
    auto expr = Function::current()->call_builtin(Type::of<expr_value_t<A>>(),
                                                  CallOp::COORDINATE_SYSTEM, {OC_EXPR(a), OC_EXPR(b), OC_EXPR(c)});
    Function::current()->expr_statement(expr);
}

template<typename N, typename T>
requires(is_all_float_vector3_v<expr_value_t<N>> && is_all_float_vector3_v<expr_value_t<T>>)
void make_normal_tangent(const N &n, const T &t, Var<float3> &a, Var<float3> &b) noexcept {
    auto expr = Function::current()->call_builtin(Type::of<expr_value_t<N>>(),
                                                  CallOp::MAKE_NORMAL_TANGENT, {OC_EXPR(n), OC_EXPR(t), OC_EXPR(a), OC_EXPR(b)});
    Function::current()->expr_statement(expr);
}

#define OC_MAKE_VEC_MAKER_DIM(type, tag, dim)                                      \
    template<typename... Args>                                                     \
    requires(any_device_type_v<Args...> && requires {                              \
        make_##type##dim(remove_device_t<Args>{}...);                              \
    })                                                                             \
    OC_NODISCARD auto make_##type##dim(const Args &...args) noexcept {             \
        auto impl = [&]<typename... As>(const As &...as) {                         \
            auto expr = Function::current()->call_builtin(Type::of<type##dim>(),   \
                                                          CallOp::MAKE_##tag##dim, \
                                                          {OC_EXPR(as)...});       \
            return eval<type##dim>(expr);                                          \
        };                                                                         \
        return impl(decay_swizzle(args)...);                                       \
    }

#define OC_MAKE_VEC_MAKER(type, tag)    \
    OC_MAKE_VEC_MAKER_DIM(type, tag, 2) \
    OC_MAKE_VEC_MAKER_DIM(type, tag, 3) \
    OC_MAKE_VEC_MAKER_DIM(type, tag, 4)

OC_MAKE_VEC_MAKER(int, INT)
OC_MAKE_VEC_MAKER(uint, UINT)
OC_MAKE_VEC_MAKER(float, FLOAT)
OC_MAKE_VEC_MAKER(half, HALF)
OC_MAKE_VEC_MAKER(real, REAL)
OC_MAKE_VEC_MAKER(bool, BOOL)
OC_MAKE_VEC_MAKER(uchar, UCHAR)

#undef OC_MAKE_VEC_MAKER_DIM
#undef OC_MAKE_VEC_MAKER

#define OC_MAKE_MATRIX(type, TYPE, N, M)                                                                 \
    template<typename... Args>                                                                           \
    requires(any_dsl_v<Args...> && requires {                                                            \
        make_##type##N##x##M(expr_value_t<Args>{}...);                                                   \
    })                                                                                                   \
    OC_NODISCARD auto make_##type##N##x##M(const Args &...args) {                                        \
        auto expr = Function::current()->call_builtin(Type::of<type##N##x##M>(),                         \
                                                      CallOp::MAKE_##TYPE##N##X##M, {OC_EXPR(args)...}); \
        return eval<type##N##x##M>(expr);                                                                \
    }

#define OC_MAKE_MATRIX_FOR_TYPE(type, TYPE) \
    OC_MAKE_MATRIX(type, TYPE, 2, 2)        \
    OC_MAKE_MATRIX(type, TYPE, 2, 3)        \
    OC_MAKE_MATRIX(type, TYPE, 2, 4)        \
    OC_MAKE_MATRIX(type, TYPE, 3, 2)        \
    OC_MAKE_MATRIX(type, TYPE, 3, 3)        \
    OC_MAKE_MATRIX(type, TYPE, 3, 4)        \
    OC_MAKE_MATRIX(type, TYPE, 4, 2)        \
    OC_MAKE_MATRIX(type, TYPE, 4, 3)        \
    OC_MAKE_MATRIX(type, TYPE, 4, 4)

OC_MAKE_MATRIX_FOR_TYPE(float, FLOAT)
OC_MAKE_MATRIX_FOR_TYPE(half, HALF)

#undef OC_MAKE_MATRIX_FOR_TYPE
#undef OC_MAKE_MATRIX

template<typename Ret = void, typename... Args>
auto call(string_view func_name, Args &&...args) noexcept {
    if constexpr (std::is_void_v<Ret>) {
        const CallExpr *expr = Function::current()->call(nullptr, func_name, {OC_EXPR(args)...});
        Function::current()->expr_statement(expr);
    } else {
        const CallExpr *expr = Function::current()->call(Type::of<Ret>(), func_name, {OC_EXPR(args)...});
        return eval<Ret>(expr);
    }
}

template<typename A, typename B>
requires concepts::plus_able<expr_value_t<A>, expr_value_t<B>>
auto atomic_add(A &&a, B &&b) noexcept {
    const Expression *expr = Function::current()->call_builtin(Type::of<expr_value_t<A>>(),
                                                               CallOp::ATOMIC_ADD,
                                                               {OC_EXPR(a), OC_EXPR(b)});
    return eval<expr_value_t<A>>(expr);
}

template<typename A, typename B>
requires concepts::minus_able<expr_value_t<A>, expr_value_t<B>>
auto atomic_sub(A &&a, B &&b) noexcept {
    const Expression *expr = Function::current()->call_builtin(Type::of<expr_value_t<A>>(),
                                                               CallOp::ATOMIC_SUB,
                                                               {OC_EXPR(a), OC_EXPR(b)});
    return eval<expr_value_t<A>>(expr);
}

template<typename A, typename B>
requires concepts::assign_able<expr_value_t<A>, expr_value_t<B>>
auto atomic_exch(A &&a, B &&b) noexcept {
    const Expression *expr = Function::current()->call_builtin(Type::of<expr_value_t<A>>(),
                                                               CallOp::ATOMIC_EXCH,
                                                               {OC_EXPR(a), OC_EXPR(b)});
    return eval<expr_value_t<A>>(expr);
}

template<typename T, typename U, typename V>
requires concepts::assign_able<expr_value_t<T>, expr_value_t<V>> &&
         concepts::assign_able<expr_value_t<T>, expr_value_t<V>>
auto atomic_CAS(T &ref, U &&compare, V &&val) {
    const Expression *expr = Function::current()->call_builtin(Type::of<expr_value_t<T>>(),
                                                               CallOp::ATOMIC_CAS,
                                                               {OC_EXPR(ref), OC_EXPR(compare),
                                                                OC_EXPR(val)});
    return eval<expr_value_t<T>>(expr);
}

template<typename T>
requires is_vector_v<expr_value_t<T>> || is_scalar_v<expr_value_t<T>>
[[nodiscard]] T zero_if_nan(T t) noexcept {
    return ocarina::select(ocarina::isnan(t), T{}, t);
}

template<typename T>
requires is_vector_v<expr_value_t<T>> || is_scalar_v<expr_value_t<T>>
[[nodiscard]] T zero_if_nan_inf(T t) noexcept {
    return ocarina::select(ocarina::isnan(t) || ocarina::isinf(t), T{}, t);
}

inline void unreachable() noexcept {
    Function::current()->expr_statement(Function::current()->call_builtin(nullptr, CallOp::UNREACHABLE, {}));
}

inline void synchronize_block() noexcept {
    Function::current()->expr_statement(Function::current()->call_builtin(nullptr, CallOp::SYNCHRONIZE_BLOCK, {}));
}

#define OC_MAKE_WARP_FUNC(func_name, Tag, ret_type)                                                \
    template<typename T>                                                                           \
    requires ocarina::is_boolean_expr_v<T>                                                         \
    [[nodiscard]] auto func_name(const T &pred) {                                                  \
        const Expression *expr = Function::current()->call_builtin(Type::of<ret_type>(),           \
                                                                   CallOp::WARP_ACTIVE_COUNT_BITS, \
                                                                   {OC_EXPR(pred)});               \
        return eval<ret_type>(expr);                                                               \
    }

OC_MAKE_WARP_FUNC(warp_active_bit_mask, WARP_ACTIVE_BIT_MASK, uint4)
OC_MAKE_WARP_FUNC(warp_active_count_bits, WARP_ACTIVE_COUNT_BITS, uint)
OC_MAKE_WARP_FUNC(warp_prefix_count_bits, WARP_PREFIX_COUNT_BITS, uint)
OC_MAKE_WARP_FUNC(warp_lane_id, WARP_LANE_ID, uint)
OC_MAKE_WARP_FUNC(warp_size, WARP_SIZE, uint)
OC_MAKE_WARP_FUNC(warp_first_active_lane, WARP_FIRST_ACTIVE_LANE, uint)
OC_MAKE_WARP_FUNC(warp_is_first_active_lane, WARP_IS_FIRST_ACTIVE_LANE, uint)

#undef OC_MAKE_WARP_FUNC

template<typename T>
void DynamicArray<T>::sanitize() noexcept {
    *this = map([&](const Var<T> &val) {
        return ocarina::select(ocarina::isnan(val) || ocarina::isinf(val), Var<T>(0), val);
    });
}

template<typename T>
Var<T> DynamicArray<T>::max() const noexcept {
    return reduce(0.f, [](auto r, auto x) noexcept {
        return ocarina::max(r, x);
    });
}
template<typename T>
Var<T> DynamicArray<T>::min() const noexcept {
    return reduce(std::numeric_limits<float>::max(), [](auto r, auto x) noexcept {
        return ocarina::min(r, x);
    });
}

template<typename T>
DynamicArray<T> DynamicArray<T>::clamp(const Var<T> &min_, const Var<T> &max_) const noexcept {
    return map([&](const Var<T> &val) {
        return ocarina::clamp(val, min_, max_);
    });
}

}// namespace ocarina