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

namespace horizon::dsl {
using namespace horizon::core;
using namespace horizon::math;
using namespace horizon::ast;

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
    explicit Var(const horizon::dsl::Expression *expression) noexcept
        : horizon::dsl::detail::Ref<org_type>(expression) {}
    Var(OC_APPEND_SRC_LOCATION) noexcept
        : Var(horizon::dsl::Function::current()->local(horizon::dsl::Type::of<org_type>(), OC_SRC_LOCATION)) {
        static_assert(!is_param_struct_v<T>);
        if constexpr (is_struct_v<T>) {
            Ref<T>::set(T{});
        }
    }
    Var(Var &&another) noexcept
        : Ref<T>(horizon::dsl::move(another)) {}

    Var<T> &set_symbol(const string &name) {
        auto variable_expr = static_cast<const VariableExpr *>(Super::expression());
        const_cast<VariableExpr *>(variable_expr)->variable().set_suffix(name);
        return *this;
    }

    Var(const Var &another) noexcept
        : Var() { horizon::dsl::detail::assign(*this, another); }
    template<typename Arg>
    requires horizon::dsl::concepts::non_pointer<std::remove_cvref_t<Arg>> &&
             concepts::different<std::remove_cvref_t<Arg>, Var<org_type>> &&
             requires(horizon::dsl::expr_value_t<org_type> a, horizon::dsl::expr_value_t<Arg> b) { a = b; }
    Var(Arg &&arg, OC_APPEND_SRC_LOCATION)
        : Var(OC_SRC_LOCATION) { horizon::dsl::detail::assign(*this, std::forward<Arg>(arg)); }
    explicit Var(horizon::dsl::detail::ArgumentCreation,
                 OC_APPEND_SRC_LOCATION) noexcept
        : Var(horizon::dsl::Function::current()->argument(horizon::dsl::Type::of<org_type>())) {}
    explicit Var(horizon::dsl::detail::ReferenceArgumentCreation, OC_APPEND_SRC_LOCATION) noexcept
        : Var(horizon::dsl::Function::current()->reference_argument(horizon::dsl::Type::of<org_type>())) {}
    template<typename Arg>
    requires requires(horizon::dsl::expr_value_t<org_type> a, horizon::dsl::remove_device_t<Arg> b) { a = b; }
    void operator=(Arg &&arg) {
        if constexpr (is_struct_v<Arg>) {
            Super::set(OC_FORWARD(arg));
        }
        else if constexpr (is_swizzle_v<Arg>) {
            *this = decay_swizzle(OC_FORWARD(arg));
        }
        else {
            horizon::dsl::detail::assign(*this, std::forward<Arg>(arg));
        }
    }
    void operator=(const Var &other) { horizon::dsl::detail::assign(*this, other); }
    OC_MAKE_GET_PROXY
private:
#define OC_MAKE_VAR_UNARY_FUNC(func, tag)                                           \
    [[nodiscard]] static auto call_##func(const dsl_type &val) noexcept {           \
        using ret_type = decltype(func(std::declval<T>()));                         \
        auto expr = Function::current()->call_builtin(Type::of<ret_type>(),         \
                                                      CallOp::tag, {OC_EXPR(val)}); \
        return eval<ret_type>(expr);                                                \
    }

    OC_MAKE_VAR_UNARY_FUNC(all, All)
    OC_MAKE_VAR_UNARY_FUNC(any, Any)
    OC_MAKE_VAR_UNARY_FUNC(none, None)

    OC_MAKE_VAR_UNARY_FUNC(rcp, Rcp)
    OC_MAKE_VAR_UNARY_FUNC(abs, Abs)
    OC_MAKE_VAR_UNARY_FUNC(sqrt, Sqrt)
    OC_MAKE_VAR_UNARY_FUNC(sqr, Sqr)
    OC_MAKE_VAR_UNARY_FUNC(exp, Exp)
    OC_MAKE_VAR_UNARY_FUNC(exp2, Exp2)
    OC_MAKE_VAR_UNARY_FUNC(exp10, Exp10)
    OC_MAKE_VAR_UNARY_FUNC(log, Log)
    OC_MAKE_VAR_UNARY_FUNC(log2, Log2)
    OC_MAKE_VAR_UNARY_FUNC(log10, Log10)
    OC_MAKE_VAR_UNARY_FUNC(cos, Cos)
    OC_MAKE_VAR_UNARY_FUNC(sin, Sin)
    OC_MAKE_VAR_UNARY_FUNC(tan, Tan)
    OC_MAKE_VAR_UNARY_FUNC(cosh, Cosh)
    OC_MAKE_VAR_UNARY_FUNC(sinh, Sinh)
    OC_MAKE_VAR_UNARY_FUNC(tanh, Tanh)
    OC_MAKE_VAR_UNARY_FUNC(acos, Acos)
    OC_MAKE_VAR_UNARY_FUNC(asin, Asin)
    OC_MAKE_VAR_UNARY_FUNC(atan, Atan)
    OC_MAKE_VAR_UNARY_FUNC(asinh, Asinh)
    OC_MAKE_VAR_UNARY_FUNC(acosh, Acosh)
    OC_MAKE_VAR_UNARY_FUNC(atanh, Atanh)
    OC_MAKE_VAR_UNARY_FUNC(degrees, Degrees)
    OC_MAKE_VAR_UNARY_FUNC(radians, Radians)
    OC_MAKE_VAR_UNARY_FUNC(ceil, Ceil)
    OC_MAKE_VAR_UNARY_FUNC(round, Round)
    OC_MAKE_VAR_UNARY_FUNC(floor, Floor)
    OC_MAKE_VAR_UNARY_FUNC(rsqrt, Rsqrt)
    OC_MAKE_VAR_UNARY_FUNC(isinf, IsInf)
    OC_MAKE_VAR_UNARY_FUNC(isnan, IsNan)
    OC_MAKE_VAR_UNARY_FUNC(fract, Fract)
    OC_MAKE_VAR_UNARY_FUNC(saturate, Saturate)
    OC_MAKE_VAR_UNARY_FUNC(sign, Sign)
    OC_MAKE_VAR_UNARY_FUNC(normalize, Normalize)
    OC_MAKE_VAR_UNARY_FUNC(length, Length)
    OC_MAKE_VAR_UNARY_FUNC(length_squared, LengthSquared)

    OC_MAKE_VAR_UNARY_FUNC(determinant, Determinant)
    OC_MAKE_VAR_UNARY_FUNC(transpose, Transpose)
    OC_MAKE_VAR_UNARY_FUNC(inverse, Inverse)

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

    OC_MAKE_VAR_BINARY_FUNC(max, Max)
    OC_MAKE_VAR_BINARY_FUNC(min, Min)
    OC_MAKE_VAR_BINARY_FUNC(pow, Pow)
    OC_MAKE_VAR_BINARY_FUNC(fmod, Fmod)
    OC_MAKE_VAR_BINARY_FUNC(mod, Mod)
    OC_MAKE_VAR_BINARY_FUNC(copysign, Copysign)
    OC_MAKE_VAR_BINARY_FUNC(atan2, Atan2)

    OC_MAKE_VAR_BINARY_FUNC(cross, Cross)
    OC_MAKE_VAR_BINARY_FUNC(dot, Dot)
    OC_MAKE_VAR_BINARY_FUNC(distance, Distance)
    OC_MAKE_VAR_BINARY_FUNC(distance_squared, DistanceSquared)

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

    OC_MAKE_VAR_TRIPLE_FUNC(clamp, Clamp)
    OC_MAKE_VAR_TRIPLE_FUNC(lerp, Lerp)
    OC_MAKE_VAR_TRIPLE_FUNC(inverse_lerp, InverseLerp)
    OC_MAKE_VAR_TRIPLE_FUNC(fma, Fma)

#undef OC_MAKE_VAR_TRIPLE_FUNC

    template<size_t N>
    requires(N == vector_dimension_v<T>)
    OC_NODISCARD static auto call_select(const Var<Vector<bool, N>> &pred,
                                         const dsl_type &t, const dsl_type &f) noexcept {
        const Expression *expr = Function::current()->call_builtin(Type::of<T>(),
                                                                   CallOp::Select,
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
                                                                   CallOp::Select,
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
                                                                   CallOp::FaceForward,
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

template<typename T>
using Texture3DViewVar = Var<Texture3DView<T>>;
template<typename T>
using Texture2DViewVar = Var<Texture2DView<T>>;
template<typename T>
using RWTexture3DViewVar = Var<RWTexture3DView<T>>;
template<typename T>
using RWTexture2DViewVar = Var<RWTexture2DView<T>>;

template<typename T>
using Texture3DVar = Texture3DViewVar<T>;
template<typename T>
using Texture2DVar = Texture2DViewVar<T>;

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

}// namespace horizon::dsl
