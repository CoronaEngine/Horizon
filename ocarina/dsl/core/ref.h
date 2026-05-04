//
// Created by Zero on 19/05/2022.
//

#pragma once

#include "ref_func.h"

namespace ocarina {
namespace detail {

#define OC_REF_COMMON(...)                                                              \
private:                                                                                \
    const Expression *expression_{nullptr};                                             \
                                                                                        \
public:                                                                                 \
    [[nodiscard]] const Expression *expression() const noexcept { return expression_; } \
    [[nodiscard]] bool is_valid() const noexcept {                                      \
        return expression()->check_context(Function::current());                        \
    }                                                                                   \
                                                                                        \
protected:                                                                              \
    explicit Ref(const Expression *e) noexcept : expression_{e} {}                      \
    Ref(Ref &&) noexcept = default;                                                     \
    Ref(const Ref &) noexcept = default;

#define OC_MAKE_ASSIGNMENT_FUNC \
public:                         \
    void                        \
    set(const this_type &t) {   \
        assign(*this, t);       \
    }

template<typename T>
struct Ref
    : detail::EnableBitwiseCast<Ref<T>>,
      detail::EnableStaticCast<Ref<T>> {
    using this_type = T;
    static_assert(is_scalar_v<T>);
    OC_REF_COMMON(Ref<T>)
    OC_MAKE_ASSIGNMENT_FUNC
};

template<typename T>
struct Ref<Vector<T, 2>>
    : detail::EnableStaticCast<Ref<Vector<T, 2>>>,
      detail::EnableBitwiseCast<Ref<Vector<T, 2>>>,
      detail::EnableGetMemberByIndex<Ref<Vector<T, 2>>>,
      detail::EnableSubscriptAccess<Ref<Vector<T, 2>>> {
    using this_type = Vector<T, 2>;
    template<size_t... index>
    using swizzle_type = Swizzle<Var<T>, 2, index...>;
    OC_REF_COMMON(Ref<Vector<T, 2>>)
    OC_MAKE_ASSIGNMENT_FUNC
public:
    explicit Ref(const Var<T> &arg) noexcept
        : expression_(ocarina::Function::current()->local(ocarina::Type::of<this_type>())) {
        x = arg;
        y = arg;
    }
    explicit Ref(const T &arg) noexcept
        : Ref{Var<T>{arg}} {}

public:
    Var<T> x{Function::current()->swizzle(Type::of<T>(), expression(), 0, 1)};
    Var<T> y{Function::current()->swizzle(Type::of<T>(), expression(), 1, 1)};
#include "math/swizzle_inl/swizzle2.inl.h"
};

template<typename T>
struct Ref<Vector<T, 3>>
    : detail::EnableStaticCast<Ref<Vector<T, 3>>>,
      detail::EnableBitwiseCast<Ref<Vector<T, 3>>>,
      detail::EnableGetMemberByIndex<Ref<Vector<T, 3>>>,
      detail::EnableSubscriptAccess<Ref<Vector<T, 3>>> {
    using this_type = Vector<T, 3>;
    template<size_t... index>
    using swizzle_type = Swizzle<Var<T>, 3, index...>;
    OC_REF_COMMON(Ref<Vector<T, 3>>)
    OC_MAKE_ASSIGNMENT_FUNC
public:
    explicit Ref(const Var<T> &arg) noexcept
        : expression_(ocarina::Function::current()->local(ocarina::Type::of<this_type>())) {
        x = arg;
        y = arg;
        z = arg;
    }
    explicit Ref(const T &arg) noexcept
        : Ref{Var<T>{arg}} {}

public:
    Var<T> x{Function::current()->swizzle(Type::of<T>(), expression(), 0, 1)};
    Var<T> y{Function::current()->swizzle(Type::of<T>(), expression(), 1, 1)};
    Var<T> z{Function::current()->swizzle(Type::of<T>(), expression(), 2, 1)};
#include "math/swizzle_inl/swizzle3.inl.h"
};

template<typename T>
struct Ref<Vector<T, 4>>
    : detail::EnableStaticCast<Ref<Vector<T, 4>>>,
      detail::EnableBitwiseCast<Ref<Vector<T, 4>>>,
      detail::EnableGetMemberByIndex<Ref<Vector<T, 4>>>,
      detail::EnableSubscriptAccess<Ref<Vector<T, 4>>> {
    using this_type = Vector<T, 4>;
    template<size_t... index>
    using swizzle_type = Swizzle<Var<T>, 4, index...>;
    OC_REF_COMMON(Ref<Vector<T, 4>>)
    OC_MAKE_ASSIGNMENT_FUNC
public:
    explicit Ref(const Var<T> &arg) noexcept
        : expression_(ocarina::Function::current()->local(ocarina::Type::of<this_type>())) {
        x = arg;
        y = arg;
        z = arg;
        w = arg;
    }
    explicit Ref(const T &arg) noexcept
        : Ref{Var<T>{arg}} {}

public:
    Var<T> x{Function::current()->swizzle(Type::of<T>(), expression(), 0, 1)};
    Var<T> y{Function::current()->swizzle(Type::of<T>(), expression(), 1, 1)};
    Var<T> z{Function::current()->swizzle(Type::of<T>(), expression(), 2, 1)};
    Var<T> w{Function::current()->swizzle(Type::of<T>(), expression(), 3, 1)};
#include "math/swizzle_inl/swizzle4.inl.h"
};

template<typename T, size_t N, size_t M>
struct Ref<Matrix<T, N, M>>
    : detail::EnableGetMemberByIndex<Ref<Matrix<T, N, M>>>,
      detail::EnableSubscriptAccess<Ref<Matrix<T, N, M>>> {
    OC_REF_COMMON(Ref<Matrix<T, N, M>>)
    using this_type = Matrix<T, N, M>;
    OC_MAKE_ASSIGNMENT_FUNC
};

template<typename T, size_t N>
struct Ref<ocarina::array<T, N>>
    : detail::EnableSubscriptAccess<Ref<ocarina::array<T, N>>>,
      detail::EnableGetMemberByIndex<Ref<ocarina::array<T, N>>> {
    using this_type = ocarina::array<T, N>;
    OC_REF_COMMON(Ref<ocarina::array<T, N>>)
public:
    void set(const this_type &t) {
        for (int i = 0; i < N; ++i) {
            (*this)[i] = t[i];
        }
    }
    void set(const Var<array<T, N>> &t) {
        for (int i = 0; i < N; ++i) {
            (*this)[i] = t[i];
        }
    }
    template<typename U>
    requires is_scalar_v<U> && is_same_v<U, T>
    void set(const Var<Vector<U, N>> &t) {
        for (int i = 0; i < N; ++i) {
            (*this)[i] = t[i];
        }
    }
    [[nodiscard]] Var<T> as_scalar() const noexcept {
        return (*this)[0];
    }
    template<size_t Dim = N>
    [[nodiscard]] auto as_vec() const noexcept {
        static_assert(Dim <= N);
        if constexpr (Dim == 1) {
            return as_scalar();
        } else {
            Var<Vector<T, Dim>> ret;
            for (int i = 0; i < Dim; ++i) {
                ret[i] = (*this)[i];
            }
            return ret;
        }
    }
    [[nodiscard]] Var<Vector<T, 2>> as_vec2() const noexcept { return as_vec<2>(); }
    [[nodiscard]] Var<Vector<T, 3>> as_vec3() const noexcept { return as_vec<3>(); }
    [[nodiscard]] Var<Vector<T, 4>> as_vec4() const noexcept { return as_vec<4>(); }
#include "../detail/swizzle/array_swizzle.inl.h"
};

template<typename T, size_t N>
struct Ref<T[N]>
    : detail::EnableSubscriptAccess<Ref<T[N]>>,
      detail::EnableGetMemberByIndex<Ref<T[N]>> {
    using this_type = T[N];
    OC_REF_COMMON(Ref<T[N]>)
public:
    void set(const this_type &t) {
        for (int i = 0; i < N; ++i) {
            (*this)[i] = t[i];
        }
    }
};

template<typename T>
struct Ref<Buffer<T>>
    : detail::EnableReadAndWrite<Ref<Buffer<T>>>,
      detail::EnableSubscriptAccess<Ref<Buffer<T>>>,
      detail::BufferAsAtomicAddress<Ref<Buffer<T>>, T> {
    OC_REF_COMMON(Ref<Buffer<T>>)

public:
    void set(const BufferDesc<T> &buffer) noexcept {
        /// empty
    }
    template<typename int_type = ulong>
    [[nodiscard]] auto size() const noexcept {
        const CallExpr *expr = Function::current()->call_builtin(Type::of<int_type>(), CallOp::BUFFER_SIZE, {expression()});
        return eval<int_type>(expr);
    }
};

template<>
struct Ref<ByteBuffer>
    : detail::EnableByteLoadAndStore<Ref<ByteBuffer>>,
      detail::BufferAsAtomicAddress<Ref<ByteBuffer>, uint> {
    OC_REF_COMMON(Ref<ByteBuffer>)

public:
    void set(const BufferDesc<> &buffer) noexcept {
        /// empty
    }
    template<typename int_type = uint>
    [[nodiscard]] auto size_in_byte() const noexcept {
        const CallExpr *expr = Function::current()->call_builtin(Type::of<int_type>(), CallOp::BYTE_BUFFER_SIZE, {expression()});
        return eval<int_type>(expr);
    }

    template<typename int_type = uint>
    [[nodiscard]] auto size() const noexcept {
        return size_in_byte<int_type>();
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] SOAViewVar<Elm, Ref<ByteBuffer>> soa_view_var(const Var<int_type> &view_size = InvalidUI32) const noexcept {
        return SOAViewVar<Elm, Ref<ByteBuffer>>(*this, view_size);
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] AOSViewVar<Elm, Ref<ByteBuffer>> aos_view_var(const Var<int_type> &view_size = InvalidUI32) const noexcept {
        return AOSViewVar<Elm, Ref<ByteBuffer>>(*this, view_size);
    }
};

template<>
struct Ref<Texture3D>
    : detail::EnableTextureSample<Ref<Texture3D>, 3>,
      detail::EnableTextureReadAndWrite<Ref<Texture3D>, 3> {
    OC_REF_COMMON(Ref<Texture3D>)
};

template<>
struct Ref<Texture2D>
    : detail::EnableTextureSample<Ref<Texture2D>, 2>,
      detail::EnableTextureReadAndWrite<Ref<Texture2D>, 2> {
    OC_REF_COMMON(Ref<Texture2D>)
};

template<>
struct Ref<Accel> {
public:
    [[nodiscard]] inline Var<TriangleHit> trace_closest(const Var<Ray> &ray) const noexcept;// implement in ref.inl
    [[nodiscard]] inline Var<bool> trace_occlusion(const Var<Ray> &ray) const noexcept;     // implement in ref.inl
    OC_REF_COMMON(Ref<Accel>)
};

template<typename... T>
struct Ref<ocarina::tuple<T...>> {
    using Tuple = ocarina::tuple<T...>;
    OC_REF_COMMON(Ref<ocarina::tuple<T...>>)

private:
    template<uint i>
    requires(i >= sizeof...(T))
    void _assignment(const Tuple &t) noexcept {}
    template<uint i = 0>
    requires(i < sizeof...(T))
    void _assignment(const Tuple &t) noexcept {
        set<i>(ocarina::get<i>(t));
        _assignment<i + 1>(t);
    }

public:
    template<size_t i>
    [[nodiscard]] auto get() const noexcept {
        using Elm = ocarina::tuple_element_t<i, Tuple>;
        return eval<Elm>(Function::current()->member(Type::of<Elm>(), expression(), i));
    }
    template<size_t i, typename elm_ty = ocarina::tuple_element_t<i, Tuple>>
    void set(elm_ty val) noexcept {
        auto expr = Function::current()->member(Type::of<expr_value_t<elm_ty>>(), expression(), i);
        assign(expr, val);
    }

    void set(const Tuple &t) {
        _assignment(t);
    };
};

}// namespace detail

template<typename T>
class BindlessArrayBuffer {
    static_assert(is_valid_buffer_element_v<T>);

private:
    const Expression *bindless_array_{nullptr};
    const Expression *index_{nullptr};

public:
    BindlessArrayBuffer(const Expression *array, const Expression *index) noexcept
        : bindless_array_{array}, index_{index} {}

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    [[nodiscard]] Var<T> read(Index index) const noexcept {
        if constexpr (is_dsl_v<Index>) {
            index = detail::correct_index(index, size(), typeid(*this).name(), traceback_string());
        }
        const CallExpr *expr = Function::current()->call_builtin(Type::of<T>(), CallOp::BINDLESS_ARRAY_BUFFER_READ,
                                                                 {bindless_array_, index_, OC_EXPR(index)});
        return eval<T>(expr);
    }

    template<typename Size = uint>
    [[nodiscard]] Var<Size> size_in_byte() const noexcept {
        const CallExpr *expr = Function::current()->call_builtin(Type::of<Size>(), CallOp::BINDLESS_ARRAY_BUFFER_SIZE,
                                                                 {bindless_array_, index_});
        return eval<Size>(expr);
    }

    template<typename Size = uint>
    [[nodiscard]] Var<Size> size() const noexcept {
        Var<Size> ret = size_in_byte<Size>();
        return detail::divide(ret, static_cast<uint>(sizeof(T)));
    }

    template<typename Index, typename Val>
    requires concepts::integral<expr_value_t<Index>> && ocarina::is_same_v<T, expr_value_t<Val>>
    void write(Index index, Val &&elm) {
        if constexpr (is_dsl_v<Index>) {
            index = detail::correct_index(index, size(), typeid(*this).name(), traceback_string());
        }
        const CallExpr *expr = Function::current()->call_builtin(nullptr, CallOp::BINDLESS_ARRAY_BUFFER_WRITE,
                                                                 {bindless_array_, index_, OC_EXPR(index), OC_EXPR(elm)});
        Function::current()->expr_statement(expr);
    }
};

class BindlessArrayByteBuffer {
private:
    const Expression *bindless_array_{nullptr};
    const Expression *index_{nullptr};

public:
    BindlessArrayByteBuffer(const Expression *array, const Expression *index) noexcept
        : bindless_array_{array}, index_{index} {}

    template<typename T, typename Offset, typename Size = uint>
    requires concepts::integral<expr_value_t<Offset>>
    [[nodiscard]] Var<T> load_as(Offset &&offset, bool check_boundary = true) const noexcept {
        if (check_boundary) {
            offset = detail::correct_index(offset, size_in_byte<Size>(), typeid(*this).name(), traceback_string());
        }
        const CallExpr *expr = Function::current()->call_builtin(Type::of<T>(), CallOp::BINDLESS_ARRAY_BYTE_BUFFER_READ,
                                                                 {bindless_array_, index_, OC_EXPR(offset)});
        return eval<T>(expr);
    }

    template<typename T, typename Offset, typename Size = uint>
    requires concepts::integral<expr_value_t<Offset>>
    [[nodiscard]] Var<T> &load_as(Offset &&offset, bool check_boundary = true) noexcept {
        if (check_boundary) {
            offset = detail::correct_index(offset, size_in_byte<Size>(), typeid(*this).name(), traceback_string());
        }
        const CallExpr *expr = Function::current()->call_builtin(Type::of<T>(), CallOp::BINDLESS_ARRAY_BYTE_BUFFER_READ,
                                                                 {bindless_array_, index_, OC_EXPR(offset)});
        Var<T> *ret = Function::current()->template create_temp_obj<Var<T>>(expr);
        return *ret;
    }

    template<typename Elm, typename Offset, typename Size = uint>
    requires is_integral_expr_v<Offset>
    void store(Offset &&offset, const Elm &val, bool check_boundary = true) noexcept {
        if (check_boundary) {
            offset = detail::correct_index(offset, size_in_byte<Size>(), typeid(*this).name(), traceback_string());
        }
        const CallExpr *expr = Function::current()->call_builtin(nullptr, CallOp::BINDLESS_ARRAY_BYTE_BUFFER_WRITE,
                                                                 {bindless_array_, index_,
                                                                  OC_EXPR(offset), OC_EXPR(val)});
        Function::current()->expr_statement(expr);
    }

    template<typename Size = uint>
    [[nodiscard]] Var<Size> size_in_byte() const noexcept {
        const CallExpr *expr = Function::current()->call_builtin(Type::of<Size>(), CallOp::BINDLESS_ARRAY_BUFFER_SIZE,
                                                                 {bindless_array_, index_});
        return eval<Size>(expr);
    }

    template<typename Size = uint>
    [[nodiscard]] Var<Size> size() const noexcept {
        Var<Size> ret = size_in_byte<Size>();
        return ret;
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] SOAViewVar<Elm, BindlessArrayByteBuffer> soa_view_var(const Var<int_type> &view_size = InvalidUI32) const noexcept {
        return SOAViewVar<Elm, BindlessArrayByteBuffer>(*this, view_size);
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] AOSViewVar<Elm, BindlessArrayByteBuffer> aos_view_var(const Var<int_type> &view_size = InvalidUI32) const noexcept {
        return AOSViewVar<Elm, BindlessArrayByteBuffer>(*this, view_size);
    }

    template<typename T, typename Offset>
    [[nodiscard]] DynamicArray<T> load_dynamic_array(uint array_size, Offset &&offset)
        const noexcept;// implement in dsl/array.h
};

template<size_t Dim>
class BindlessArrayTexture {
private:
    static_assert(Dim == 2 || Dim == 3, "The dimension of texture must be 2 or 3!");
    static constexpr CallOp call_op = Dim == 2 ? CallOp::BINDLESS_ARRAY_TEX2D_SAMPLE :
                                                 CallOp::BINDLESS_ARRAY_TEX3D_SAMPLE;
    const Expression *bindless_array_{nullptr};
    const Expression *index_{nullptr};

public:
    BindlessArrayTexture(const Expression *array, const Expression *index) noexcept
        : bindless_array_{array}, index_{index} {}

    template<typename U, typename V, typename W>
    requires(is_all_floating_point_expr_v<U, V, W>)
    OC_NODISCARD DynamicArray<float> sample(uint channel_num, const U &u, const V &v, const W &w)
        const noexcept;// implement in dsl/array.h

    template<typename U, typename V>
    requires(is_all_floating_point_expr_v<U, V>)
    OC_NODISCARD DynamicArray<float> sample(uint channel_num, const U &u, const V &v)
        const noexcept;// implement in dsl/array.h

    template<typename UVW>
    requires(is_general_float_vector3_v<remove_device_t<UVW>>)
    OC_NODISCARD DynamicArray<float> sample(uint channel_num, const UVW &uvw)
        const noexcept;// implement in dsl/array.h

    template<typename UV>
    requires(is_general_float_vector2_v<remove_device_t<UV>>)
    OC_NODISCARD DynamicArray<float> sample(uint channel_num, const UV &uv)
        const noexcept;// implement in dsl/array.h
};

namespace detail {
template<>
struct Ref<BindlessArray> {
    OC_REF_COMMON(Ref<BindlessArray>)
public:
    template<typename T, typename Index>
    requires concepts::integral<expr_value_t<Index>>
    [[nodiscard]] BindlessArrayBuffer<T> buffer_var(Index index, const string &desc = "",
                                                    uint buffer_num = 0) const noexcept {
        if (buffer_num != 0) {
            if constexpr (is_integral_v<Index>) {
                OC_ASSERT(index < buffer_num);
            } else {
                index = correct_index(index, buffer_num, desc, traceback_string(1));
            }
        }
        return BindlessArrayBuffer<T>(expression(), OC_EXPR(index));
    }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    [[nodiscard]] BindlessArrayTexture<3> tex3d_var(Index index, const string &desc = "",
                                                    uint tex_num = 0) const noexcept {
        if (tex_num != 0) {
            if constexpr (is_integral_v<Index>) {
                OC_ASSERT(index < tex_num);
            } else {
                index = correct_index(index, tex_num, desc, traceback_string(1));
            }
        }
        return BindlessArrayTexture<3>(expression(), OC_EXPR(index));
    }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    [[nodiscard]] BindlessArrayTexture<2> tex2d_var(Index index, const string &desc = "",
                                                    uint tex_num = 0) const noexcept {
        if (tex_num != 0) {
            if constexpr (is_integral_v<Index>) {
                OC_ASSERT(index < tex_num);
            } else {
                index = correct_index(index, tex_num, desc, traceback_string(1));
            }
        }
        return BindlessArrayTexture<2>(expression(), OC_EXPR(index));
    }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    [[nodiscard]] BindlessArrayByteBuffer byte_buffer_var(Index index, const string &desc = "",
                                                          uint buffer_num = 0) const noexcept {
        if (buffer_num != 0) {
            if constexpr (is_integral_v<Index>) {
                OC_ASSERT(index < buffer_num);
            } else {
                index = correct_index(index, buffer_num, desc, traceback_string(1));
            }
        }
        return BindlessArrayByteBuffer(expression(), OC_EXPR(index));
    }

    template<typename Elm, typename Index>
    requires concepts::integral<expr_value_t<Index>>
    [[nodiscard]] SOAViewVar<Elm, BindlessArrayByteBuffer> soa_view_var(Index &&index, const string &desc = "",
                                                                        uint buffer_num = 0) noexcept {
        return byte_buffer_var(OC_FORWARD(index), desc, buffer_num).template soa_view_var<Elm>();
    }
};

#define OC_MAKE_STRUCT_MEMBER(m)                                                                                          \
    dsl_t<std::remove_cvref_t<decltype(this_type::m)>>(m){Function::current()->member(Type::of<decltype(this_type::m)>(), \
                                                                                      expression(),                       \
                                                                                      ocarina::struct_member_tuple<this_type>::member_index(#m))};

#define OC_MAKE_MEMBER_ASSIGNMENT(m) \
    m.set(_t_.m);

#define OC_MAKE_COMPUTABLE_BODY(S, ...)                   \
    namespace ocarina {                                   \
    namespace detail {                                    \
    template<>                                            \
    struct Ref<S> {                                       \
    public:                                               \
        using this_type = S;                              \
        static constexpr const char *cname =              \
            TypeDesc<S>::name().data();                   \
        OC_REF_COMMON(S)                                  \
    public:                                               \
        void                                              \
        set(const this_type &_t_) {                       \
            MAP(OC_MAKE_MEMBER_ASSIGNMENT, ##__VA_ARGS__) \
        }                                                 \
                                                          \
    public:                                               \
        MAP(OC_MAKE_STRUCT_MEMBER, ##__VA_ARGS__)         \
    };                                                    \
    }                                                     \
    }
}// namespace detail

template<typename T>
struct Proxy : public ocarina::detail::Ref<T> {
    static_assert(ocarina::always_false_v<T>, "proxy is invalid !");
};

#define OC_MAKE_GET_PROXY                                                                     \
    auto operator->() noexcept { return reinterpret_cast<ocarina::Proxy<org_type> *>(this); } \
    auto operator->() const noexcept { return reinterpret_cast<const ocarina::Proxy<org_type> *>(this); }

#define OC_MAKE_PROXY(S) \
    template<>           \
    struct ocarina::Proxy<S> : public ocarina::detail::Ref<S>

}// namespace ocarina