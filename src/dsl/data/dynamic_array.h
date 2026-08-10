//
// Created by Zero on 17/07/2022.
//

#pragma once

#include "core/stl.h"
#include "../core/ref.h"
#include "core/util/string_util.h"
#include "core/type_system/type_desc.h"
#include "../api/syntax.h"

namespace ocarina {

template<typename T>
class DynamicArray : public detail::EnableSubscriptAccess<DynamicArray<T>, T> {
public:
    using element_type = T;

private:
    uint size_{};
    const Expression *expression_{};

public:
    DynamicArray() = default;
    DynamicArray(DynamicArray &&) noexcept = default;

    explicit DynamicArray(const Expression *expression)
        : size_(expression->type()->dimension()), expression_(expression) {}

    explicit DynamicArray(size_t num, const Expression *expression = nullptr,
                          std::source_location location = std::source_location::current())
        : size_(num),
          expression_(expression == nullptr ? Function::current()->local(type(), location) : expression) {}

    template<typename U>
    requires is_dsl_v<U> || is_basic_v<U>
    explicit DynamicArray(size_t num, U &&u,
                          std::source_location location = std::source_location::current())
        : DynamicArray(num, nullptr, location) {
        for (int i = 0; i < num; ++i) {
            (*this)[i] = u;
        }
    }

    template<typename U>
    requires is_vector_v<U> && concepts::different<std::remove_cvref_t<U>, DynamicArray<T>>
    explicit DynamicArray(U &&vec,
                          std::source_location location = std::source_location::current()) noexcept
        : DynamicArray(vector_expr_dimension_v<U>, nullptr, location) {
        for (int i = 0; i < size(); ++i) {
            (*this)[i] = vec[i];
        }
    }

    explicit DynamicArray(const vector<T> &vec,
                          std::source_location location = std::source_location::current()) noexcept
        : DynamicArray(vec.size(), nullptr, location) {
        for (int i = 0; i < size(); ++i) {
            (*this)[i] = vec[i];
        }
    }

    DynamicArray(const DynamicArray &other,
                 std::source_location location = std::source_location::current()) noexcept
        : size_{other.size_}, expression_(Function::current()->local(type(), location)) {
        Function::current()->assign(expression_, other.expression_);
    }
    static DynamicArray<T> zero(uint size) noexcept { return DynamicArray<T>{size, static_cast<T>(0)}; }
    static DynamicArray<T> one(uint size) noexcept { return DynamicArray<T>{size, static_cast<T>(1)}; }
    [[nodiscard]] bool valid() const noexcept { return size_ > 0; }
    [[nodiscard]] static const Type *type(uint size) noexcept {
        return Type::from(ocarina::format("array<{},{}>",
                                          TypeDesc<T>::description(),
                                          size));
    }

    void reset(const DynamicArray<T> &array) noexcept {
        size_ = array.size();
        expression_ = array.expression();
    }

    void invalidate() noexcept {
        size_ = 0;
        expression_ = nullptr;
    }

    [[nodiscard]] const Type *type() const noexcept {
        return type(size_);
    }

    template<typename U>
    requires is_array_expr_v<U> || is_std_vector_v<U>
    static DynamicArray<T> create(U &&array) noexcept {
        DynamicArray<T> ret{array.size()};
        for (uint i = 0; i < ret.size(); ++i) {
            ret[i] = array[i];
        }
        return ret;
    }

    template<typename U, size_t N>
    static DynamicArray<T> from_vec(Vector<U, N> vec) noexcept {
        DynamicArray<T> ret{N};
        for (uint i = 0; i < N; ++i) {
            ret[i] = vec[i];
        }
        return ret;
    }

    template<typename U, size_t N>
    static DynamicArray<T> from_vec(const Var<Vector<U, N>> &vec) noexcept {
        DynamicArray<T> ret{N};
        for (uint i = 0; i < N; ++i) {
            ret[i] = vec[i];
        }
        return ret;
    }

    template<typename... Args>
    static DynamicArray<T> create(Args &&...args) noexcept {
        return create(ocarina::array<Var<T>, sizeof...(args)>{OC_FORWARD(args)...});
    }

    [[nodiscard]] Var<T> as_scalar() const noexcept {
        return (*this)[0];
    }

    template<size_t N>
    [[nodiscard]] auto as_vec() const noexcept {
        OC_ASSERT(N <= size_);
        if constexpr (N == 1) {
            return as_scalar();
        } else {
            Var<Vector<T, N>> ret;
            for (int i = 0; i < N; ++i) {
                ret[i] = (*this)[i];
            }
            return ret;
        }
    }

    [[nodiscard]] DynamicArray<T> sub(uint offset, uint num) const noexcept {
        DynamicArray<T> ret{num};
        for (int i = 0; i < num; ++i) {
            ret[i] = at(i + offset);
        }
        return ret;
    }

    [[nodiscard]] Var<Vector<T, 2>> as_vec2() const noexcept { return as_vec<2>(); }
    [[nodiscard]] Var<Vector<T, 3>> as_vec3() const noexcept { return as_vec<3>(); }
    [[nodiscard]] Var<Vector<T, 4>> as_vec4() const noexcept { return as_vec<4>(); }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    [[nodiscard]] decltype(auto) at(Index &&index) const noexcept {
        return (*this)[OC_FORWARD(index)];
    }

#include "../detail/swizzle/dynamic_array_swizzle.inl.h"

    DynamicArray &operator=(const DynamicArray &rhs) noexcept {
        OC_ASSERT(size_ == rhs.size_ || rhs.size_ == 1);
        if (&rhs == this) {
            return *this;
        }
        if (rhs.size_ == 1) {
            for (int i = 0; i < size(); ++i) {
                (*this)[i] = rhs[0];
            }
        } else {
            Function::current()->assign(expression_, rhs.expression_);
        }
        return *this;
    }

    void set(const vector<T> &rhs) {
        auto tmp = DynamicArray<T>::create(rhs);
        *this = tmp;
    }

    DynamicArray &operator=(const Var<T> &rhs) noexcept {
        for (int i = 0; i < size(); ++i) {
            (*this)[i] = rhs;
        }
        return *this;
    }

    DynamicArray &operator=(DynamicArray &&rhs) noexcept {
        *this = static_cast<const DynamicArray &>(rhs);
        return *this;
    }

    [[nodiscard]] const Expression *expression() const noexcept { return expression_; }
    [[nodiscard]] uint size() const noexcept { return size_; }

    template<typename F>
    [[nodiscard]] DynamicArray<T> map(F &&f) const noexcept {
        DynamicArray<T> s{size()};
        for (auto i = 0u; i < size(); i++) {
            if constexpr (std::invocable<F, Var<T>>) {
                s[i] = f((*this)[i]);
            } else {
                s[i] = f(i, (*this)[i]);
            }
        }
        return s;
    }

    template<typename I, typename F>
    [[nodiscard]] auto reduce(I &&initial, F &&f) const noexcept {
        auto r = eval(OC_FORWARD(initial));
        for (auto i = 0u; i < size(); i++) {
            if constexpr (std::invocable<F, Var<expr_value_t<decltype(r)>>, Float>) {
                r = f(r, (*this)[i]);
            } else {
                r = f(r, i, (*this)[i]);
            }
        }
        return r;
    }

    template<typename F>
    void foreach (F &&f) const noexcept {
        for (auto i = 0u; i < size(); i++) {
            if constexpr (std::invocable<F, Var<T>>) {
                f((*this)[i]);
            } else if constexpr (std::invocable<F, uint, Var<T>>) {
                f(i, (*this)[i]);
            } else {
                f(i);
            }
        }
    }

    [[nodiscard]] Var<T> sum() const noexcept {
        return reduce(0.f, [](auto r, auto x) noexcept { return r + x; });
    }

    /// implement in src/dsl/builtin.h
    [[nodiscard]] DynamicArray<T> clamp(const Var<T> &min_, const Var<T> &max_) const noexcept;
    /// implement in src/dsl/builtin.h
    [[nodiscard]] Var<T> max() const noexcept;
    /// implement in src/dsl/builtin.h
    [[nodiscard]] Var<T> min() const noexcept;
    /// implement in src/dsl/builtin.h
    void sanitize() noexcept;

    template<typename F>
    [[nodiscard]] Bool any(F &&f) const noexcept {
        return reduce(false, [&f](auto ans, auto value) noexcept { return ans || f(value); });
    }
    template<typename F>
    [[nodiscard]] Bool all(F &&f) const noexcept {
        return reduce(true, [&f](auto ans, auto value) noexcept { return ans && f(value); });
    }
    template<typename F>
    [[nodiscard]] Bool none(F &&f) const noexcept { return !any(OC_FORWARD(f)); }
};

#define OC_MAKE_ARRAY_DIM(type, dim) using type##dim##_array = DynamicArray<type##dim>;
#define OC_MAKE_ARRAY(type)    \
    OC_MAKE_ARRAY_DIM(type, )  \
    OC_MAKE_ARRAY_DIM(type, 2) \
    OC_MAKE_ARRAY_DIM(type, 3) \
    OC_MAKE_ARRAY_DIM(type, 4)

OC_MAKE_ARRAY(float)
OC_MAKE_ARRAY(uint)
OC_MAKE_ARRAY(int)

#undef OC_MAKE_ARRAY
#undef OC_MAKE_ARRAY_DIM

template<typename T>
OC_NODISCARD constexpr auto
lerp(const DynamicArray<T> &t, const DynamicArray<T> &a,
     const DynamicArray<T> &b) noexcept {
    return a + t * (b - a);
}

template<typename T>
class Container : public DynamicArray<T> {
private:
    Uint _count{0u};
    using Super = DynamicArray<T>;

public:
    using DynamicArray<T>::DynamicArray;
    void push_back(const Var<T> &t) noexcept {
        Super::operator[](_count) = t;
        _count += 1;
    }
    void pop() noexcept {
        _count -= 1;
    }
    [[nodiscard]] Var<T> top() const noexcept {
        return Super::operator[](_count - 1);
    }
    [[nodiscard]] Uint count() const noexcept {
        return _count;
    }
    void clear() noexcept {
        _count = 0;
    }
    template<typename F>
    void for_each(F &&f, Uint start = 0u, Int end = 0) const noexcept {
        Uint _end = _count + end;
        $for(i, start, _end) {
            if constexpr (std::invocable<F, Var<T>>) {
                f((*this)[i]);
            } else {
                f(i, (*this)[i]);
            }
        };
    }
};

namespace detail {

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] DynamicArray<T> expand_to_array(const T &t, uint size) noexcept {
    return DynamicArray<T>(size, t);
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] DynamicArray<T> expand_to_array(const Var<T> &t, uint size) noexcept {
    return DynamicArray<T>(size, t);
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] DynamicArray<T> expand_to_array(DynamicArray<T> arr, uint size) noexcept {
    OC_ASSERT(arr.size() == 1 || arr.size() == size);
    if (arr.size() == size) {
        return arr;
    }
    return DynamicArray<T>(size, arr[0]);
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] uint mix_size(const Var<T> &t) noexcept { return 1; }

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] uint mix_size(const T &t) noexcept { return 1; }

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] uint mix_size(const DynamicArray<T> &t) noexcept { return t.size(); }

template<typename T>
[[nodiscard]] inline DynamicArray<T> eval_dynamic_array(const DynamicArray<T> &array) noexcept {
    DynamicArray<T> ret{array.size()};
    ret = array;
    return ret;
}

template<typename T, size_t Dim>
template<typename U, typename V>
requires(is_all_floating_point_expr_v<U, V>)
DynamicArray<float> EnableTextureSample<T, Dim>::sample(uint channel_num, const U &u, const V &v)
    const noexcept {
    const T *texture = static_cast<const T *>(this);
    const CallExpr *expr = Function::current()->call_builtin(DynamicArray<float>::type(channel_num),
                                                             call_op,
                                                             {texture->expression(),
                                                              OC_EXPR(u),
                                                              OC_EXPR(v)},
                                                             {channel_num});
    texture->expression()->mark(Usage::READ);
    return eval_dynamic_array(DynamicArray<float>(channel_num, expr));
}

template<typename T, size_t Dim>
template<typename U, typename V, typename W>
requires(is_all_floating_point_expr_v<U, V, W>)
DynamicArray<float> EnableTextureSample<T, Dim>::sample(uint channel_num, const U &u, const V &v, const W &w)
    const noexcept {
    const T *texture = static_cast<const T *>(this);
    static_assert(Dim == 3,"This method only available on 3D tex");
    const CallExpr *expr = Function::current()->call_builtin(DynamicArray<float>::type(channel_num),
                                                             call_op,
                                                             {texture->expression(),
                                                              OC_EXPR(u),
                                                              OC_EXPR(v),
                                                              OC_EXPR(w)},
                                                             {channel_num});
    texture->expression()->mark(Usage::READ);
    return eval_dynamic_array(DynamicArray<float>(channel_num, expr));
}

template<typename T, size_t Dim>
template<typename UVW>
requires(is_general_float_vector3_v<remove_device_t<UVW>>)
DynamicArray<float> EnableTextureSample<T, Dim>::sample(uint channel_num, const UVW &uvw)
    const noexcept {
    return [&]<typename Arg>(const Arg &arg) {
        return sample(channel_num, arg.x, arg.y, arg.z);
    }(decay_swizzle(uvw));
}

template<typename T, size_t Dim>
template<typename UV>
requires(is_general_float_vector2_v<remove_device_t<UV>>)
DynamicArray<float> EnableTextureSample<T, Dim>::sample(uint channel_num, const UV &uv)
    const noexcept {
    return [&]<typename Arg>(const Arg &arg) {
        return sample(channel_num, arg.x, arg.y);
    }(decay_swizzle(uv));
}

}// namespace detail

template<typename T, typename Offset>
[[nodiscard]] DynamicArray<T> BindlessArrayByteBuffer::load_dynamic_array(uint array_size, Offset &&offset) const noexcept {
    if constexpr (is_dsl_v<Offset>) {
        offset = detail::correct_index(offset, this->size_in_byte(), typeid(*this).name(), traceback_string());
    }
    const CallExpr *expr = Function::current()->call_builtin(DynamicArray<T>::type(array_size),
                                                             CallOp::BINDLESS_ARRAY_BYTE_BUFFER_READ,
                                                             {bindless_array_, index_, OC_EXPR(offset)});
    return detail::eval_dynamic_array(DynamicArray<T>(array_size, expr));
}

template<size_t Dim>
template<typename U, typename V>
requires(is_all_floating_point_expr_v<U, V>)
DynamicArray<float> BindlessArrayTexture<Dim>::sample(uint channel_num, const U &u,
                                                      const V &v) const noexcept {
    const CallExpr *expr = Function::current()->call_builtin(DynamicArray<float>::type(channel_num),
                                                             call_op,
                                                             {bindless_array_, index_, OC_EXPR(u), OC_EXPR(v)},
                                                             {channel_num});
    return detail::eval_dynamic_array(DynamicArray<float>(channel_num, expr));
}

template<size_t Dim>
template<typename U, typename V, typename W>
requires(is_all_floating_point_expr_v<U, V, W>)
DynamicArray<float> BindlessArrayTexture<Dim>::sample(uint channel_num, const U &u,
                                                      const V &v, const W &w) const noexcept {
    static_assert(Dim == 3,"This method only available on 3D tex");
    const CallExpr *expr = Function::current()->call_builtin(DynamicArray<float>::type(channel_num),
                                                             call_op,
                                                             {bindless_array_, index_, OC_EXPR(u), OC_EXPR(v), OC_EXPR(w)},
                                                             {channel_num});
    return detail::eval_dynamic_array(DynamicArray<float>(channel_num, expr));
}

template<size_t Dim>
template<typename UVW>
requires(is_general_float_vector3_v<remove_device_t<UVW>>)
DynamicArray<float> BindlessArrayTexture<Dim>::sample(uint channel_num, const UVW &uvw)
    const noexcept {
    return [&]<typename Arg>(const Arg &arg) {
        return sample(channel_num, arg.x, arg.y, arg.z);
    }(decay_swizzle(uvw));
}

template<size_t Dim>
template<typename UV>
requires(is_general_float_vector2_v<remove_device_t<UV>>)
DynamicArray<float> BindlessArrayTexture<Dim>::sample(uint channel_num, const UV &uv)
    const noexcept {
    return [&]<typename Arg>(const Arg &arg) {
        return sample(channel_num, arg.x, arg.y);
    }(decay_swizzle(uv));
}

}// namespace ocarina