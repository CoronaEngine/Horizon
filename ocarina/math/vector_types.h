//
// Created by Zero on 2024/5/20.
//

#pragma once

#include "basic_traits.h"
#include "scalar_func.h"
#include "math/constants.h"
#include "half.h"
#include "real.h"

namespace ocarina {
namespace detail {

template<typename T>
struct valid_vector_impl : public std::disjunction<
                               std::is_same<T, bool>,
                               std::is_same<T, float>,
                               std::is_same<T, real>,
                               std::is_same<T, int>,
                               std::is_same<T, char>,
                               std::is_same<T, short>,
                               std::is_same<T, ushort>,
                               std::is_same<T, half>,
                               std::is_same<T, ulong>,
                               std::is_same<T, uchar>,
                               std::is_same<T, uint>> {};

};// namespace detail

template<typename T>
static constexpr auto valid_vector_v = detail::valid_vector_impl<std::remove_cvref_t<T>>::value;

}// namespace ocarina

namespace ocarina {

template<typename T, size_t N, size_t... Indices>
struct Swizzle {
    static constexpr uint num_component = sizeof...(Indices);
    static constexpr std::array index_array = {Indices...};
    static_assert(num_component <= 4 && std::max({Indices...}) < N);

    template<typename Scalar>
    struct vec {
        using type = Vector<Scalar, num_component>;
    };

    template<typename Scalar>
    struct vec<ocarina::Var<Scalar>> {
        using type = ocarina::Var<Vector<Scalar, num_component>>;
    };

    using vec_type = typename vec<T>::type;
    using scalar_type = T;
    using this_type = Swizzle<T, N, Indices...>;

private:
    template<size_t... index>
    void assign_to(vec_type &vec, std::index_sequence<index...>) const noexcept {
        ((vec[index] = data_[Indices]), ...);
    }

    template<typename U, size_t... index>
    void assign_from(const U &vec, std::index_sequence<index...>) noexcept {
        ((data_[Indices] = vec[index]), ...);
    }

public:
    ocarina::array<T, N> data_{};

public:
    Swizzle &operator=(const vec_type &vec) noexcept {
        assign_from(vec, std::make_index_sequence<num_component>());
        return *this;
    }

    template<typename U, size_t M, size_t... OtherIndices>
    Swizzle &operator=(const Swizzle<U, M, OtherIndices...> &other) {
        *this = other.decay();
        return *this;
    }

    [[nodiscard]] vec_type decay() const noexcept {
        vec_type ret;
        assign_to(ret, std::make_index_sequence<num_component>());
        return ret;
    }

    operator vec_type() const noexcept {
        return decay();
    }

    [[nodiscard]] vec_type decay() noexcept {
        vec_type ret;
        assign_to(ret, std::make_index_sequence<num_component>());
        return ret;
    }

    operator vec_type() noexcept {
        return decay();
    }

    [[nodiscard]] const T &at(uint i) const noexcept {
        return data_[index_array[i]];
    }

    [[nodiscard]] T &at(uint i) noexcept {
        return data_[index_array[i]];
    }

    [[nodiscard]] const T &operator[](uint i) const noexcept {
        return at(i);
    }

    [[nodiscard]] T &operator[](uint i) noexcept {
        return at(i);
    }

#define OC_MAKE_SWIZZLE_UNARY_OP(op)    \
    auto operator op() const noexcept { \
        return op decay();              \
    }
    OC_MAKE_SWIZZLE_UNARY_OP(+)
    OC_MAKE_SWIZZLE_UNARY_OP(-)
    OC_MAKE_SWIZZLE_UNARY_OP(!)
    OC_MAKE_SWIZZLE_UNARY_OP(~)

#undef OC_MAKE_SWIZZLE_UNARY_OP

#define OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(op)                                                 \
    template<typename U, size_t M, size_t... OtherIndices>                                   \
    requires requires { vec_type{} op typename Swizzle<U, M, OtherIndices...>::vec_type{}; } \
    auto operator op(Swizzle<U, M, OtherIndices...> rhs) const noexcept {                    \
        return decay() op rhs.decay();                                                       \
    }                                                                                        \
                                                                                             \
    template<typename U>                                                                     \
    requires is_scalar_v<remove_device_t<U>> && requires { vec_type{} op U{}; }              \
    auto operator op(U rhs) const noexcept {                                                 \
        return decay() op rhs;                                                               \
    }                                                                                        \
                                                                                             \
    template<typename U>                                                                     \
    requires is_scalar_v<U> && requires { U{} op vec_type{}; }                               \
    friend auto operator op(U lhs, Swizzle<T, N, Indices...> rhs) {                          \
        return lhs op rhs.decay();                                                           \
    }                                                                                        \
                                                                                             \
    template<typename Arg>                                                                   \
    requires(is_vector_v<vec_type, 0>)                                                       \
    Swizzle &operator op## = (Arg && arg) noexcept {                                         \
        auto tmp = *this;                                                                    \
        *this = tmp op OC_FORWARD(arg);                                                      \
        return *this;                                                                        \
    }

    OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(+)
    OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(-)
    OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(*)
    OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(/)
    OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(%)
    OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(>>)
    OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(<<)
    OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(|)
    OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(&)
    OC_MAKE_SWIZZLE_MEMBER_BINARY_OP(^)

#undef OC_MAKE_SWIZZLE_MEMBER_BINARY_OP

#define OC_MAKE_SWIZZLE_MEMBER_LOGIC_OP(op, ...)                                  \
    template<typename U, size_t M, size_t... OtherIndices>                        \
    requires __VA_ARGS__                                                          \
    [[nodiscard]] auto operator op(Swizzle<U, M, OtherIndices...> rhs) noexcept { \
        return decay() op rhs.decay();                                            \
    }                                                                             \
    template<typename U>                                                          \
    requires __VA_ARGS__                                                          \
    [[nodiscard]] auto operator op(U rhs) const noexcept {                        \
        return decay() op rhs;                                                    \
    }

    OC_MAKE_SWIZZLE_MEMBER_LOGIC_OP(||, ocarina::is_all_boolean_v<remove_device_t<T>, remove_device_t<U>>)
    OC_MAKE_SWIZZLE_MEMBER_LOGIC_OP(&&, ocarina::is_all_boolean_v<remove_device_t<T>, remove_device_t<U>>)
    OC_MAKE_SWIZZLE_MEMBER_LOGIC_OP(==, ocarina::is_all_number_v<remove_device_t<T>, remove_device_t<U>>)
    OC_MAKE_SWIZZLE_MEMBER_LOGIC_OP(!=, ocarina::is_all_number_v<remove_device_t<T>, remove_device_t<U>>)
    OC_MAKE_SWIZZLE_MEMBER_LOGIC_OP(<, ocarina::is_all_number_v<remove_device_t<T>, remove_device_t<U>>)
    OC_MAKE_SWIZZLE_MEMBER_LOGIC_OP(>, ocarina::is_all_number_v<remove_device_t<T>, remove_device_t<U>>)
    OC_MAKE_SWIZZLE_MEMBER_LOGIC_OP(<=, ocarina::is_all_number_v<remove_device_t<T>, remove_device_t<U>>)
    OC_MAKE_SWIZZLE_MEMBER_LOGIC_OP(>=, ocarina::is_all_number_v<remove_device_t<T>, remove_device_t<U>>)

#undef OC_MAKE_SWIZZLE_MEMBER_LOGIC_OP
};

}// namespace ocarina

namespace ocarina {

namespace detail {
template<typename T, size_t N>
struct VectorStorage {
    static_assert(valid_vector_v<T>, "Invalid vector type");

public:
    array<T, N> data_{};

public:
    VectorStorage() = default;
    explicit constexpr VectorStorage(T s) noexcept {
        data_.fill(s);
    }
    explicit constexpr VectorStorage(const T *ptr) noexcept {
        for (size_t i = 0; i < N; ++i) {
            data_[i] = ptr[i];
        }
    }
    template<typename... Args>
    requires(sizeof...(Args) == N)
    constexpr VectorStorage(Args... args) noexcept : data_{static_cast<T>(args)...} {}
    [[nodiscard]] constexpr T *data() noexcept { return data_.data(); }
    [[nodiscard]] constexpr const T *data() const noexcept { return data_.data(); }
};

template<typename T>
struct alignas(sizeof(T) * 2) VectorStorage<T, 2> {
    static_assert(valid_vector_v<T>, "Invalid vector type");

public:
    template<size_t... index>
    using swizzle_type = Swizzle<T, 2, index...>;

public:
    T x{}, y{};
    VectorStorage() : x{}, y{} {}
    explicit constexpr VectorStorage(T s) noexcept : x{s}, y{s} {}
    explicit constexpr VectorStorage(const T *ptr) noexcept : x{ptr[0]}, y{ptr[1]} {}
    constexpr VectorStorage(T x, T y) noexcept : x{x}, y{y} {}
    [[nodiscard]] constexpr T *data() noexcept { return &x; }
    [[nodiscard]] constexpr const T *data() const noexcept { return &x; }
#include "swizzle_inl/swizzle2.inl.h"
};

#define OC_VECTOR3_ALIGN_16 1

template<typename T>
struct alignas(sizeof(T) * 4) VectorStorage<T, 3> {
    static_assert(valid_vector_v<T>, "Invalid vector type");

public:
    template<size_t... index>
    using swizzle_type = Swizzle<T, 3, index...>;

public:
    T x{}, y{}, z{};
    VectorStorage() : x{}, y{}, z{} {}
    explicit constexpr VectorStorage(T s) noexcept : x{s}, y{s}, z{s} {}
    explicit constexpr VectorStorage(const T *ptr) noexcept : x{ptr[0]}, y{ptr[1]}, z{ptr[2]} {}
    constexpr VectorStorage(T x, T y, T z) noexcept : x{x}, y{y}, z{z} {}
    [[nodiscard]] constexpr T *data() noexcept { return &x; }
    [[nodiscard]] constexpr const T *data() const noexcept { return &x; }
#include "swizzle_inl/swizzle3.inl.h"
};

template<typename T>
struct alignas(sizeof(T) * 4) VectorStorage<T, 4> {
    static_assert(valid_vector_v<T>, "Invalid vector type");

public:
    template<size_t... index>
    using swizzle_type = Swizzle<T, 4, index...>;

public:
    T x{}, y{}, z{}, w{};
    VectorStorage() : x{}, y{}, z{}, w{} {}
    explicit constexpr VectorStorage(T s) noexcept : x{s}, y{s}, z{s}, w{s} {}
    explicit constexpr VectorStorage(const T *ptr) noexcept : x{ptr[0]}, y{ptr[1]}, z{ptr[2]}, w{ptr[3]} {}
    constexpr VectorStorage(T x, T y, T z, T w) noexcept : x{x}, y{y}, z{z}, w{w} {}
    [[nodiscard]] constexpr T *data() noexcept { return &x; }
    [[nodiscard]] constexpr const T *data() const noexcept { return &x; }
#include "swizzle_inl/swizzle4.inl.h"
};
}// namespace detail

struct MemberAccessor;

template<typename T, size_t N>
struct Vector : public detail::VectorStorage<T, N> {
    using detail::VectorStorage<T, N>::VectorStorage;
    using this_type = Vector<T, N>;
    using vec_type = this_type;
    using scalar_type = T;
    static constexpr size_t dimension = N;
    friend struct MemberAccessor;
    template<typename U>
    requires is_scalar_v<U>
    explicit constexpr Vector(U s) noexcept : Vector(static_cast<T>(s)) {}

    template<typename U, size_t NN>
    requires(NN > N)
    explicit constexpr Vector(Vector<U, NN> v)
        : Vector([&]<size_t... i>(std::index_sequence<i...>) {
              return Vector<T, N>(static_cast<T>(v[i])...);
          }(std::make_index_sequence<N>())) {}

    template<typename U>
    constexpr Vector(Vector<U, N> v)
        : Vector([&]<size_t... i>(std::index_sequence<i...>) {
              return Vector<T, N>(static_cast<T>(v[i])...);
          }(std::make_index_sequence<N>())) {}

    template<typename U>
    constexpr this_type &operator=(Vector<U, N> other) noexcept {
        [&]<size_t... i>(std::index_sequence<i...>) {
            ((this->operator[](i) = static_cast<T>(other[i])), ...);
        }(std::make_index_sequence<N>());
        return *this;
    }
    template<typename U, size_t NN, size_t... Indices>
    requires requires { this_type{} = typename Swizzle<U, NN, Indices...>::vec_type{}; }
    constexpr this_type &operator=(Swizzle<U, NN, Indices...> other) noexcept {
        return *this = other.decay();
    }
    [[nodiscard]] constexpr T &operator[](size_t index) noexcept { return this->data()[index]; }
    [[nodiscard]] constexpr const T &operator[](size_t index) const noexcept { return this->data()[index]; }
    [[nodiscard]] constexpr T &at(size_t index) noexcept { return this->data()[index]; }
    [[nodiscard]] constexpr const T &at(size_t index) const noexcept { return this->data()[index]; }

#define OC_MAKE_UNARY_OP(op)                                                  \
    [[nodiscard]] friend constexpr auto operator op(this_type val) noexcept { \
        using R = Vector<decltype(op T{}), N>;                                \
        return [&]<size_t... index>(std::index_sequence<index...>) {          \
            return R{op val.at(index)...};                                    \
        }(std::make_index_sequence<N>());                                     \
    }
    OC_MAKE_UNARY_OP(+)
    OC_MAKE_UNARY_OP(-)
    OC_MAKE_UNARY_OP(~)
    OC_MAKE_UNARY_OP(!)
#undef OC_MAKE_UNARY_OP

#define OC_MAKE_VECTOR_BINARY_OPERATOR(op, ...)                                                        \
    template<typename U>                                                                               \
    requires __VA_ARGS__                                                                               \
    [[nodiscard]] friend constexpr auto operator op(this_type lhs,                                     \
                                                    Vector<U, N> rhs) noexcept {                       \
        using ret_type = decltype(T {} op U{});                                                        \
        return [&]<size_t... index>(std::index_sequence<index...>) {                                   \
            return Vector<ret_type, N>{(lhs[index] op rhs[index])...};                                 \
        }(std::make_index_sequence<N>());                                                              \
    }                                                                                                  \
    template<typename U, size_t M, size_t... Indices>                                                  \
    requires __VA_ARGS__                                                                               \
    [[nodiscard]] friend constexpr auto operator op(Swizzle<U, M, Indices...> lhs,                     \
                                                    this_type rhs) noexcept {                          \
        return lhs.decay() op rhs;                                                                     \
    }                                                                                                  \
    template<typename U, size_t M, size_t... Indices>                                                  \
    requires __VA_ARGS__                                                                               \
    [[nodiscard]] friend constexpr auto operator op(this_type lhs,                                     \
                                                    Swizzle<U, M, Indices...> rhs) noexcept {          \
        return lhs op rhs.decay();                                                                     \
    }                                                                                                  \
    template<typename U>                                                                               \
    requires __VA_ARGS__                                                                               \
    [[nodiscard]] friend constexpr auto operator op(this_type lhs, U rhs) noexcept {                   \
        return lhs op Vector<U, N>{rhs};                                                               \
    }                                                                                                  \
    template<typename U>                                                                               \
    requires __VA_ARGS__                                                                               \
    [[nodiscard]] friend constexpr auto operator op(U lhs, Vector<T, N> rhs) noexcept {                \
        return Vector<U, N>{lhs} op rhs;                                                               \
    }                                                                                                  \
    template<typename U>                                                                               \
    requires __VA_ARGS__                                                                               \
    constexpr friend auto &operator op## = (this_type & lhs, Vector<U, N> rhs) noexcept {              \
        lhs = lhs op rhs;                                                                              \
        return lhs;                                                                                    \
    }                                                                                                  \
    template<typename U, size_t M, size_t... Indices>                                                  \
    requires __VA_ARGS__                                                                               \
    constexpr friend auto &operator op## = (this_type & lhs, Swizzle<U, M, Indices...> rhs) noexcept { \
        lhs = lhs op rhs;                                                                              \
        return lhs;                                                                                    \
    }                                                                                                  \
    template<typename U>                                                                               \
    requires __VA_ARGS__                                                                               \
    constexpr friend decltype(auto) operator op## = (this_type & lhs, U rhs) noexcept {                \
        return (lhs op## = Vector<U, N>{rhs});                                                         \
    }

    OC_MAKE_VECTOR_BINARY_OPERATOR(+, ocarina::is_all_number_v<T, U>)
    OC_MAKE_VECTOR_BINARY_OPERATOR(-, ocarina::is_all_number_v<T, U>)
    OC_MAKE_VECTOR_BINARY_OPERATOR(*, ocarina::is_all_number_v<T, U>)
    OC_MAKE_VECTOR_BINARY_OPERATOR(/, ocarina::is_all_number_v<T, U>)
    OC_MAKE_VECTOR_BINARY_OPERATOR(%, ocarina::is_all_number_v<T, U>)
    OC_MAKE_VECTOR_BINARY_OPERATOR(>>, ocarina::is_all_integral_v<T, U>)
    OC_MAKE_VECTOR_BINARY_OPERATOR(<<, ocarina::is_all_integral_v<T, U>)
    OC_MAKE_VECTOR_BINARY_OPERATOR(|, ocarina::is_all_integral_v<T, U>)
    OC_MAKE_VECTOR_BINARY_OPERATOR(&, ocarina::is_all_integral_v<T, U>)
    OC_MAKE_VECTOR_BINARY_OPERATOR(^, ocarina::is_all_integral_v<T, U>)

#undef OC_MAKE_VECTOR_BINARY_OPERATOR

#define OC_MAKE_VECTOR_LOGIC_OPERATOR(op, ...)                                                \
    template<typename U>                                                                      \
    requires __VA_ARGS__                                                                      \
    [[nodiscard]] friend constexpr auto operator op(this_type lhs,                            \
                                                    Vector<U, N> rhs) noexcept {              \
        return [&]<size_t... index>(std::index_sequence<index...>) {                          \
            return Vector<bool, N>{lhs[index] op rhs[index]...};                              \
        }(std::make_index_sequence<N>());                                                     \
    }                                                                                         \
    template<typename U, size_t M, size_t... Indices>                                         \
    requires __VA_ARGS__                                                                      \
    [[nodiscard]] friend constexpr auto operator op(Swizzle<U, M, Indices...> lhs,            \
                                                    this_type rhs) noexcept {                 \
        return lhs.decay() op rhs;                                                            \
    }                                                                                         \
    template<typename U, size_t M, size_t... Indices>                                         \
    requires __VA_ARGS__                                                                      \
    [[nodiscard]] friend constexpr auto operator op(this_type lhs,                            \
                                                    Swizzle<U, M, Indices...> rhs) noexcept { \
        return lhs op rhs.decay();                                                            \
    }                                                                                         \
    template<typename U>                                                                      \
    requires __VA_ARGS__                                                                      \
    [[nodiscard]] friend constexpr auto operator op(this_type lhs, U rhs) noexcept {          \
        return lhs op Vector<U, N>{rhs};                                                      \
    }                                                                                         \
    template<typename U>                                                                      \
    requires __VA_ARGS__                                                                      \
    [[nodiscard]] friend constexpr auto operator op(T lhs, Vector<U, N> rhs) noexcept {       \
        return this_type{lhs} op rhs;                                                         \
    }

    OC_MAKE_VECTOR_LOGIC_OPERATOR(||, ocarina::is_all_boolean_v<T, U>)
    OC_MAKE_VECTOR_LOGIC_OPERATOR(&&, ocarina::is_all_boolean_v<T, U>)
    OC_MAKE_VECTOR_LOGIC_OPERATOR(==, ocarina::is_all_scalar_v<T, U>)
    OC_MAKE_VECTOR_LOGIC_OPERATOR(!=, ocarina::is_all_scalar_v<T, U>)
    OC_MAKE_VECTOR_LOGIC_OPERATOR(<, ocarina::is_all_number_v<T, U>)
    OC_MAKE_VECTOR_LOGIC_OPERATOR(>, ocarina::is_all_number_v<T, U>)
    OC_MAKE_VECTOR_LOGIC_OPERATOR(<=, ocarina::is_all_number_v<T, U>)
    OC_MAKE_VECTOR_LOGIC_OPERATOR(>=, ocarina::is_all_number_v<T, U>)

#undef OC_MAKE_VECTOR_LOGIC_OPERATOR

#define OC_MAKE_VECTOR_UNARY_FUNC(func)                                                      \
    [[nodiscard]] static constexpr decltype(auto) call_##func(const this_type &v) noexcept { \
        using ret_type = Vector<decltype(func(T{})), this_type::dimension>;                  \
        return [&]<size_t... index>(std::index_sequence<index...>) {                         \
            return ret_type{func(v.at(index))...};                                           \
        }(std::make_index_sequence<N>());                                                    \
    }

private:
    OC_MAKE_VECTOR_UNARY_FUNC(rcp)
    OC_MAKE_VECTOR_UNARY_FUNC(abs)
    OC_MAKE_VECTOR_UNARY_FUNC(sqrt)
    OC_MAKE_VECTOR_UNARY_FUNC(sqr)
    OC_MAKE_VECTOR_UNARY_FUNC(sign)
    OC_MAKE_VECTOR_UNARY_FUNC(cos)
    OC_MAKE_VECTOR_UNARY_FUNC(sin)
    OC_MAKE_VECTOR_UNARY_FUNC(tan)
    OC_MAKE_VECTOR_UNARY_FUNC(cosh)
    OC_MAKE_VECTOR_UNARY_FUNC(sinh)
    OC_MAKE_VECTOR_UNARY_FUNC(tanh)
    OC_MAKE_VECTOR_UNARY_FUNC(log)
    OC_MAKE_VECTOR_UNARY_FUNC(log2)
    OC_MAKE_VECTOR_UNARY_FUNC(log10)
    OC_MAKE_VECTOR_UNARY_FUNC(exp)
    OC_MAKE_VECTOR_UNARY_FUNC(exp2)
    OC_MAKE_VECTOR_UNARY_FUNC(asin)
    OC_MAKE_VECTOR_UNARY_FUNC(acos)
    OC_MAKE_VECTOR_UNARY_FUNC(atan)
    OC_MAKE_VECTOR_UNARY_FUNC(asinh)
    OC_MAKE_VECTOR_UNARY_FUNC(acosh)
    OC_MAKE_VECTOR_UNARY_FUNC(atanh)
    OC_MAKE_VECTOR_UNARY_FUNC(floor)
    OC_MAKE_VECTOR_UNARY_FUNC(ceil)
    OC_MAKE_VECTOR_UNARY_FUNC(degrees)
    OC_MAKE_VECTOR_UNARY_FUNC(radians)
    OC_MAKE_VECTOR_UNARY_FUNC(round)
    OC_MAKE_VECTOR_UNARY_FUNC(isnan)
    OC_MAKE_VECTOR_UNARY_FUNC(isinf)
    OC_MAKE_VECTOR_UNARY_FUNC(fract)
    OC_MAKE_VECTOR_UNARY_FUNC(copysign)

    [[nodiscard]] static constexpr auto call_volume(this_type v) noexcept {
        return [&]<size_t... index>(std::index_sequence<index...>) {
            return ((v[index] * ...));
        }(std::make_index_sequence<N>());
    }

    [[nodiscard]] static constexpr auto call_length_squared(this_type v) noexcept {
        return call_dot(v, v);
    }

    [[nodiscard]] static constexpr auto call_length(this_type u) noexcept {
        return sqrt(call_length_squared(u));
    }

    [[nodiscard]] static constexpr auto call_normalize(this_type u) noexcept {
        return u * (1.0f / call_length(u));
    }

#undef OC_MAKE_VECTOR_UNARY_FUNC

#define OC_MAKE_VECTOR_BINARY_FUNC(func)                                                         \
    OC_NODISCARD static constexpr decltype(auto) call_##func(const this_type &v,                 \
                                                             const this_type &u) noexcept {      \
        using ret_type = Vector<std::remove_cvref_t<decltype(func(T{}, T{}))>,                   \
                                this_type::dimension>;                                           \
        return [&]<size_t... index>(std::index_sequence<index...>) {                             \
            return ret_type{func(v[index], u[index])...};                                        \
        }(std::make_index_sequence<N>());                                                        \
    }                                                                                            \
    OC_NODISCARD static constexpr decltype(auto) call_##func(const this_type &v, T u) noexcept { \
        return call_##func(v, this_type{u});                                                     \
    }                                                                                            \
    OC_NODISCARD static constexpr decltype(auto) call_##func(const T &v, this_type u) noexcept { \
        return call_##func(this_type{v}, u);                                                     \
    }
    OC_MAKE_VECTOR_BINARY_FUNC(max)
    OC_MAKE_VECTOR_BINARY_FUNC(min)
    OC_MAKE_VECTOR_BINARY_FUNC(pow)
    OC_MAKE_VECTOR_BINARY_FUNC(atan2)

    [[nodiscard]] static constexpr auto call_cross(this_type u, this_type v) noexcept {
        static_assert(N == 3);
        return this_type(u.y * v.z - v.y * u.z,
                         u.z * v.x - v.z * u.x,
                         u.x * v.y - v.x * u.y);
    }

    [[nodiscard]] static constexpr auto call_dot(this_type u, this_type v) noexcept {
        return [&]<size_t... index>(std::index_sequence<index...>) {
            return ((v[index] * u[index]) + ...);
        }(std::make_index_sequence<N>());
    }

    [[nodiscard]] static constexpr auto call_distance_squared(this_type u, this_type v) noexcept {
        return call_length_squared(u - v);
    }

    [[nodiscard]] static constexpr auto call_distance(this_type u, this_type v) noexcept {
        return call_length(u - v);
    }

#undef OC_MAKE_VECTOR_BINARY_FUNC

#define OC_MAKE_VECTOR_TRIPLE_FUNC(func)                                      \
    [[nodiscard]] static this_type call_##func(const this_type &t,            \
                                               const this_type &u,            \
                                               const this_type &v) noexcept { \
        return [&]<size_t... index>(std::index_sequence<index...>) {          \
            return this_type{func(t[index], u[index], v[index])...};          \
        }(std::make_index_sequence<N>());                                     \
    }

    OC_MAKE_VECTOR_TRIPLE_FUNC(fma)
    OC_MAKE_VECTOR_TRIPLE_FUNC(clamp)
    OC_MAKE_VECTOR_TRIPLE_FUNC(lerp)
    OC_MAKE_VECTOR_TRIPLE_FUNC(inverse_lerp)

#undef OC_MAKE_VECTOR_TRIPLE_FUNC

    [[nodiscard]] static this_type call_select(bool pred, this_type t, this_type f) {
        return [&]<size_t... index>(std::index_sequence<index...>) {
            return this_type{select(pred, t[index], f[index])...};
        }(std::make_index_sequence<N>());
    }

    [[nodiscard]] static this_type call_select(Vector<bool, N> pred, this_type t, this_type f) {
        return [&]<size_t... index>(std::index_sequence<index...>) {
            return this_type{select(pred[index], t[index], f[index])...};
        }(std::make_index_sequence<N>());
    }
};

struct MemberAccessor {
public:
#define OC_MAKE_ACCESSOR_FUNC(func)                                     \
    template<typename T, typename... Args>                              \
    requires requires {                                                 \
        T::call_##func(std::declval<Args>()...);                        \
    }                                                                   \
    [[nodiscard]] static decltype(auto) func(Args &&...args) noexcept { \
        return T::call_##func(OC_FORWARD(args)...);                     \
    }
    /// unary functions
    OC_MAKE_ACCESSOR_FUNC(all)
    OC_MAKE_ACCESSOR_FUNC(any)
    OC_MAKE_ACCESSOR_FUNC(none)

    OC_MAKE_ACCESSOR_FUNC(rcp)
    OC_MAKE_ACCESSOR_FUNC(abs)
    OC_MAKE_ACCESSOR_FUNC(sqrt)
    OC_MAKE_ACCESSOR_FUNC(sqr)
    OC_MAKE_ACCESSOR_FUNC(exp)
    OC_MAKE_ACCESSOR_FUNC(exp2)
    OC_MAKE_ACCESSOR_FUNC(exp10)
    OC_MAKE_ACCESSOR_FUNC(log)
    OC_MAKE_ACCESSOR_FUNC(log2)
    OC_MAKE_ACCESSOR_FUNC(log10)
    OC_MAKE_ACCESSOR_FUNC(cos)
    OC_MAKE_ACCESSOR_FUNC(sin)
    OC_MAKE_ACCESSOR_FUNC(tan)
    OC_MAKE_ACCESSOR_FUNC(cosh)
    OC_MAKE_ACCESSOR_FUNC(sinh)
    OC_MAKE_ACCESSOR_FUNC(tanh)
    OC_MAKE_ACCESSOR_FUNC(acos)
    OC_MAKE_ACCESSOR_FUNC(asin)
    OC_MAKE_ACCESSOR_FUNC(atan)
    OC_MAKE_ACCESSOR_FUNC(asinh)
    OC_MAKE_ACCESSOR_FUNC(acosh)
    OC_MAKE_ACCESSOR_FUNC(atanh)
    OC_MAKE_ACCESSOR_FUNC(degrees)
    OC_MAKE_ACCESSOR_FUNC(radians)
    OC_MAKE_ACCESSOR_FUNC(ceil)
    OC_MAKE_ACCESSOR_FUNC(round)
    OC_MAKE_ACCESSOR_FUNC(floor)
    OC_MAKE_ACCESSOR_FUNC(rsqrt)
    OC_MAKE_ACCESSOR_FUNC(isinf)
    OC_MAKE_ACCESSOR_FUNC(isnan)
    OC_MAKE_ACCESSOR_FUNC(fract)
    OC_MAKE_ACCESSOR_FUNC(saturate)
    OC_MAKE_ACCESSOR_FUNC(sign)
    OC_MAKE_ACCESSOR_FUNC(normalize)
    OC_MAKE_ACCESSOR_FUNC(length)
    OC_MAKE_ACCESSOR_FUNC(volume)
    OC_MAKE_ACCESSOR_FUNC(length_squared)

    OC_MAKE_ACCESSOR_FUNC(determinant)
    OC_MAKE_ACCESSOR_FUNC(transpose)
    OC_MAKE_ACCESSOR_FUNC(inverse)

    /// binary functions
    OC_MAKE_ACCESSOR_FUNC(max)
    OC_MAKE_ACCESSOR_FUNC(min)
    OC_MAKE_ACCESSOR_FUNC(pow)
    OC_MAKE_ACCESSOR_FUNC(fmod)
    OC_MAKE_ACCESSOR_FUNC(mod)
    OC_MAKE_ACCESSOR_FUNC(copysign)
    OC_MAKE_ACCESSOR_FUNC(atan2)

    OC_MAKE_ACCESSOR_FUNC(cross)
    OC_MAKE_ACCESSOR_FUNC(dot)
    OC_MAKE_ACCESSOR_FUNC(distance)
    OC_MAKE_ACCESSOR_FUNC(distance_squared)

    /// triple functions
    OC_MAKE_ACCESSOR_FUNC(clamp)
    OC_MAKE_ACCESSOR_FUNC(lerp)
    OC_MAKE_ACCESSOR_FUNC(inverse_lerp)
    OC_MAKE_ACCESSOR_FUNC(fma)
    OC_MAKE_ACCESSOR_FUNC(select)
    OC_MAKE_ACCESSOR_FUNC(face_forward)

#undef OC_MAKE_ACCESSOR_FUNC
};

#define OC_MAKE_VECTOR_UNARY_FUNC(func)                      \
    template<typename T>                                     \
    requires is_general_vector_v<T>                          \
    [[nodiscard]] decltype(auto) func(const T &v) noexcept { \
        return MemberAccessor::func<deduce_vec_t<T>>(v);     \
    }

OC_MAKE_VECTOR_UNARY_FUNC(rcp)
OC_MAKE_VECTOR_UNARY_FUNC(abs)
OC_MAKE_VECTOR_UNARY_FUNC(sqrt)
OC_MAKE_VECTOR_UNARY_FUNC(sqr)
OC_MAKE_VECTOR_UNARY_FUNC(sign)
OC_MAKE_VECTOR_UNARY_FUNC(cos)
OC_MAKE_VECTOR_UNARY_FUNC(sin)
OC_MAKE_VECTOR_UNARY_FUNC(tan)
OC_MAKE_VECTOR_UNARY_FUNC(cosh)
OC_MAKE_VECTOR_UNARY_FUNC(sinh)
OC_MAKE_VECTOR_UNARY_FUNC(tanh)
OC_MAKE_VECTOR_UNARY_FUNC(log)
OC_MAKE_VECTOR_UNARY_FUNC(log2)
OC_MAKE_VECTOR_UNARY_FUNC(log10)
OC_MAKE_VECTOR_UNARY_FUNC(exp)
OC_MAKE_VECTOR_UNARY_FUNC(exp2)
OC_MAKE_VECTOR_UNARY_FUNC(asin)
OC_MAKE_VECTOR_UNARY_FUNC(acos)
OC_MAKE_VECTOR_UNARY_FUNC(atan)
OC_MAKE_VECTOR_UNARY_FUNC(asinh)
OC_MAKE_VECTOR_UNARY_FUNC(acosh)
OC_MAKE_VECTOR_UNARY_FUNC(atanh)
OC_MAKE_VECTOR_UNARY_FUNC(floor)
OC_MAKE_VECTOR_UNARY_FUNC(ceil)
OC_MAKE_VECTOR_UNARY_FUNC(degrees)
OC_MAKE_VECTOR_UNARY_FUNC(radians)
OC_MAKE_VECTOR_UNARY_FUNC(round)
OC_MAKE_VECTOR_UNARY_FUNC(isnan)
OC_MAKE_VECTOR_UNARY_FUNC(isinf)
OC_MAKE_VECTOR_UNARY_FUNC(fract)
OC_MAKE_VECTOR_UNARY_FUNC(copysign)

OC_MAKE_VECTOR_UNARY_FUNC(volume)
OC_MAKE_VECTOR_UNARY_FUNC(length)
OC_MAKE_VECTOR_UNARY_FUNC(normalize)
OC_MAKE_VECTOR_UNARY_FUNC(length_squared)

#undef OC_MAKE_VECTOR_UNARY_FUNC

#define OC_MAKE_VECTOR_BINARY_FUNC(func)                                \
    template<typename T, typename U>                                    \
    requires is_all_general_basic_v<T, U>                               \
    OC_NODISCARD decltype(auto) func(const T &t, const U &u) noexcept { \
        using vec_type = deduce_binary_op_vec_t<T, U>;                  \
        return MemberAccessor::func<vec_type>(t, u);                    \
    }

OC_MAKE_VECTOR_BINARY_FUNC(pow)
OC_MAKE_VECTOR_BINARY_FUNC(min)
OC_MAKE_VECTOR_BINARY_FUNC(max)
OC_MAKE_VECTOR_BINARY_FUNC(atan2)
OC_MAKE_VECTOR_BINARY_FUNC(cross)

OC_MAKE_VECTOR_BINARY_FUNC(dot)
OC_MAKE_VECTOR_BINARY_FUNC(distance)
OC_MAKE_VECTOR_BINARY_FUNC(distance_squared)

template<typename Pred, typename T, typename F>
requires is_all_general_vector_v<T, F>
[[nodiscard]] decltype(auto) select(const Pred &pred, const T &t, const F &f) noexcept {
    using scalar_type = decltype(select(bool{}, type_element_t<T>{}, type_element_t<F>{}));
    return MemberAccessor::select<Vector<scalar_type, type_dimension_v<T>>>(pred, t, f);
}

#undef OC_MAKE_VECTOR_BINARY_FUNC

template<typename T, typename U, typename V>
requires(is_all_general_vector_v<T, U, V> &&
         type_dimension_v<T> == type_dimension_v<U> &&
         type_dimension_v<T> == type_dimension_v<V>)
[[nodiscard]] auto fma(const T &t, const U &u, const V &v) noexcept {
    return MemberAccessor::fma<deduce_vec_t<T>>(t, u, v);
}

#define OC_MAKE_VECTOR_TYPES(T) \
    using T##2 = Vector<T, 2>;  \
    using T##3 = Vector<T, 3>;  \
    using T##4 = Vector<T, 4>;

OC_MAKE_VECTOR_TYPES(bool)
OC_MAKE_VECTOR_TYPES(float)
OC_MAKE_VECTOR_TYPES(real)
OC_MAKE_VECTOR_TYPES(half)
OC_MAKE_VECTOR_TYPES(int)
OC_MAKE_VECTOR_TYPES(char)
OC_MAKE_VECTOR_TYPES(short)
OC_MAKE_VECTOR_TYPES(ushort)
OC_MAKE_VECTOR_TYPES(uchar)
OC_MAKE_VECTOR_TYPES(uint)
OC_MAKE_VECTOR_TYPES(ulong)

#undef OC_MAKE_VECTOR_TYPES

template<size_t N>
[[nodiscard]] constexpr bool any(Vector<bool, N> v) noexcept {
    return [&]<size_t... index>(std::index_sequence<index...>) {
        return (v[index] || ...);
    }(std::make_index_sequence<N>());
}
template<size_t N>
[[nodiscard]] constexpr bool all(Vector<bool, N> v) noexcept {
    return [&]<size_t... index>(std::index_sequence<index...>) {
        return (v[index] && ...);
    }(std::make_index_sequence<N>());
}
template<size_t N>
[[nodiscard]] constexpr bool none(Vector<bool, N> v) noexcept { return !any(v); }

#define OC_MAKE_SWIZZLE_LOGIC_FUNC(func)                                         \
    template<size_t N, size_t... Indices>                                        \
    [[nodiscard]] constexpr bool func(Swizzle<bool, N, Indices...> v) noexcept { \
        return func(v.decay());                                                  \
    }
OC_MAKE_SWIZZLE_LOGIC_FUNC(any)
OC_MAKE_SWIZZLE_LOGIC_FUNC(all)
OC_MAKE_SWIZZLE_LOGIC_FUNC(none)

#undef OC_MAKE_SWIZZLE_LOGIC_FUNC

#define OC_MAKE_TYPE_N(type)                                                                                                 \
    [[nodiscard]] constexpr auto make_##type##2(type s = {}) noexcept { return type##2(s); }                                 \
    [[nodiscard]] constexpr auto make_##type##2(type x, type y) noexcept { return type##2(x, y); }                           \
    template<typename T, size_t N>                                                                                           \
    requires(N >= 2)                                                                                                         \
    [[nodiscard]] constexpr auto make_##type##2(Vector<T, N> v) noexcept {                                                   \
        return type##2(                                                                                                      \
            static_cast<type>(v.x),                                                                                          \
            static_cast<type>(v.y));                                                                                         \
    }                                                                                                                        \
    template<typename T, size_t N, size_t... Indices>                                                                        \
    requires(sizeof...(Indices) >= 2)                                                                                        \
    [[nodiscard]] constexpr auto make_##type##2(ocarina::Swizzle<T, N, Indices...> v) noexcept {                             \
        return make_##type##2(v.decay());                                                                                    \
    }                                                                                                                        \
    template<typename T, size_t N>                                                                                           \
    requires(N >= 2)                                                                                                         \
    [[nodiscard]] constexpr auto make_##type##2(ocarina::array<T, N> v) noexcept {                                           \
        return type##2(                                                                                                      \
            static_cast<type>(v[0]),                                                                                         \
            static_cast<type>(v[1]));                                                                                        \
    }                                                                                                                        \
                                                                                                                             \
    [[nodiscard]] constexpr auto make_##type##3(type s = {}) noexcept { return type##3(s); }                                 \
    [[nodiscard]] constexpr auto make_##type##3(type x, type y, type z) noexcept { return type##3(x, y, z); }                \
    template<typename T, size_t N>                                                                                           \
    requires(N >= 3)                                                                                                         \
    [[nodiscard]] constexpr auto make_##type##3(Vector<T, N> v) noexcept {                                                   \
        return type##3(                                                                                                      \
            static_cast<type>(v.x),                                                                                          \
            static_cast<type>(v.y),                                                                                          \
            static_cast<type>(v.z));                                                                                         \
    }                                                                                                                        \
    template<typename T, size_t N, size_t... Indices>                                                                        \
    requires(sizeof...(Indices) >= 3)                                                                                        \
    [[nodiscard]] constexpr auto make_##type##3(ocarina::Swizzle<T, N, Indices...> v) noexcept {                             \
        return make_##type##3(v.decay());                                                                                    \
    }                                                                                                                        \
    template<typename T, size_t N>                                                                                           \
    requires(N >= 3)                                                                                                         \
    [[nodiscard]] constexpr auto make_##type##3(ocarina::array<T, N> v) noexcept {                                           \
        return type##3(                                                                                                      \
            static_cast<type>(v[0]),                                                                                         \
            static_cast<type>(v[1]),                                                                                         \
            static_cast<type>(v[2]));                                                                                        \
    }                                                                                                                        \
    [[nodiscard]] constexpr auto make_##type##3(type##2 v, type z) noexcept { return type##3(v.x, v.y, z); }                 \
    [[nodiscard]] constexpr auto make_##type##3(type x, type##2 v) noexcept { return type##3(x, v.x, v.y); }                 \
    [[nodiscard]] constexpr auto make_##type##3(type##4 v) noexcept { return type##3(v.x, v.y, v.z); }                       \
                                                                                                                             \
    [[nodiscard]] constexpr auto make_##type##4(type s = {}) noexcept { return type##4(s); }                                 \
    [[nodiscard]] constexpr auto make_##type##4(type x, type y, type z, type w) noexcept { return type##4(x, y, z, w); }     \
    template<typename T, size_t N>                                                                                           \
    requires(N == 4)                                                                                                         \
    [[nodiscard]] constexpr auto make_##type##4(Vector<T, N> v) noexcept {                                                   \
        return type##4(                                                                                                      \
            static_cast<type>(v.x),                                                                                          \
            static_cast<type>(v.y),                                                                                          \
            static_cast<type>(v.z),                                                                                          \
            static_cast<type>(v.w));                                                                                         \
    }                                                                                                                        \
    template<typename T, size_t N, size_t... Indices>                                                                        \
    requires(sizeof...(Indices) >= 4)                                                                                        \
    [[nodiscard]] constexpr auto make_##type##4(ocarina::Swizzle<T, N, Indices...> v) noexcept {                             \
        return make_##type##4(v.decay());                                                                                    \
    }                                                                                                                        \
    template<typename T, size_t N>                                                                                           \
    requires(N == 4)                                                                                                         \
    [[nodiscard]] constexpr auto make_##type##4(ocarina::array<T, N> v) noexcept {                                           \
        return type##4(                                                                                                      \
            static_cast<type>(v[0]),                                                                                         \
            static_cast<type>(v[1]),                                                                                         \
            static_cast<type>(v[2]),                                                                                         \
            static_cast<type>(v[3]));                                                                                        \
    }                                                                                                                        \
    [[nodiscard]] constexpr auto make_##type##4(type##2 v, type z, type w) noexcept { return type##4(v.x, v.y, z, w); }      \
    [[nodiscard]] constexpr auto make_##type##4(type x, type##2 v, type w) noexcept { return type##4(x, v.x, v.y, w); }      \
    [[nodiscard]] constexpr auto make_##type##4(type x, type y, type##2 v) noexcept { return type##4(x, y, v.x, v.y); }      \
    [[nodiscard]] constexpr auto make_##type##4(type##2 xy, type##2 zw) noexcept { return type##4(xy.x, xy.y, zw.x, zw.y); } \
    [[nodiscard]] constexpr auto make_##type##4(type##3 v, type w) noexcept { return type##4(v.x, v.y, v.z, w); }            \
    [[nodiscard]] constexpr auto make_##type##4(type x, type##3 v) noexcept { return type##4(x, v.x, v.y, v.z); }

OC_MAKE_TYPE_N(bool)
OC_MAKE_TYPE_N(float)
OC_MAKE_TYPE_N(real)
OC_MAKE_TYPE_N(half)
OC_MAKE_TYPE_N(int)
OC_MAKE_TYPE_N(uint)
OC_MAKE_TYPE_N(uchar)
OC_MAKE_TYPE_N(short)
OC_MAKE_TYPE_N(ushort)
OC_MAKE_TYPE_N(ulong)
OC_MAKE_TYPE_N(char)

#undef OC_MAKE_TYPE_N

}// namespace ocarina