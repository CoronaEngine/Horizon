#pragma once

#include "core/type.h"
#include "math/basic_types.h"

namespace horizon::core {
using horizon::math::Matrix;
using horizon::math::Vector;
using horizon::math::half;
using horizon::math::real;

template<size_t N>
struct is_core_bool_vector<horizon::math::Vector<bool, N>> : std::true_type {};

template<>
struct static_type_layout_traits<horizon::math::real> {
    using raw_type = horizon::math::real;
    using element_type = raw_type;
    static constexpr bool is_scalar = true;
    static constexpr bool is_vector = false;
    static constexpr bool is_matrix = false;
    static constexpr bool is_array = false;
    static constexpr bool is_structure = false;
    static constexpr size_t dimension = 1u;
    static constexpr size_t vector_storage_width = 0u;
};

template<>
struct static_type_layout_traits<horizon::math::half> : static_type_layout_traits<horizon::math::real> {
    using raw_type = horizon::math::half;
    using element_type = raw_type;
};

template<typename T, size_t N>
struct static_type_layout_traits<horizon::math::Vector<T, N>> {
    using raw_type = horizon::math::Vector<T, N>;
    using element_type = T;
    static constexpr bool is_scalar = false;
    static constexpr bool is_vector = true;
    static constexpr bool is_matrix = false;
    static constexpr bool is_array = false;
    static constexpr bool is_structure = false;
    static constexpr size_t dimension = N;
    static constexpr size_t vector_storage_width = N == 3u ? 4u : N;
};

template<typename T, size_t N, size_t M>
struct static_type_layout_traits<horizon::math::Matrix<T, N, M>> {
    using raw_type = horizon::math::Matrix<T, N, M>;
    using element_type = horizon::math::Vector<T, M>;
    static constexpr bool is_scalar = false;
    static constexpr bool is_vector = false;
    static constexpr bool is_matrix = true;
    static constexpr bool is_array = false;
    static constexpr bool is_structure = false;
    static constexpr size_t dimension = N;
    static constexpr size_t vector_storage_width = 0u;
};

template<>
struct scalar_storage_traits<horizon::math::real> {
    using raw_type = horizon::math::real;

    [[nodiscard]] static constexpr size_t size(StoragePrecisionPolicy policy) noexcept {
        return policy.policy == PrecisionPolicy::force_f32 ? sizeof(float) : sizeof(uint16_t);
    }

    [[nodiscard]] static constexpr size_t alignment(StoragePrecisionPolicy policy) noexcept {
        return policy.policy == PrecisionPolicy::force_f32 ? alignof(float) : alignof(uint16_t);
    }

    template<typename ByteBuffer>
    static void store(ByteBuffer &buffer,
                      size_t offset,
                      raw_type value,
                      StoragePrecisionPolicy policy) noexcept {
        if (policy.policy == PrecisionPolicy::force_f32) {
            buffer.template store<float>(offset, static_cast<float>(value));
        } else {
            buffer.template store<uint16_t>(offset, horizon::math::float_to_half(static_cast<float>(value)));
        }
    }

    template<typename ByteBuffer>
    [[nodiscard]] static raw_type load(const ByteBuffer &buffer,
                                       size_t offset,
                                       StoragePrecisionPolicy policy) noexcept {
        if (policy.policy == PrecisionPolicy::force_f32) {
            return raw_type{buffer.template load<float>(offset)};
        }
        return raw_type{horizon::math::half_to_float(buffer.template load<uint16_t>(offset))};
    }
};

template<>
struct scalar_storage_traits<horizon::math::half> {
    using raw_type = horizon::math::half;

    [[nodiscard]] static constexpr size_t size(StoragePrecisionPolicy) noexcept { return sizeof(uint16_t); }
    [[nodiscard]] static constexpr size_t alignment(StoragePrecisionPolicy) noexcept { return alignof(uint16_t); }

    template<typename ByteBuffer>
    static void store(ByteBuffer &buffer,
                      size_t offset,
                      raw_type value,
                      StoragePrecisionPolicy) noexcept {
        buffer.template store<uint16_t>(offset, value.bits());
    }

    template<typename ByteBuffer>
    [[nodiscard]] static raw_type load(const ByteBuffer &buffer,
                                       size_t offset,
                                       StoragePrecisionPolicy) noexcept {
        return raw_type{horizon::math::half_to_float(buffer.template load<uint16_t>(offset))};
    }
};

template<typename T, size_t N>
struct struct_member_tuple<Vector<T, N>> {
    using type = typename struct_member_tuple<horizon::core::array<T, N>>::type;
};

template<typename T, size_t N, size_t M>
struct struct_member_tuple<Matrix<T, N, M>> {
    using type = typename struct_member_tuple<horizon::core::array<Vector<T, M>, N>>::type;
};

// Pure type mapping from logical storage type T to the concrete storage tag F.
namespace detail {

template<typename T, typename F>
struct resolved_storage_map {
    using type = T;
};

template<typename F>
struct resolved_storage_map<real, F> {
    using type = F;
};

template<template<typename T, size_t N> typename Container, size_t N, typename F>
struct resolved_storage_map<Container<real, N>, F> {
    using type = Container<F, N>;
};

template<size_t N, size_t M, typename F>
struct resolved_storage_map<Matrix<real, N, M>, F> {
    using type = Matrix<F, N, M>;
};

template<typename T, typename F>
struct resolved_storage_impl;

template<typename T, typename F>
using resolved_storage_impl_t = typename resolved_storage_impl<std::remove_cvref_t<T>, F>::type;

template<typename T>
concept resolved_storage_supported_tag = std::same_as<T, float> || std::same_as<T, half>;

template<typename F, typename T>
[[nodiscard]] resolved_storage_impl_t<T, F> to_storage_impl_value(const T &value) noexcept;

template<typename T, typename F>
[[nodiscard]] std::remove_cvref_t<T> from_storage_impl_value(const resolved_storage_impl_t<T, F> &value) noexcept;

}// namespace detail

template<typename T, PrecisionPolicy Policy>
using resolved_storage_tag_t = std::conditional_t<Policy == PrecisionPolicy::force_f16, half, float>;

template<typename T, typename F>
using resolved_storage_by_tag_t = detail::resolved_storage_impl_t<T, F>;

template<typename T, PrecisionPolicy Policy>
struct resolved_storage;

template<typename T, PrecisionPolicy Policy>
using resolved_storage_t = typename resolved_storage<std::remove_cvref_t<T>, Policy>::type;

template<typename T, PrecisionPolicy Policy>
using storage_t = resolved_storage_t<T, Policy>;

template<PrecisionPolicy Policy, typename T>
[[nodiscard]] resolved_storage_t<T, Policy> to_storage_value(const T &value) noexcept;

template<typename T, PrecisionPolicy Policy>
[[nodiscard]] std::remove_cvref_t<T> from_storage_value(const resolved_storage_t<T, Policy> &value) noexcept;

/// Value-level implementation working directly with a concrete storage tag F.
namespace detail {

template<typename T, typename F>
struct resolved_storage_impl {
    using source_type = std::remove_cvref_t<T>;
    using type = typename resolved_storage_map<source_type, F>::type;

    [[nodiscard]] static type encode(const source_type &value) noexcept {
        return value;
    }

    [[nodiscard]] static source_type decode(const type &value) noexcept {
        return value;
    }
};

template<typename F>
struct resolved_storage_impl<real, F> {
    using source_type = real;
    using type = typename resolved_storage_map<source_type, F>::type;

    [[nodiscard]] static type encode(source_type value) noexcept {
        return static_cast<type>(static_cast<float>(value));
    }

    [[nodiscard]] static source_type decode(type value) noexcept {
        return source_type{static_cast<float>(value)};
    }
};

/// Shared encode/decode path for fixed-size element-addressable containers.
template<template<typename, size_t> typename Container, typename T, size_t N, typename F>
struct resolved_storage_fixed_container_impl {
    using source_type = Container<T, N>;
    using element_type = detail::resolved_storage_impl_t<T, F>;
    using type = Container<element_type, N>;

    [[nodiscard]] static type encode(const source_type &value) noexcept {
        type result{};
        for (size_t index = 0; index < N; ++index) {
            result[index] = detail::to_storage_impl_value<F>(value[index]);
        }
        return result;
    }

    [[nodiscard]] static source_type decode(const type &value) noexcept {
        source_type result{};
        for (size_t index = 0; index < N; ++index) {
            result[index] = detail::from_storage_impl_value<T, F>(value[index]);
        }
        return result;
    }
};

template<typename T, size_t N, typename F>
struct resolved_storage_impl<horizon::core::array<T, N>, F> : resolved_storage_fixed_container_impl<horizon::core::array, T, N, F> {};

template<typename T, size_t N, typename F>
struct resolved_storage_impl<Vector<T, N>, F> : resolved_storage_fixed_container_impl<Vector, T, N, F> {};

template<typename T, size_t N, size_t M, typename F>
struct resolved_storage_impl<Matrix<T, N, M>, F> {
    using source_type = Matrix<T, N, M>;
    using type = Matrix<detail::resolved_storage_impl_t<T, F>, N, M>;

    [[nodiscard]] static type encode(const source_type &value) noexcept {
        type result{};
        for (size_t index = 0; index < N; ++index) {
            result[index] = detail::to_storage_impl_value<F>(value[index]);
        }
        return result;
    }

    [[nodiscard]] static source_type decode(const type &value) noexcept {
        source_type result{};
        for (size_t index = 0; index < N; ++index) {
            result[index] = detail::from_storage_impl_value<Vector<T, M>, F>(value[index]);
        }
        return result;
    }
};

template<typename... T, typename F>
struct resolved_storage_impl<horizon::core::tuple<T...>, F> {
    using source_type = horizon::core::tuple<T...>;
    using type = horizon::core::tuple<detail::resolved_storage_impl_t<T, F>...>;

    [[nodiscard]] static type encode(const source_type &value) noexcept {
        return [&]<size_t... Index>(std::index_sequence<Index...>) {
            return type{detail::to_storage_impl_value<F>(horizon::core::get<Index>(value))...};
        }(std::make_index_sequence<sizeof...(T)>{});
    }

    [[nodiscard]] static source_type decode(const type &value) noexcept {
        return [&]<size_t... Index>(std::index_sequence<Index...>) {
            return source_type{detail::from_storage_impl_value<T, F>(horizon::core::get<Index>(value))...};
        }(std::make_index_sequence<sizeof...(T)>{});
    }
};

}// namespace detail

/// External policy wrapper that selects the concrete storage tag first,
/// then forwards to the internal F-based implementation layer.
template<typename T, PrecisionPolicy Policy>
struct resolved_storage : detail::resolved_storage_impl<std::remove_cvref_t<T>, resolved_storage_tag_t<T, Policy>> {};

namespace detail {

template<typename F, typename T>
[[nodiscard]] resolved_storage_impl_t<T, F> to_storage_impl_value(const T &value) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    return resolved_storage_impl<raw_t, F>::encode(value);
}

template<typename T, typename F>
[[nodiscard]] std::remove_cvref_t<T> from_storage_impl_value(const resolved_storage_impl_t<T, F> &value) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    return resolved_storage_impl<raw_t, F>::decode(value);
}

}// namespace detail

template<PrecisionPolicy Policy, typename T>
[[nodiscard]] resolved_storage_t<T, Policy> to_storage_value(const T &value) noexcept {
    return detail::to_storage_impl_value<resolved_storage_tag_t<T, Policy>>(value);
}

template<typename T, PrecisionPolicy Policy>
[[nodiscard]] std::remove_cvref_t<T> from_storage_value(const resolved_storage_t<T, Policy> &value) noexcept {
    return detail::from_storage_impl_value<T, resolved_storage_tag_t<T, Policy>>(value);
}

#define OC_STORAGE_MEMBER_TYPE(storage, member) horizon::core::resolved_storage_by_tag_t<std::remove_cvref_t<decltype(this_type::member)>, storage>
#define OC_STORAGE_MEMBER_DECL(member, storage) OC_STORAGE_MEMBER_TYPE(storage, member) member;
#define OC_STORAGE_MEMBER_ASSIGN_ENCODE(member, storage) result.member = horizon::core::detail::to_storage_impl_value<storage>(value.member);
#define OC_STORAGE_MEMBER_ASSIGN_DECODE(member, storage) result.member = horizon::core::detail::from_storage_impl_value<std::remove_cvref_t<decltype(this_type::member)>, storage>(value.member);

#define OC_MAKE_STORAGE_TYPE(S, ...)                                        \
    template<typename storage>                                              \
    requires horizon::core::detail::resolved_storage_supported_tag<storage>       \
    struct horizon::core::detail::resolved_storage_impl<S, storage> {             \
        using this_type = S;                                                \
        struct type {                                                       \
            using oc_storage_source_type = this_type;                       \
            using oc_storage_tag_type = storage;                            \
            MAP_UD(OC_STORAGE_MEMBER_DECL, storage, ##__VA_ARGS__)          \
            static constexpr PrecisionPolicy storage_policy() noexcept {    \
                if constexpr (std::same_as<storage, half>) {                \
                    return PrecisionPolicy::force_f16;                      \
                } else {                                                    \
                    return PrecisionPolicy::force_f32;                      \
                }                                                           \
            }                                                               \
            static constexpr string_view storage_suffix() noexcept {        \
                if constexpr (std::same_as<storage, half>) {                \
                    return "_storage_f16";                                  \
                } else {                                                    \
                    return "_storage_f32";                                  \
                }                                                           \
            }                                                               \
            static const horizon::core::string &storage_cname() noexcept {        \
                static thread_local horizon::core::string s = horizon::core::format(    \
                    "{}{}",                                                 \
                    Type::of<oc_storage_source_type>()->cname(),            \
                    storage_suffix());                                      \
                return s;                                                   \
            }                                                               \
            static horizon::core::string description() noexcept {                 \
                static thread_local horizon::core::string s = [] {                \
                    StoragePrecisionPolicy policy{                          \
                        .policy = storage_policy(),                         \
                        .allow_real_in_storage = false};                    \
                    const Type *resolved = Type::resolve(                   \
                        Type::of<oc_storage_source_type>(), policy);        \
                    OC_ASSERT(resolved != nullptr);                         \
                    horizon::core::string ret = horizon::core::format(                  \
                        "struct<{},{},{},{}",                               \
                        storage_cname(),                                    \
                        resolved->alignment(),                              \
                        resolved->is_builtin_struct(),                      \
                        resolved->is_param_struct());                       \
                    for (const Type *member : resolved->members()) {        \
                        ret.append(",").append(member->description());      \
                    }                                                       \
                    ret.push_back('>');                                     \
                    return ret;                                             \
                }();                                                        \
                return s;                                                   \
            }                                                               \
            static horizon::core::string_view name() noexcept {                   \
                return storage_cname();                                     \
            }                                                               \
        };                                                                  \
                                                                            \
        [[nodiscard]] static type encode(const this_type &value) noexcept { \
            type result{};                                                  \
            MAP_UD(OC_STORAGE_MEMBER_ASSIGN_ENCODE, storage, ##__VA_ARGS__) \
            return result;                                                  \
        }                                                                   \
                                                                            \
        [[nodiscard]] static this_type decode(const type &value) noexcept { \
            this_type result{};                                             \
            MAP_UD(OC_STORAGE_MEMBER_ASSIGN_DECODE, storage, ##__VA_ARGS__) \
            return result;                                                  \
        }                                                                   \
    };

namespace detail {

template<typename T, size_t N>
struct dimension_impl<Vector<T, N>> {
    static constexpr auto value = N;
};

template<typename T, size_t N, size_t M>
struct dimension_impl<Matrix<T, N, M>> {
    static constexpr auto value = N;
};

} // namespace detail

} // namespace horizon::core
