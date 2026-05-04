//
// Created by Z on 12/04/2026.
//

#pragma once

#include "core/dynamic_buffer/dynamic_buffer_layout_common.h"
#include "core/dynamic_buffer/host_byte_buffer.h"
#include "core/type_system/precision_policy.h"
#include "core/type.h"
#include "math/half.h"
#include "math/real.h"

namespace ocarina {

enum class DynamicBufferLayout : uint8_t {
    AOS,
    SOA,
};

namespace detail {

[[nodiscard]] inline bool store_real_as_f32(StoragePrecisionPolicy policy,
                                            bool direct_array_element = false) noexcept {
    (void)direct_array_element;
    return policy.policy == PrecisionPolicy::force_f32;
}

template<typename T>
[[nodiscard]] size_t resolved_size(StoragePrecisionPolicy policy) noexcept;

template<typename T>
[[nodiscard]] size_t resolved_alignment(StoragePrecisionPolicy policy) noexcept;

template<typename T>
[[nodiscard]] size_t resolved_size_in_context(StoragePrecisionPolicy policy,
                                              bool direct_array_element) noexcept;

template<typename T>
[[nodiscard]] size_t resolved_alignment_in_context(StoragePrecisionPolicy policy,
                                                   bool direct_array_element) noexcept;

template<typename T, typename Func, size_t... Indices>
void for_each_array_element_impl(Func &&func,
                                 std::index_sequence<Indices...>) noexcept {
    (func(std::integral_constant<size_t, Indices>{}), ...);
}

template<typename T, typename Func>
void for_each_array_element(Func &&func) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    constexpr size_t element_count = array_dimension_v<raw_t>;
    for_each_array_element_impl<raw_t>(std::forward<Func>(func),
                                       std::make_index_sequence<element_count>{});
}

template<typename T>
[[nodiscard]] size_t resolved_size(StoragePrecisionPolicy policy) noexcept {
    return compile_time_resolved_layout_size<T>(policy);
}

template<typename T>
[[nodiscard]] size_t resolved_alignment(StoragePrecisionPolicy policy) noexcept {
    return compile_time_resolved_layout_alignment<T>(policy);
}

template<typename T>
[[nodiscard]] size_t resolved_size_in_context(StoragePrecisionPolicy policy,
                                              bool direct_array_element) noexcept {
    (void)direct_array_element;
    return resolved_size<T>(policy);
}

template<typename T>
[[nodiscard]] size_t resolved_alignment_in_context(StoragePrecisionPolicy policy,
                                                   bool direct_array_element) noexcept {
    (void)direct_array_element;
    return resolved_alignment<T>(policy);
}

template<size_t I, typename T>
[[nodiscard]] decltype(auto) member_ref(T &value) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    if constexpr (is_array_v<raw_t> || is_vector_v<raw_t> || is_matrix_v<raw_t>) {
        return (value[I]);
    } else {
        using member_t = tuple_element_t<I, struct_member_tuple_t<raw_t>>;
        constexpr size_t offset = struct_member_tuple<raw_t>::offset_array[I];
        auto *ptr = reinterpret_cast<std::byte *>(addressof(value)) + offset;
        return *std::launder(reinterpret_cast<member_t *>(ptr));
    }
}

template<size_t I, typename T>
[[nodiscard]] decltype(auto) member_ref(const T &value) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    if constexpr (is_array_v<raw_t> || is_vector_v<raw_t> || is_matrix_v<raw_t>) {
        return (value[I]);
    } else {
        using member_t = tuple_element_t<I, struct_member_tuple_t<raw_t>>;
        constexpr size_t offset = struct_member_tuple<raw_t>::offset_array[I];
        auto *ptr = reinterpret_cast<const std::byte *>(addressof(value)) + offset;
        return *std::launder(reinterpret_cast<const member_t *>(ptr));
    }
}

template<typename T>
constexpr bool is_soa_atomic_v = is_scalar_v<T> || is_vector_v<T>;

template<typename Member, typename T>
[[nodiscard]] decltype(auto) runtime_member_ref(T &value, size_t index) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    if constexpr (is_array_v<raw_t> || is_vector_v<raw_t> || is_matrix_v<raw_t>) {
        return value[index];
    } else {
        return struct_member_at<Member>(value, index);
    }
}

template<typename Member, typename Getter>
struct ConstStructMemberGetter {
    Getter *getter{nullptr};
    size_t member_index{0u};

    [[nodiscard]] decltype(auto) operator()(size_t record_index) const noexcept {
        return runtime_member_ref<Member>((*getter)(record_index), member_index);
    }
};

template<typename Member, typename Getter>
struct StructMemberGetter {
    Getter *getter{nullptr};
    size_t member_index{0u};

    [[nodiscard]] decltype(auto) operator()(size_t record_index) const noexcept {
        return runtime_member_ref<Member>((*getter)(record_index), member_index);
    }
};

template<typename Member, typename Getter>
[[nodiscard]] auto make_const_struct_member_getter(Getter &getter, size_t member_index) noexcept {
    return ConstStructMemberGetter<Member, Getter>{addressof(getter), member_index};
}

template<typename Member, typename Getter>
[[nodiscard]] auto make_struct_member_getter(Getter &getter, size_t member_index) noexcept {
    return StructMemberGetter<Member, Getter>{addressof(getter), member_index};
}

template<typename T>
void encode_aos_value(HostByteBuffer &dst,
                      size_t offset,
                      const T &value,
                      StoragePrecisionPolicy policy,
                      bool direct_array_element = false) noexcept;

template<typename T>
[[nodiscard]] T decode_aos_value(const HostByteBuffer &src,
                                 size_t offset,
                                 StoragePrecisionPolicy policy,
                                 bool direct_array_element = false) noexcept;

template<typename T, typename Getter>
[[nodiscard]] size_t encode_soa_from_getter(size_t count,
                                            HostByteBuffer &dst,
                                            size_t offset,
                                            StoragePrecisionPolicy policy,
                                            Getter &&getter,
                                            bool direct_array_element = false) noexcept;

template<typename T, typename Getter>
[[nodiscard]] size_t decode_soa_to_getter(size_t count,
                                          const HostByteBuffer &src,
                                          size_t offset,
                                          StoragePrecisionPolicy policy,
                                          Getter &&getter,
                                          bool direct_array_element = false) noexcept;

template<typename T>
[[nodiscard]] size_t soa_storage_bytes(size_t count,
                                       StoragePrecisionPolicy policy,
                                       bool direct_array_element = false) noexcept;

template<typename T>
void zero_bytes(HostByteBuffer &dst, size_t offset, size_t size) noexcept {
    auto out = dst.subspan(offset, size);
    std::fill(out.begin(), out.end(), std::byte{0});
}

template<typename T>
void encode_aos_struct_members(HostByteBuffer &dst,
                               size_t offset,
                               const T &value,
                               StoragePrecisionPolicy policy) noexcept {
    size_t current = offset;
    for_each_struct_member(value, [&](const auto &member, size_t) {
        using member_t = std::remove_cvref_t<decltype(member)>;
        current = mem_offset(current, resolved_alignment<member_t>(policy));
        encode_aos_value<member_t>(dst, current, member, policy, false);
        current += resolved_size<member_t>(policy);
    });
}

template<typename T>
[[nodiscard]] T decode_aos_struct_members(const HostByteBuffer &src,
                                          size_t offset,
                                          StoragePrecisionPolicy policy) noexcept {
    T value{};
    size_t current = offset;
    for_each_struct_member(value, [&](auto &member, size_t) {
        using member_t = std::remove_cvref_t<decltype(member)>;
        current = mem_offset(current, resolved_alignment<member_t>(policy));
        member = decode_aos_value<member_t>(src, current, policy, false);
        current += resolved_size<member_t>(policy);
    });
    return value;
}

template<typename T>
void encode_scalar(HostByteBuffer &dst,
                   size_t offset,
                   const T &value,
                   StoragePrecisionPolicy policy,
                   bool direct_array_element) noexcept {
    (void)direct_array_element;
    if constexpr (is_real_v<T>) {
        if (policy.policy == PrecisionPolicy::force_f32) {
            dst.store<float>(offset, static_cast<float>(value));
        } else {
            dst.store<uint16_t>(offset, float_to_half(static_cast<float>(value)));
        }
    } else if constexpr (is_half_v<T>) {
        dst.store<uint16_t>(offset, value.bits());
    } else {
        dst.store<T>(offset, value);
    }
}

template<typename T>
[[nodiscard]] T decode_scalar(const HostByteBuffer &src,
                              size_t offset,
                              StoragePrecisionPolicy policy,
                              bool direct_array_element) noexcept {
    (void)direct_array_element;
    if constexpr (is_real_v<T>) {
        if (policy.policy == PrecisionPolicy::force_f32) {
            return real{src.load<float>(offset)};
        }
        return real{half_to_float(src.load<uint16_t>(offset))};
    } else if constexpr (is_half_v<T>) {
        return half{half_to_float(src.load<uint16_t>(offset))};
    } else {
        return src.load<T>(offset);
    }
}

template<typename T>
void encode_aos_value(HostByteBuffer &dst,
                      size_t offset,
                      const T &value,
                      StoragePrecisionPolicy policy,
                      bool direct_array_element) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    if constexpr (is_scalar_v<raw_t>) {
        encode_scalar(dst, offset, value, policy, direct_array_element);
    } else if constexpr (is_vector_v<raw_t>) {
        using element_t = type_element_t<raw_t>;
        constexpr size_t dim = vector_dimension_v<raw_t>;
        size_t total_size = resolved_size<raw_t>(policy);
        size_t element_size = resolved_size<element_t>(policy);
        zero_bytes<raw_t>(dst, offset, total_size);
        for (size_t index = 0; index < dim; ++index) {
            encode_scalar<element_t>(dst, offset + index * element_size, value[index], policy, false);
        }
    } else if constexpr (is_matrix_v<raw_t>) {
        constexpr size_t dim = matrix_dimension_v<raw_t>;
        using column_t = tuple_element_t<0, struct_member_tuple_t<raw_t>>;
        size_t column_size = resolved_size<column_t>(policy);
        for (size_t index = 0; index < dim; ++index) {
            encode_aos_value<column_t>(dst, offset + index * column_size, value[index], policy, false);
        }
    } else if constexpr (is_array_v<raw_t>) {
        using element_t = array_element_t<raw_t>;
        constexpr size_t dim = array_dimension_v<raw_t>;
        size_t element_size = resolved_size_in_context<element_t>(policy, true);
        for (size_t index = 0; index < dim; ++index) {
            encode_aos_value<element_t>(dst, offset + index * element_size, value[index], policy, true);
        }
    } else if constexpr (is_struct_v<raw_t>) {
        zero_bytes<raw_t>(dst, offset, resolved_size<raw_t>(policy));
        encode_aos_struct_members(dst, offset, value, policy);
    } else {
        static_assert(always_false_v<raw_t>, "Unsupported type for DynamicBufferLayoutCodec::encode_aos_value");
    }
}

template<typename T>
[[nodiscard]] T decode_aos_value(const HostByteBuffer &src,
                                 size_t offset,
                                 StoragePrecisionPolicy policy,
                                 bool direct_array_element) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    if constexpr (is_scalar_v<raw_t>) {
        return decode_scalar<raw_t>(src, offset, policy, direct_array_element);
    } else if constexpr (is_vector_v<raw_t>) {
        using element_t = type_element_t<raw_t>;
        constexpr size_t dim = vector_dimension_v<raw_t>;
        size_t element_size = resolved_size<element_t>(policy);
        raw_t value{};
        for (size_t index = 0; index < dim; ++index) {
            value[index] = decode_scalar<element_t>(src, offset + index * element_size, policy, false);
        }
        return value;
    } else if constexpr (is_matrix_v<raw_t>) {
        constexpr size_t dim = matrix_dimension_v<raw_t>;
        using column_t = tuple_element_t<0, struct_member_tuple_t<raw_t>>;
        size_t column_size = resolved_size<column_t>(policy);
        raw_t value{};
        for (size_t index = 0; index < dim; ++index) {
            value[index] = decode_aos_value<column_t>(src, offset + index * column_size, policy, false);
        }
        return value;
    } else if constexpr (is_array_v<raw_t>) {
        using element_t = array_element_t<raw_t>;
        constexpr size_t dim = array_dimension_v<raw_t>;
        size_t element_size = resolved_size_in_context<element_t>(policy, true);
        raw_t value{};
        for (size_t index = 0; index < dim; ++index) {
            value[index] = decode_aos_value<element_t>(src, offset + index * element_size, policy, true);
        }
        return value;
    } else if constexpr (is_struct_v<raw_t>) {
        return decode_aos_struct_members<raw_t>(src, offset, policy);
    } else {
        static_assert(always_false_v<raw_t>, "Unsupported type for DynamicBufferLayoutCodec::decode_aos_value");
    }
}

template<typename T, typename Getter>
[[nodiscard]] size_t encode_soa_struct_like(size_t count,
                                            HostByteBuffer &dst,
                                            size_t offset,
                                            StoragePrecisionPolicy policy,
                                            Getter &&getter) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    auto &getter_ref = getter;
    size_t current = offset;
    for_each_struct_member_type<raw_t>([&](auto member_tag, size_t member_index) {
        using member_t = std::remove_cvref_t<decltype(member_tag)>;
        current += encode_soa_from_getter<member_t>(
            count, dst, current, policy, make_const_struct_member_getter<member_t>(getter_ref, member_index));
    });
    return current - offset;
}

template<typename T, typename Getter>
[[nodiscard]] size_t encode_soa_array_like(size_t count,
                                           HostByteBuffer &dst,
                                           size_t offset,
                                           StoragePrecisionPolicy policy,
                                           Getter &&getter) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    auto &getter_ref = getter;
    size_t current = offset;
    for_each_array_element<raw_t>([&]<size_t Index>(std::integral_constant<size_t, Index>) {
        current += encode_soa_from_getter<array_element_t<raw_t>>(
            count, dst, current, policy,
            make_const_struct_member_getter<array_element_t<raw_t>>(getter_ref, Index),
            true);
    });
    return current - offset;
}

template<typename T, typename Getter>
[[nodiscard]] size_t decode_soa_struct_like(size_t count,
                                            const HostByteBuffer &src,
                                            size_t offset,
                                            StoragePrecisionPolicy policy,
                                            Getter &&getter) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    auto &getter_ref = getter;
    size_t current = offset;
    for_each_struct_member_type<raw_t>([&](auto member_tag, size_t member_index) {
        using member_t = std::remove_cvref_t<decltype(member_tag)>;
        current += decode_soa_to_getter<member_t>(
            count, src, current, policy, make_struct_member_getter<member_t>(getter_ref, member_index));
    });
    return current - offset;
}

template<typename T, typename Getter>
[[nodiscard]] size_t decode_soa_array_like(size_t count,
                                           const HostByteBuffer &src,
                                           size_t offset,
                                           StoragePrecisionPolicy policy,
                                           Getter &&getter) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    auto &getter_ref = getter;
    size_t current = offset;
    for_each_array_element<raw_t>([&]<size_t Index>(std::integral_constant<size_t, Index>) {
        current += decode_soa_to_getter<array_element_t<raw_t>>(
            count, src, current, policy,
            make_struct_member_getter<array_element_t<raw_t>>(getter_ref, Index),
            true);
    });
    return current - offset;
}

template<typename T>
[[nodiscard]] size_t soa_struct_like_storage_bytes(size_t count,
                                                   StoragePrecisionPolicy policy) noexcept {
    return compile_time_soa_storage_bytes<T>(count, policy);
}

template<typename T, typename Getter>
[[nodiscard]] size_t encode_soa_from_getter(size_t count,
                                            HostByteBuffer &dst,
                                            size_t offset,
                                            StoragePrecisionPolicy policy,
                                            Getter &&getter,
                                            bool direct_array_element) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    if constexpr (is_soa_atomic_v<raw_t>) {
        size_t stride = resolved_size_in_context<raw_t>(policy, direct_array_element);
        for (size_t index = 0; index < count; ++index) {
            encode_aos_value<raw_t>(dst, offset + index * stride, getter(index), policy, direct_array_element);
        }
        return count * stride;
    } else if constexpr (is_matrix_v<raw_t>) {
        return encode_soa_struct_like<raw_t>(count, dst, offset, policy, std::forward<Getter>(getter));
    } else if constexpr (is_array_v<raw_t>) {
        return encode_soa_array_like<raw_t>(count, dst, offset, policy, std::forward<Getter>(getter));
    } else if constexpr (is_struct_v<raw_t>) {
        return encode_soa_struct_like<raw_t>(count, dst, offset, policy, std::forward<Getter>(getter));
    } else {
        static_assert(always_false_v<raw_t>, "Unsupported type for DynamicBufferLayoutCodec::encode_soa_from_getter");
    }
}

template<typename T, typename Getter>
[[nodiscard]] size_t decode_soa_to_getter(size_t count,
                                          const HostByteBuffer &src,
                                          size_t offset,
                                          StoragePrecisionPolicy policy,
                                          Getter &&getter,
                                          bool direct_array_element) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    if constexpr (is_soa_atomic_v<raw_t>) {
        size_t stride = resolved_size_in_context<raw_t>(policy, direct_array_element);
        for (size_t index = 0; index < count; ++index) {
            getter(index) = decode_aos_value<raw_t>(src, offset + index * stride, policy, direct_array_element);
        }
        return count * stride;
    } else if constexpr (is_matrix_v<raw_t>) {
        return decode_soa_struct_like<raw_t>(count, src, offset, policy, std::forward<Getter>(getter));
    } else if constexpr (is_array_v<raw_t>) {
        return decode_soa_array_like<raw_t>(count, src, offset, policy, std::forward<Getter>(getter));
    } else if constexpr (is_struct_v<raw_t>) {
        return decode_soa_struct_like<raw_t>(count, src, offset, policy, std::forward<Getter>(getter));
    } else {
        static_assert(always_false_v<raw_t>, "Unsupported type for DynamicBufferLayoutCodec::decode_soa_to_getter");
    }
}

template<typename T>
[[nodiscard]] size_t soa_storage_bytes(size_t count,
                                       StoragePrecisionPolicy policy,
                                       bool direct_array_element) noexcept {
    (void)direct_array_element;
    return compile_time_soa_storage_bytes<T>(count, policy);
}

}// namespace detail

template<typename T>
struct DynamicBufferLayoutCodec {
    [[nodiscard]] static size_t storage_bytes(size_t count,
                                              StoragePrecisionPolicy policy,
                                              DynamicBufferLayout layout) noexcept {
        switch (layout) {
            case DynamicBufferLayout::AOS:
                return count * detail::resolved_size<T>(policy);
            case DynamicBufferLayout::SOA:
                return detail::soa_storage_bytes<T>(count, policy);
            default:
                return 0u;
        }
    }

    static void encode(const T *src,
                       size_t count,
                       HostByteBuffer &dst,
                       StoragePrecisionPolicy policy,
                       DynamicBufferLayout layout) noexcept {
        OC_ASSERT(src != nullptr || count == 0u);
        dst.resize(storage_bytes(count, policy, layout));
        if (count == 0u) {
            return;
        }
        switch (layout) {
            case DynamicBufferLayout::AOS: {
                size_t stride = detail::resolved_size<T>(policy);
                for (size_t index = 0; index < count; ++index) {
                    detail::encode_aos_value<T>(dst, index * stride, src[index], policy, false);
                }
                break;
            }
            case DynamicBufferLayout::SOA:
                (void)detail::encode_soa_from_getter<T>(count, dst, 0u, policy,
                                                        [src](size_t index) -> const T & { return src[index]; });
                break;
            default:
                break;
        }
    }

    static void decode(const HostByteBuffer &src,
                       size_t count,
                       T *dst,
                       StoragePrecisionPolicy policy,
                       DynamicBufferLayout layout) noexcept {
        OC_ASSERT(dst != nullptr || count == 0u);
        OC_ASSERT(src.size() >= storage_bytes(count, policy, layout));
        if (count == 0u) {
            return;
        }
        switch (layout) {
            case DynamicBufferLayout::AOS: {
                size_t stride = detail::resolved_size<T>(policy);
                for (size_t index = 0; index < count; ++index) {
                    dst[index] = detail::decode_aos_value<T>(src, index * stride, policy, false);
                }
                break;
            }
            case DynamicBufferLayout::SOA:
                (void)detail::decode_soa_to_getter<T>(count, src, 0u, policy,
                                                      [dst](size_t index) -> T & { return dst[index]; });
                break;
            default:
                break;
        }
    }
};

}// namespace ocarina