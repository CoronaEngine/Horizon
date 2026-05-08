//
// Created by Zero on 30/04/2022.
//

#pragma once

#include "core/header.h"
#include "core/util/hash.h"
#include "core/image/image_base.h"
#include "core/util/macro_map.h"
#include "core/type_system/precision_policy.h"
#include "core/stl.h"
#include "math/basic_types.h"

namespace ocarina {

template<typename T>
struct array_dimension {
    static constexpr size_t value = 0u;
};

template<typename T, size_t N>
struct array_dimension<T[N]> {
    static constexpr auto value = N;
};

template<typename T, size_t N>
struct array_dimension<ocarina::array<T, N>> {
    static constexpr auto value = N;
};

OC_DEFINE_TEMPLATE_VALUE(array_dimension)

template<typename T>
struct array_element {
    using type = T;
};

template<typename T, size_t N>
struct array_element<T[N]> {
    using type = T;
};

template<typename T, size_t N>
struct array_element<ocarina::array<T, N>> {
    using type = T;
};

template<typename T>
using array_element_t = typename array_element<T>::type;

template<typename T>
class is_array : public std::false_type {};

template<typename T, size_t N>
class is_array<T[N]> : public std::true_type {};

template<typename T, size_t N>
class is_array<ocarina::array<T, N>> : public std::true_type {};

template<typename T>
constexpr auto is_array_v = is_array<T>::value;

template<typename T>
struct is_tuple : std::false_type {};

template<typename... T>
struct is_tuple<ocarina::tuple<T...>> : std::true_type {};

template<typename T>
constexpr auto is_tuple_v = is_tuple<T>::value;

template<typename T>
struct is_struct : std::false_type {};

template<typename... T>
struct is_struct<ocarina::tuple<T...>> : std::true_type {};

template<typename T>
constexpr auto is_struct_v = is_struct<T>::value;

namespace detail {

template<typename T, size_t>
using array_to_tuple_element_t = T;

template<typename T, size_t N, size_t... i>
[[nodiscard]] constexpr auto array_to_tuple_impl(ocarina::array<T, N> array, std::index_sequence<i...>) noexcept {
    return ocarina::tuple<array_to_tuple_element_t<T, i>...>(array[i]...);
}

template<typename T, size_t N>
[[nodiscard]] constexpr auto array_to_tuple_impl(ocarina::array<T, N> array = {}) noexcept {
    return array_to_tuple_impl(array, std::make_index_sequence<N>());
}

template<typename T>
struct array_to_tuple {
    using type = T;
};

template<typename T, size_t N>
struct array_to_tuple<ocarina::array<T, N>> {
    using type = decltype(detail::array_to_tuple_impl<typename array_to_tuple<T>::type, N>());
};

template<typename... T>
struct array_to_tuple<ocarina::tuple<T...>> {
    using type = ocarina::tuple<T...>;
};

template<typename T>
struct is_builtin_struct_impl {
    static constexpr bool value = false;
};

template<typename T>
struct is_param_struct_impl {
    static constexpr bool value = false;
};

template<typename T, typename U>
requires is_integral_v<T> && is_integral_v<U>
[[nodiscard]] constexpr auto mem_offset(T offset, U alignment) noexcept {
    return (offset + alignment - 1u) / alignment * alignment;
}

template<typename S, typename Members, typename offsets>
struct is_valid_reflection : std::false_type {};

template<typename S, typename... M, typename I, I... os>
struct is_valid_reflection<S, ocarina::tuple<M...>, std::integer_sequence<I, os...>> {
    static_assert((!is_bool_vector_v<M> && ...),
                  "Boolean vectors are not allowed in DSL "
                  "structures since their may have different "
                  "layouts on different platforms.");

private:
    [[nodiscard]] static constexpr bool _check() noexcept {
        constexpr auto count = sizeof...(M);
        static_assert(sizeof...(os) == count);
        constexpr ocarina::array<size_t, count> sizes{sizeof(M)...};
        constexpr ocarina::array<size_t, count> alignments{alignof(M)...};
        constexpr ocarina::array<size_t, count> offsets{os...};
        size_t cur_offset = 0u;
        for (auto i = 0u; i < count; ++i) {
            auto offset = offsets[i];
            auto size = sizes[i];
            auto alignment = alignments[i];
            cur_offset = mem_offset(cur_offset, alignment);
            if (cur_offset != offset) {
                return false;
            }
            cur_offset += size;
        }
        constexpr auto struct_size = sizeof(S);
        constexpr auto struct_alignment = alignof(S);
        cur_offset = mem_offset(cur_offset, struct_alignment);
        return cur_offset == struct_size;
    };

public:
    static constexpr bool value = _check();
};

}// namespace detail

template<typename T>
using array_to_tuple_t = typename detail::array_to_tuple<T>::type;

template<typename T>
struct struct_member_tuple {
    using type = T;
};

template<typename... T>
struct struct_member_tuple<ocarina::tuple<T...>> {
    using type = ocarina::tuple<T...>;
};

template<typename T, size_t N>
struct struct_member_tuple<ocarina::array<T, N>> {
    using type = array_to_tuple_t<ocarina::array<T, N>>;
};

template<typename T, size_t N>
struct struct_member_tuple<T[N]> {
    using type = typename struct_member_tuple<ocarina::array<T, N>>::type;
};

template<typename T, size_t N>
struct struct_member_tuple<Vector<T, N>> {
    using type = typename struct_member_tuple<ocarina::array<T, N>>::type;
};

template<typename T, size_t N, size_t M>
struct struct_member_tuple<Matrix<T, N, M>> {
    using type = typename struct_member_tuple<ocarina::array<Vector<T, M>, N>>::type;
};

#define OC_MEMBER_TYPE_MAP(member) std::remove_cvref_t<decltype(this_type::member)>
#define OC_TYPE_OFFSET_OF(member) OC_OFFSET_OF(this_type, member)
#define OC_TYPE_SIZE(member) sizeof(this_type::member)

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
struct resolved_storage_impl<ocarina::array<T, N>, F> : resolved_storage_fixed_container_impl<ocarina::array, T, N, F> {};

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
struct resolved_storage_impl<ocarina::tuple<T...>, F> {
    using source_type = ocarina::tuple<T...>;
    using type = ocarina::tuple<detail::resolved_storage_impl_t<T, F>...>;

    [[nodiscard]] static type encode(const source_type &value) noexcept {
        return [&]<size_t... Index>(std::index_sequence<Index...>) {
            return type{detail::to_storage_impl_value<F>(ocarina::get<Index>(value))...};
        }(std::make_index_sequence<sizeof...(T)>{});
    }

    [[nodiscard]] static source_type decode(const type &value) noexcept {
        return [&]<size_t... Index>(std::index_sequence<Index...>) {
            return source_type{detail::from_storage_impl_value<T, F>(ocarina::get<Index>(value))...};
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

#define OC_STORAGE_MEMBER_TYPE(storage, member) ocarina::resolved_storage_by_tag_t<std::remove_cvref_t<decltype(this_type::member)>, storage>
#define OC_STORAGE_MEMBER_DECL(member, storage) OC_STORAGE_MEMBER_TYPE(storage, member) member;
#define OC_STORAGE_MEMBER_ASSIGN_ENCODE(member, storage) result.member = ocarina::detail::to_storage_impl_value<storage>(value.member);
#define OC_STORAGE_MEMBER_ASSIGN_DECODE(member, storage) result.member = ocarina::detail::from_storage_impl_value<std::remove_cvref_t<decltype(this_type::member)>, storage>(value.member);

#define OC_MAKE_STORAGE_TYPE(S, ...)                                        \
    template<typename storage>                                              \
    requires ocarina::detail::resolved_storage_supported_tag<storage>       \
    struct ocarina::detail::resolved_storage_impl<S, storage> {             \
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
            static const ocarina::string &storage_cname() noexcept {        \
                static thread_local ocarina::string s = ocarina::format(    \
                    "{}{}",                                                 \
                    Type::of<oc_storage_source_type>()->cname(),            \
                    storage_suffix());                                      \
                return s;                                                   \
            }                                                               \
            static ocarina::string description() noexcept {                 \
                static thread_local ocarina::string s = [] {                \
                    StoragePrecisionPolicy policy{                          \
                        .policy = storage_policy(),                         \
                        .allow_real_in_storage = false};                    \
                    const Type *resolved = Type::resolve(                   \
                        Type::of<oc_storage_source_type>(), policy);        \
                    OC_ASSERT(resolved != nullptr);                         \
                    ocarina::string ret = ocarina::format(                  \
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
            static ocarina::string_view name() noexcept {                   \
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

#define OC_MAKE_STRUCT_REFLECTION(S, ...)                                                         \
    template<>                                                                                    \
    struct ocarina::is_struct<S> : std::true_type {};                                             \
    template<>                                                                                    \
    struct ocarina::struct_member_tuple<S> {                                                      \
        using this_type = S;                                                                      \
        static constexpr string_view struct_name = #S;                                            \
        static constexpr string_view members[] = {MAP_LIST(OC_STRINGIFY, __VA_ARGS__)};           \
        using type = ocarina::tuple<MAP_LIST(OC_MEMBER_TYPE_MAP, ##__VA_ARGS__)>;                 \
        using offset = std::index_sequence<MAP_LIST(OC_TYPE_OFFSET_OF, ##__VA_ARGS__)>;           \
        static constexpr array offset_array = {MAP_LIST(OC_TYPE_OFFSET_OF, ##__VA_ARGS__)};       \
        static constexpr auto min_size = std::min({MAP_LIST(OC_TYPE_SIZE, ##__VA_ARGS__)});       \
        static_assert(min_size >= 4 || ocarina::is_builtin_struct_v<S>,                           \
                      "Due to the memory alignment, min member size must >= 4");                  \
        static_assert(ocarina::is_valid_reflection_v<this_type, type, offset>,                    \
                      "may be order of members is wrong!");                                       \
        static_assert(sizeof(this_type) >= 4);                                                    \
        static constexpr auto member_index(ocarina::string_view name) {                           \
            return std::find(std::begin(members), std::end(members), name) - std::begin(members); \
        }                                                                                         \
    };

template<typename T>
using struct_member_tuple_t = typename struct_member_tuple<T>::type;

template<typename T>
struct canonical_layout {
    using type = struct_member_tuple_t<T>;
};

template<typename... T>
struct canonical_layout<ocarina::tuple<T...>> {
    using type = ocarina::tuple<typename canonical_layout<T>::type...>;
};

template<typename T>
using canonical_layout_t = typename canonical_layout<T>::type;

template<typename... T>
struct tuple_join {
    static_assert(always_false_v<T...>);
};

template<typename... T, typename... U>
struct tuple_join<ocarina::tuple<T...>, U...> {
    using type = ocarina::tuple<T..., U...>;
};

template<typename... A, typename... B, typename... C>
struct tuple_join<ocarina::tuple<A...>, ocarina::tuple<B...>, C...> {
    using type = typename tuple_join<ocarina::tuple<A..., B...>, C...>::type;
};

template<typename... T>
using tuple_join_t = typename tuple_join<T...>::type;

namespace detail {

template<typename A, typename B>
struct linear_layout_impl {
    using type = ocarina::tuple<B>;
};

template<typename... A, typename... B>
struct linear_layout_impl<ocarina::tuple<A...>, ocarina::tuple<B...>> {
    using type = tuple_join_t<ocarina::tuple<A...>,
                              typename linear_layout_impl<ocarina::tuple<>, B>::type...>;
};

template<typename T>
struct dimension_impl {
    static constexpr auto value = dimension_impl<canonical_layout_t<T>>::value;
};

template<typename T, size_t N>
struct dimension_impl<T[N]> {
    static constexpr auto value = N;
};

template<typename T, size_t N>
struct dimension_impl<ocarina::array<T, N>> {
    static constexpr auto value = N;
};

template<typename T, size_t N>
struct dimension_impl<Vector<T, N>> {
    static constexpr auto value = N;
};

template<typename T, size_t N, size_t M>
struct dimension_impl<Matrix<T, N, M>> {
    static constexpr auto value = N;
};

template<typename... T>
struct dimension_impl<ocarina::tuple<T...>> {
    static constexpr auto value = sizeof...(T);
};

}// namespace detail

template<typename T>
using linear_layout = detail::linear_layout_impl<ocarina::tuple<>, canonical_layout_t<T>>;

template<typename T>
using linear_layout_t = typename linear_layout<T>::type;

template<typename T>
using dimension = detail::dimension_impl<std::remove_cvref_t<T>>;

template<typename T>
constexpr auto dimension_v = dimension<T>::value;

template<typename T>
struct is_builtin_struct : public detail::is_builtin_struct_impl<std::remove_cvref_t<T>> {};
OC_DEFINE_TEMPLATE_VALUE(is_builtin_struct)

template<typename T, typename Func>
void for_each_struct_member_type(Func &&func) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    traverse_tuple(struct_member_tuple_t<raw_t>{}, std::forward<Func>(func));
}

template<typename Member, typename T>
[[nodiscard]] decltype(auto) struct_member_at(T &value, size_t index) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    using value_t = std::remove_reference_t<T>;
    using qualified_member_t = std::conditional_t<std::is_const_v<value_t>, const Member, Member>;
    constexpr auto offset_array = struct_member_tuple<raw_t>::offset_array;
    using byte_ptr_t = std::conditional_t<std::is_const_v<value_t>, const std::byte *, std::byte *>;
    auto *head = reinterpret_cast<byte_ptr_t>(addressof(value));
    auto *addr = head + offset_array[index];
    return *std::launder(reinterpret_cast<qualified_member_t *>(addr));
}

template<typename T, typename Func>
void for_each_struct_member(T &&value, Func &&func) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    for_each_struct_member_type<raw_t>([&]<typename Member>(const Member &, size_t index) {
        decltype(auto) member = struct_member_at<Member>(value, index);
        if constexpr (std::invocable<Func, decltype(member), size_t>) {
            func(member, index);
        } else {
            func(member);
        }
    });
}

#define OC_MAKE_BUILTIN_STRUCT(S)                       \
    template<>                                          \
    struct ocarina::detail::is_builtin_struct_impl<S> { \
        static constexpr bool value = true;             \
    };

template<typename T>
struct is_param_struct : public detail::is_param_struct_impl<std::remove_cvref_t<T>> {};
OC_DEFINE_TEMPLATE_VALUE(is_param_struct)

#define OC_MAKE_PARAM_STRUCT(S)                       \
    template<>                                        \
    struct ocarina::detail::is_param_struct_impl<S> { \
        static constexpr bool value = true;           \
    };

template<typename S, typename M, typename I>
static constexpr bool is_valid_reflection_v = detail::is_valid_reflection<S, M, I>::value;

class Type;

struct BindlessArrayDesc {
    handle_ty buffer_slot;
    handle_ty tex3d_slot;
    handle_ty tex2d_slot;
};

struct TextureDesc {
    handle_ty texture{};
    handle_ty surface{};
    PixelStorage pixel_storage{};
};

template<typename T = std::byte>
struct BufferDesc {
    T *handle{};
    uint offset{};
    uint64_t size{};

    [[nodiscard]] size_t data_alignment() const noexcept {
        return alignof(decltype(*this));
    }

    [[nodiscard]] size_t data_size() const noexcept {
        return sizeof(*this);
    }

    [[nodiscard]] MemoryBlock memory_block() const noexcept {
        return {this, data_size(), data_alignment(), sizeof(handle_ty)};
    }

    [[nodiscard]] handle_ty head() const noexcept {
        return reinterpret_cast<handle_ty>(handle);
    }

    [[nodiscard]] uint64_t size_in_byte() const noexcept {
        return size * sizeof(T);
    }

    [[nodiscard]] uint offset_in_byte() const noexcept {
        return offset * sizeof(T);
    }
};

namespace detail {
template<typename T>
struct is_buffer_proxy_impl {
    static constexpr bool value = false;
};

template<typename T>
struct is_buffer_proxy_impl<BufferDesc<T>> {
    static constexpr bool value = true;
};
}// namespace detail

template<typename T>
struct is_buffer_proxy : public detail::is_buffer_proxy_impl<std::remove_cvref_t<T>> {};
OC_DEFINE_TEMPLATE_VALUE(is_buffer_proxy);

using ByteBufferDesc = BufferDesc<>;

struct TypeVisitor {
    virtual void visit(const Type *) noexcept = 0;
};

namespace detail {

struct TypeParser;

struct TypeSystemCallbacks {
    void (*on_type_access)(const Type *) noexcept {nullptr};
};

OC_CORE_API void register_type_system_callbacks(TypeSystemCallbacks callbacks) noexcept;

}// namespace detail

class Type final : public concepts::Noncopyable, public Hashable {
public:
    enum struct Tag : uint32_t {
        BOOL,
        FLOAT,
        REAL,
        HALF,
        INT,
        UINT,
        UCHAR,
        CHAR,
        SHORT,
        USHORT,
        ULONG,

        VECTOR,
        MATRIX,

        ARRAY,
        STRUCTURE,

        BUFFER,
        BYTE_BUFFER,
        TEXTURE3D,
        TEXTURE2D,
        BINDLESS_ARRAY,
        ACCEL,

        NONE
    };
    friend struct detail::TypeParser;

private:
    size_t size_{0};
    size_t index_{0};
    size_t alignment_{0};
    uint32_t dimension_{0};
    Tag tag_{Tag::NONE};
    ocarina::string description_;
    ocarina::string name_;
    mutable ocarina::string cname_;
    mutable ocarina::vector<string_view> member_name_;
    ocarina::vector<const Type *> members_;
    [[nodiscard]] uint64_t compute_hash() const noexcept override;
    bool builtin_struct_{false};
    bool param_struct_{false};

private:
    void update_name(ocarina::string_view desc) noexcept;
    void set_description(ocarina::string_view desc) noexcept;
    void update_member_name(const string_view *names, int num) const noexcept;

public:
    Type() = default;
    static void for_each(TypeVisitor *visitor);
    template<typename T>
    [[nodiscard]] static const Type *of() noexcept;

    template<typename T>
    [[nodiscard]] static auto of(T &&) noexcept { return of<std::remove_cvref_t<T>>(); }
    [[nodiscard]] static const Type *resolve(const Type *type,
                                             StoragePrecisionPolicy policy) noexcept;
    [[nodiscard]] static const Type *resolve(const Type *type) noexcept {
        return Type::resolve(type, global_storage_policy());
    }
    [[nodiscard]] const Type *resolve(StoragePrecisionPolicy policy) const noexcept {
        return Type::resolve(this, policy);
    }
    [[nodiscard]] const Type *resolve() const noexcept {
        return Type::resolve(this);
    }
    template<typename T>
    [[nodiscard]] static const Type *resolve(StoragePrecisionPolicy policy) noexcept {
        return Type::resolve(of<T>(), policy);
    }
    template<typename T>
    [[nodiscard]] static const Type *resolve() noexcept {
        return Type::resolve(of<T>());
    }
    [[nodiscard]] static const Type *from(ocarina::string_view description) noexcept;
    [[nodiscard]] bool is_dynamic() const noexcept;
    [[nodiscard]] static const Type *at(uint32_t uid) noexcept;
    [[nodiscard]] static size_t count() noexcept;
    [[nodiscard]] static bool exists(ocarina::string_view description) noexcept;
    [[nodiscard]] static bool exists(uint64_t hash) noexcept;
    [[nodiscard]] const Type *get_member(ocarina::string_view name) const noexcept;
    [[nodiscard]] ocarina::span<const string_view> member_name() const noexcept { return member_name_; }
    [[nodiscard]] bool operator==(const Type &rhs) const noexcept { return hash() == rhs.hash(); }
    [[nodiscard]] bool operator!=(const Type &rhs) const noexcept { return !(*this == rhs); }
    [[nodiscard]] bool operator<(const Type &rhs) const noexcept { return index_ < rhs.index_; }
    [[nodiscard]] constexpr size_t index() const noexcept { return index_; }
    [[nodiscard]] constexpr size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr size_t alignment() const noexcept { return alignment_; }
    [[nodiscard]] constexpr Tag tag() const noexcept { return tag_; }
    [[nodiscard]] auto description() const noexcept { return ocarina::string_view{description_}; }
    [[nodiscard]] ocarina::string name() const noexcept { return name_; }
    [[nodiscard]] ocarina::string cname() const noexcept { return cname_; }
    void set_cname(string s) const noexcept;
    [[nodiscard]] ocarina::string simple_cname() const noexcept;
    [[nodiscard]] constexpr uint dimension() const noexcept { return dimension_; }
    [[nodiscard]] ocarina::span<const Type *const> members() const noexcept;
    [[nodiscard]] const Type *element() const noexcept;
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] constexpr bool is_scalar() const noexcept {
        return tag_ == Tag::BOOL || tag_ == Tag::FLOAT || tag_ == Tag::REAL || tag_ == Tag::INT ||
               tag_ == Tag::UINT || tag_ == Tag::UCHAR || tag_ == Tag::CHAR ||
               tag_ == Tag::USHORT || tag_ == Tag::SHORT || tag_ == Tag::HALF || tag_ == Tag::ULONG;
    }
    [[nodiscard]] size_t max_member_size() const noexcept;
    [[nodiscard]] constexpr bool is_builtin_struct() const noexcept { return builtin_struct_; }
    [[nodiscard]] constexpr bool is_param_struct() const noexcept { return param_struct_; }
    [[nodiscard]] constexpr bool is_basic() const noexcept { return is_scalar() || is_vector() || is_matrix(); }
    [[nodiscard]] constexpr bool is_array() const noexcept { return tag_ == Tag::ARRAY; }
    [[nodiscard]] constexpr bool is_vector() const noexcept { return tag_ == Tag::VECTOR; }
    [[nodiscard]] constexpr bool is_matrix() const noexcept { return tag_ == Tag::MATRIX; }
    [[nodiscard]] constexpr bool is_structure() const noexcept { return tag_ == Tag::STRUCTURE; }
    [[nodiscard]] constexpr bool is_buffer() const noexcept { return tag_ == Tag::BUFFER; }
    [[nodiscard]] constexpr bool is_byte_buffer() const noexcept { return tag_ == Tag::BYTE_BUFFER; }
    [[nodiscard]] constexpr bool is_texture() const noexcept { return tag_ == Tag::TEXTURE3D || tag_ == Tag::TEXTURE2D; }
    [[nodiscard]] constexpr bool is_bindless_array() const noexcept { return tag_ == Tag::BINDLESS_ARRAY; }
    [[nodiscard]] constexpr bool is_accel() const noexcept { return tag_ == Tag::ACCEL; }
    [[nodiscard]] constexpr bool is_resource() const noexcept {
        return is_buffer() || is_byte_buffer() || is_texture() || is_accel() || is_bindless_array();
    }
};

}// namespace ocarina