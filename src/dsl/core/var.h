//
// Created by Zero on 02/05/2022.
//

#pragma once

#include "core/stl.h"
#include "ref.h"
#include "ast/function.h"
#include "expr.h"
#include "math/basic_types.h"
#include "type_trait.h"
#include <source_location>

namespace ocarina {

namespace detail {
struct ArgumentCreation {};
struct ReferenceArgumentCreation {};
}// namespace detail

using detail::Ref;

template<typename T>
struct Var : public Ref<T> {
public:
    using org_type = T;
    using Super = Ref<T>;
    using Ref<T>::Ref;
    using dsl_type = Var<T>;
    friend class MemberAccessor;
    explicit Var(const ocarina::Expression *expression) noexcept
        : ocarina::detail::Ref<org_type>(expression) {}
    Var(OC_APPEND_SRC_LOCATION) noexcept
        : Var(ocarina::Function::current()->local(ocarina::Type::of<org_type>(), OC_SRC_LOCATION)) {
        static_assert(!is_param_struct_v<T>);
        if constexpr (is_struct_v<T>) {
            Ref<T>::set(T{});
        }
    }
    Var(Var &&another) noexcept
        : Ref<T>(ocarina::move(another)) {}

    Var<T> &set_symbol(const string &name) {
        auto variable_expr = static_cast<const VariableExpr *>(Super::expression());
        const_cast<VariableExpr *>(variable_expr)->variable().set_suffix(name);
        return *this;
    }

    Var(const Var &another) noexcept
        : Var() { ocarina::detail::assign(*this, another); }
    template<typename Arg>
    requires ocarina::concepts::non_pointer<std::remove_cvref_t<Arg>> &&
             concepts::different<std::remove_cvref_t<Arg>, Var<org_type>> &&
             requires(ocarina::expr_value_t<org_type> a, ocarina::expr_value_t<Arg> b) { a = b; }
    Var(Arg &&arg, OC_APPEND_SRC_LOCATION)
        : Var(OC_SRC_LOCATION) { ocarina::detail::assign(*this, std::forward<Arg>(arg)); }
    explicit Var(ocarina::detail::ArgumentCreation,
                 OC_APPEND_SRC_LOCATION) noexcept
        : Var(ocarina::Function::current()->argument(ocarina::Type::of<org_type>())) {}
    explicit Var(ocarina::detail::ReferenceArgumentCreation, OC_APPEND_SRC_LOCATION) noexcept
        : Var(ocarina::Function::current()->reference_argument(ocarina::Type::of<org_type>())) {}
    template<typename Arg>
    requires requires(ocarina::expr_value_t<org_type> a, ocarina::remove_device_t<Arg> b) { a = b; }
    void operator=(Arg &&arg) {
        if constexpr (is_struct_v<Arg>) {
            Super::set(OC_FORWARD(arg));
        }
        else if constexpr (is_swizzle_v<Arg>) {
            *this = decay_swizzle(OC_FORWARD(arg));
        }
        else {
            ocarina::detail::assign(*this, std::forward<Arg>(arg));
        }
    }
    void operator=(const Var &other) { ocarina::detail::assign(*this, other); }
    OC_MAKE_GET_PROXY
private:
#define OC_MAKE_VAR_UNARY_FUNC(func, tag)                                           \
    [[nodiscard]] static auto call_##func(const dsl_type &val) noexcept {           \
        using ret_type = decltype(func(std::declval<T>()));                         \
        auto expr = Function::current()->call_builtin(Type::of<ret_type>(),         \
                                                      CallOp::tag, {OC_EXPR(val)}); \
        return eval<ret_type>(expr);                                                \
    }

    OC_MAKE_VAR_UNARY_FUNC(all, ALL)
    OC_MAKE_VAR_UNARY_FUNC(any, ANY)
    OC_MAKE_VAR_UNARY_FUNC(none, NONE)

    OC_MAKE_VAR_UNARY_FUNC(rcp, RCP)
    OC_MAKE_VAR_UNARY_FUNC(abs, ABS)
    OC_MAKE_VAR_UNARY_FUNC(sqrt, SQRT)
    OC_MAKE_VAR_UNARY_FUNC(sqr, SQR)
    OC_MAKE_VAR_UNARY_FUNC(exp, EXP)
    OC_MAKE_VAR_UNARY_FUNC(exp2, EXP2)
    OC_MAKE_VAR_UNARY_FUNC(exp10, EXP10)
    OC_MAKE_VAR_UNARY_FUNC(log, LOG)
    OC_MAKE_VAR_UNARY_FUNC(log2, LOG2)
    OC_MAKE_VAR_UNARY_FUNC(log10, LOG10)
    OC_MAKE_VAR_UNARY_FUNC(cos, COS)
    OC_MAKE_VAR_UNARY_FUNC(sin, SIN)
    OC_MAKE_VAR_UNARY_FUNC(tan, TAN)
    OC_MAKE_VAR_UNARY_FUNC(cosh, COSH)
    OC_MAKE_VAR_UNARY_FUNC(sinh, SINH)
    OC_MAKE_VAR_UNARY_FUNC(tanh, TANH)
    OC_MAKE_VAR_UNARY_FUNC(acos, ACOS)
    OC_MAKE_VAR_UNARY_FUNC(asin, ASIN)
    OC_MAKE_VAR_UNARY_FUNC(atan, ATAN)
    OC_MAKE_VAR_UNARY_FUNC(asinh, ASINH)
    OC_MAKE_VAR_UNARY_FUNC(acosh, ACOSH)
    OC_MAKE_VAR_UNARY_FUNC(atanh, ATANH)
    OC_MAKE_VAR_UNARY_FUNC(degrees, DEGREES)
    OC_MAKE_VAR_UNARY_FUNC(radians, RADIANS)
    OC_MAKE_VAR_UNARY_FUNC(ceil, CEIL)
    OC_MAKE_VAR_UNARY_FUNC(round, ROUND)
    OC_MAKE_VAR_UNARY_FUNC(floor, FLOOR)
    OC_MAKE_VAR_UNARY_FUNC(rsqrt, RSQRT)
    OC_MAKE_VAR_UNARY_FUNC(isinf, IS_INF)
    OC_MAKE_VAR_UNARY_FUNC(isnan, IS_NAN)
    OC_MAKE_VAR_UNARY_FUNC(fract, FRACT)
    OC_MAKE_VAR_UNARY_FUNC(saturate, SATURATE)
    OC_MAKE_VAR_UNARY_FUNC(sign, SIGN)
    OC_MAKE_VAR_UNARY_FUNC(normalize, NORMALIZE)
    OC_MAKE_VAR_UNARY_FUNC(length, LENGTH)
    OC_MAKE_VAR_UNARY_FUNC(length_squared, LENGTH_SQUARED)

    OC_MAKE_VAR_UNARY_FUNC(determinant, DETERMINANT)
    OC_MAKE_VAR_UNARY_FUNC(transpose, TRANSPOSE)
    OC_MAKE_VAR_UNARY_FUNC(inverse, INVERSE)

#undef OC_MAKE_VAR_LOGIC_FUNC

#define OC_MAKE_VAR_BINARY_FUNC(func, tag)                                           \
    OC_NODISCARD static auto call_##func(const dsl_type &lhs,                        \
                                         const dsl_type &rhs) noexcept {             \
        using ret_type = decltype(func(std::declval<T>(), std::declval<T>()));       \
        auto expr = Function::current()->call_builtin(Type::of<T>(),                 \
                                                      CallOp::tag,                   \
                                                      {OC_EXPR(lhs), OC_EXPR(rhs)}); \
        return eval<ret_type>(expr);                                                 \
    }

    OC_MAKE_VAR_BINARY_FUNC(max, MAX)
    OC_MAKE_VAR_BINARY_FUNC(min, MIN)
    OC_MAKE_VAR_BINARY_FUNC(pow, POW)
    OC_MAKE_VAR_BINARY_FUNC(fmod, FMOD)
    OC_MAKE_VAR_BINARY_FUNC(mod, MOD)
    OC_MAKE_VAR_BINARY_FUNC(copysign, COPYSIGN)
    OC_MAKE_VAR_BINARY_FUNC(atan2, ATAN2)

    OC_MAKE_VAR_BINARY_FUNC(cross, CROSS)
    OC_MAKE_VAR_BINARY_FUNC(dot, DOT)
    OC_MAKE_VAR_BINARY_FUNC(distance, DISTANCE)
    OC_MAKE_VAR_BINARY_FUNC(distance_squared, DISTANCE_SQUARED)

#undef OC_MAKE_VAR_BINARY_FUNC

#define OC_MAKE_VAR_TRIPLE_FUNC(func, tag)                             \
    OC_NODISCARD static auto call_##func(const dsl_type &a,            \
                                         const dsl_type &b,            \
                                         const dsl_type &c) noexcept { \
        using ret_type = decltype(func(std::declval<T>(),              \
                                       std::declval<T>(),              \
                                       std::declval<T>()));            \
        auto expr = Function::current()->call_builtin(Type::of<T>(),   \
                                                      CallOp::tag,     \
                                                      {OC_EXPR(a),     \
                                                       OC_EXPR(b),     \
                                                       OC_EXPR(c)});   \
        return eval<ret_type>(expr);                                   \
    }

    OC_MAKE_VAR_TRIPLE_FUNC(clamp, CLAMP)
    OC_MAKE_VAR_TRIPLE_FUNC(lerp, LERP)
    OC_MAKE_VAR_TRIPLE_FUNC(inverse_lerp, INVERSE_LERP)
    OC_MAKE_VAR_TRIPLE_FUNC(fma, FMA)

#undef OC_MAKE_VAR_TRIPLE_FUNC

    template<size_t N>
    requires(N == vector_dimension_v<T>)
    OC_NODISCARD static auto call_select(const Var<Vector<bool, N>> &pred,
                                         const dsl_type &t, const dsl_type &f) noexcept {
        const Expression *expr = Function::current()->call_builtin(Type::of<T>(),
                                                                   CallOp::SELECT,
                                                                   {OC_EXPR(pred),
                                                                    OC_EXPR(t),
                                                                    OC_EXPR(f)});
        return eval<T>(expr);
    }

    template<size_t N>
    requires(N == vector_dimension_v<T>)
    OC_NODISCARD static auto call_select(const Vector<bool, N> &pred,
                                         const dsl_type &t, const dsl_type &f) noexcept {
        return call_select(Var<Vector<bool, N>>(pred), t, f);
    }

    static auto call_select(const Var<bool> &pred, const dsl_type &t, const dsl_type &f) noexcept {
        const Expression *expr = Function::current()->call_builtin(Type::of<T>(),
                                                                   CallOp::SELECT,
                                                                   {OC_EXPR(pred),
                                                                    OC_EXPR(t),
                                                                    OC_EXPR(f)});
        return eval<T>(expr);
    }

    template<typename... Args>
    requires((sizeof...(Args) == 1 || sizeof...(Args) == 2) &&
             is_all_float_vector3_v<remove_device_t<Args>...>)
    static auto call_face_forward(const dsl_type &n, Args &&...args) {
        const Expression *expr = Function::current()->call_builtin(Type::of<T>(),
                                                                   CallOp::FACE_FORWARD,
                                                                   {OC_EXPR(n),
                                                                    OC_EXPR(args)...});
        return eval<T>(expr);
    }
};

template<size_t N, typename T>
[[nodiscard]] auto to_general_vector(const T &val) noexcept {
    if constexpr (N > 1) {
        if constexpr (is_swizzle_v<T>) {
            return decay_swizzle(val);
        } else if constexpr (is_scalar_v<T>) {
            return Vector<T, N>(val);
        } else if constexpr (is_scalar_v<expr_value_t<T>> && is_dsl_v<T>) {
            Var<Vector<expr_value_t<T>, N>> ret;
            for (int i = 0; i < N; ++i) {
                ret[i] = val;
            }
            return ret;
        } else {
            return val;
        }
    } else {
        return val;
    }
}

template<typename T>
using BufferVar = Var<Buffer<T>>;

using ByteBufferVar = Var<ByteBuffer>;

using Texture3DVar = Var<Texture3D>;
using Texture2DVar = Var<Texture2D>;

using BindlessArrayVar = Var<BindlessArray>;

#define OC_MAKE_DSL_TYPE_IMPL(dsl_type, type, dim) \
    using dsl_type##dim = Var<type##dim>;

#define OC_MAKE_DSL_TYPE(dsl_type, type)     \
    OC_MAKE_DSL_TYPE_IMPL(dsl_type, type, )  \
    OC_MAKE_DSL_TYPE_IMPL(dsl_type, type, 2) \
    OC_MAKE_DSL_TYPE_IMPL(dsl_type, type, 3) \
    OC_MAKE_DSL_TYPE_IMPL(dsl_type, type, 4)

OC_MAKE_DSL_TYPE(Int, int)
OC_MAKE_DSL_TYPE(Uint, uint)
OC_MAKE_DSL_TYPE(Ulong, ulong)
OC_MAKE_DSL_TYPE(Float, float)
OC_MAKE_DSL_TYPE(Half, half)
OC_MAKE_DSL_TYPE(Uchar, uchar)
OC_MAKE_DSL_TYPE(Char, char)
OC_MAKE_DSL_TYPE(Short, short)
OC_MAKE_DSL_TYPE(Ushort, ushort)
OC_MAKE_DSL_TYPE(Bool, bool)

#undef OC_MAKE_DSL_TYPE
#undef OC_MAKE_DSL_TYPE_IMPL

#define OC_MAKE_DSL_MATRIX(Alias, type, N, M) \
    using Alias##N##x##M = Var<Matrix<type, N, M>>;

#define OC_MAKE_DSL_FOR_TYPE(Alias, type) \
    OC_MAKE_DSL_MATRIX(Alias, type, 2, 2) \
    OC_MAKE_DSL_MATRIX(Alias, type, 2, 3) \
    OC_MAKE_DSL_MATRIX(Alias, type, 2, 4) \
    OC_MAKE_DSL_MATRIX(Alias, type, 3, 2) \
    OC_MAKE_DSL_MATRIX(Alias, type, 3, 3) \
    OC_MAKE_DSL_MATRIX(Alias, type, 3, 4) \
    OC_MAKE_DSL_MATRIX(Alias, type, 4, 2) \
    OC_MAKE_DSL_MATRIX(Alias, type, 4, 3) \
    OC_MAKE_DSL_MATRIX(Alias, type, 4, 4)

OC_MAKE_DSL_FOR_TYPE(Float, float)
OC_MAKE_DSL_FOR_TYPE(Half, half)

#undef OC_MAKE_DSL_FOR_TYPE
#undef OC_MAKE_DSL_MATRIX

template<typename T>
Var(T &&) -> Var<expr_value_t<T>>;

template<typename T>
Var(const Buffer<T> &) -> Var<Buffer<T>>;

#define Def(Type, var_name) \
    Type var_name;          \
    var_name.set_symbol(#var_name)

}// namespace ocarina