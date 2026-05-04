//
// Created by Z on 2026/4/13.
//

#pragma once

#include "core/type_system/precision_policy.h"
#include "core/type.h"
#include "math/half.h"
#include "math/real.h"

namespace ocarina::detail {

template<typename T>
struct StaticTypeKey {
    using type = T;
};

struct RuntimeTypeLayoutAdapter;
struct CompileTimeTypeLayoutAdapter;

template<typename Adapter, typename T>
[[nodiscard]] size_t recursive_resolved_alignment(StaticTypeKey<T> node,
                                                  StoragePrecisionPolicy policy) noexcept;

template<typename Adapter, typename T>
[[nodiscard]] size_t recursive_resolved_size(StaticTypeKey<T> node,
                                             StoragePrecisionPolicy policy) noexcept;

template<typename Adapter, typename T>
[[nodiscard]] size_t recursive_soa_storage_bytes(StaticTypeKey<T> node,
                                                 size_t count,
                                                 StoragePrecisionPolicy policy) noexcept;

template<typename Adapter>
[[nodiscard]] size_t recursive_resolved_alignment(const Type *node,
                                                  StoragePrecisionPolicy policy) noexcept;

template<typename Adapter>
[[nodiscard]] size_t recursive_resolved_size(const Type *node,
                                             StoragePrecisionPolicy policy) noexcept;

template<typename Adapter>
[[nodiscard]] size_t recursive_soa_storage_bytes(const Type *node,
                                                 size_t count,
                                                 StoragePrecisionPolicy policy) noexcept;

template<typename T>
[[nodiscard]] inline size_t compile_time_resolved_layout_size(StoragePrecisionPolicy policy) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    return recursive_resolved_size<CompileTimeTypeLayoutAdapter>(StaticTypeKey<raw_t>{}, policy);
}

template<typename T>
[[nodiscard]] inline size_t compile_time_resolved_layout_alignment(StoragePrecisionPolicy policy) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    return recursive_resolved_alignment<CompileTimeTypeLayoutAdapter>(StaticTypeKey<raw_t>{}, policy);
}

template<typename T>
[[nodiscard]] inline size_t compile_time_soa_storage_bytes(size_t count,
                                                           StoragePrecisionPolicy policy) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    return recursive_soa_storage_bytes<CompileTimeTypeLayoutAdapter>(StaticTypeKey<raw_t>{}, count, policy);
}

template<typename T>
[[nodiscard]] inline size_t compile_time_soa_stride(StoragePrecisionPolicy policy,
                                                    size_t stride = 0u) noexcept {
    return stride == 0u ? compile_time_soa_storage_bytes<T>(1u, policy) : stride;
}

[[nodiscard]] inline size_t runtime_resolved_layout_size(const Type *type,
                                                         StoragePrecisionPolicy policy) noexcept {
    return recursive_resolved_size<RuntimeTypeLayoutAdapter>(type, policy);
}

[[nodiscard]] inline size_t runtime_resolved_layout_alignment(const Type *type,
                                                              StoragePrecisionPolicy policy) noexcept {
    return recursive_resolved_alignment<RuntimeTypeLayoutAdapter>(type, policy);
}

[[nodiscard]] inline size_t runtime_soa_storage_bytes(const Type *type,
                                                      size_t count,
                                                      StoragePrecisionPolicy policy) noexcept {
    return recursive_soa_storage_bytes<RuntimeTypeLayoutAdapter>(type, count, policy);
}

template<typename T>
[[nodiscard]] inline size_t resolved_scalar_size(StoragePrecisionPolicy policy) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    if constexpr (is_real_v<raw_t>) {
        return policy.policy == PrecisionPolicy::force_f32 ? sizeof(float) : sizeof(uint16_t);
    } else if constexpr (is_half_v<raw_t>) {
        return sizeof(uint16_t);
    } else {
        return sizeof(raw_t);
    }
}

template<typename T>
[[nodiscard]] inline size_t resolved_scalar_alignment(StoragePrecisionPolicy policy) noexcept {
    using raw_t = std::remove_cvref_t<T>;
    if constexpr (is_real_v<raw_t>) {
        return policy.policy == PrecisionPolicy::force_f32 ? alignof(float) : alignof(uint16_t);
    } else if constexpr (is_half_v<raw_t>) {
        return alignof(uint16_t);
    } else {
        return alignof(raw_t);
    }
}

template<typename Adapter, typename T>
[[nodiscard]] size_t recursive_resolved_size(StaticTypeKey<T> node,
                                             StoragePrecisionPolicy policy) noexcept {
    if constexpr (Adapter::is_scalar(node)) {
        return Adapter::scalar_size(node, policy);
    } else if constexpr (Adapter::is_vector(node)) {
        return recursive_resolved_size<Adapter>(Adapter::element(node), policy) *
               Adapter::vector_storage_width(node);
    } else if constexpr (Adapter::is_matrix(node) || Adapter::is_array(node)) {
        return recursive_resolved_size<Adapter>(Adapter::element(node), policy) * Adapter::dimension(node);
    } else if constexpr (Adapter::is_structure(node)) {
        size_t size = 0u;
        Adapter::for_each_member(node, [&](auto member) {
            size = Adapter::align_up(size, recursive_resolved_alignment<Adapter>(member, policy));
            size += recursive_resolved_size<Adapter>(member, policy);
        });
        return Adapter::align_up(size, recursive_resolved_alignment<Adapter>(node, policy));
    } else {
        return Adapter::fallback_size(node);
    }
}

template<typename Adapter, typename T>
[[nodiscard]] size_t recursive_resolved_alignment(StaticTypeKey<T> node,
                                                  StoragePrecisionPolicy policy) noexcept {
    if constexpr (Adapter::is_scalar(node)) {
        return Adapter::scalar_alignment(node, policy);
    } else if constexpr (Adapter::is_vector(node)) {
        return recursive_resolved_size<Adapter>(node, policy);
    } else if constexpr (Adapter::is_matrix(node) || Adapter::is_array(node)) {
        return recursive_resolved_alignment<Adapter>(Adapter::element(node), policy);
    } else if constexpr (Adapter::is_structure(node)) {
        size_t align = 0u;
        Adapter::for_each_member(node, [&](auto member) {
            align = std::max(align, recursive_resolved_alignment<Adapter>(member, policy));
        });
        return align;
    } else {
        return Adapter::fallback_alignment(node);
    }
}

template<typename Adapter, typename T>
[[nodiscard]] size_t recursive_soa_storage_bytes(StaticTypeKey<T> node,
                                                 size_t count,
                                                 StoragePrecisionPolicy policy) noexcept {
    if constexpr (Adapter::is_scalar(node) || Adapter::is_vector(node)) {
        return recursive_resolved_size<Adapter>(node, policy) * count;
    } else if constexpr (Adapter::is_matrix(node) || Adapter::is_array(node)) {
        return Adapter::dimension(node) * recursive_soa_storage_bytes<Adapter>(Adapter::element(node), count, policy);
    } else if constexpr (Adapter::is_structure(node)) {
        size_t total = 0u;
        Adapter::for_each_member(node, [&](auto member) {
            total += recursive_soa_storage_bytes<Adapter>(member, count, policy);
        });
        return total;
    } else {
        return Adapter::fallback_size(node) * count;
    }
}

template<typename Adapter>
[[nodiscard]] size_t recursive_resolved_size(const Type *node,
                                             StoragePrecisionPolicy policy) noexcept {
    if (Adapter::is_scalar(node)) {
        return Adapter::scalar_size(node, policy);
    }
    if (Adapter::is_vector(node)) {
        return recursive_resolved_size<Adapter>(Adapter::element(node), policy) *
               Adapter::vector_storage_width(node);
    }
    if (Adapter::is_matrix(node) || Adapter::is_array(node)) {
        return recursive_resolved_size<Adapter>(Adapter::element(node), policy) * Adapter::dimension(node);
    }
    if (Adapter::is_structure(node)) {
        size_t size = 0u;
        Adapter::for_each_member(node, [&](auto member) {
            size = Adapter::align_up(size, recursive_resolved_alignment<Adapter>(member, policy));
            size += recursive_resolved_size<Adapter>(member, policy);
        });
        return Adapter::align_up(size, recursive_resolved_alignment<Adapter>(node, policy));
    }
    return Adapter::fallback_size(node);
}

template<typename Adapter>
[[nodiscard]] size_t recursive_resolved_alignment(const Type *node,
                                                  StoragePrecisionPolicy policy) noexcept {
    if (Adapter::is_scalar(node)) {
        return Adapter::scalar_alignment(node, policy);
    }
    if (Adapter::is_vector(node)) {
        return recursive_resolved_size<Adapter>(node, policy);
    }
    if (Adapter::is_matrix(node) || Adapter::is_array(node)) {
        return recursive_resolved_alignment<Adapter>(Adapter::element(node), policy);
    }
    if (Adapter::is_structure(node)) {
        size_t align = 0u;
        Adapter::for_each_member(node, [&](auto member) {
            align = std::max(align, recursive_resolved_alignment<Adapter>(member, policy));
        });
        return align;
    }
    return Adapter::fallback_alignment(node);
}

template<typename Adapter>
[[nodiscard]] size_t recursive_soa_storage_bytes(const Type *node,
                                                 size_t count,
                                                 StoragePrecisionPolicy policy) noexcept {
    if (Adapter::is_scalar(node) || Adapter::is_vector(node)) {
        return recursive_resolved_size<Adapter>(node, policy) * count;
    }
    if (Adapter::is_matrix(node) || Adapter::is_array(node)) {
        return Adapter::dimension(node) * recursive_soa_storage_bytes<Adapter>(Adapter::element(node), count, policy);
    }
    if (Adapter::is_structure(node)) {
        size_t total = 0u;
        Adapter::for_each_member(node, [&](auto member) {
            total += recursive_soa_storage_bytes<Adapter>(member, count, policy);
        });
        return total;
    }
    return Adapter::fallback_size(node) * count;
}

struct RuntimeTypeLayoutAdapter {
    [[nodiscard]] static size_t align_up(size_t value, size_t alignment) noexcept {
        OC_ASSERT(alignment != 0u);
        return (value + alignment - 1u) / alignment * alignment;
    }

    [[nodiscard]] static bool is_scalar(const Type *type) noexcept { return type->is_scalar(); }
    [[nodiscard]] static bool is_vector(const Type *type) noexcept { return type->is_vector(); }
    [[nodiscard]] static bool is_matrix(const Type *type) noexcept { return type->is_matrix(); }
    [[nodiscard]] static bool is_array(const Type *type) noexcept { return type->is_array(); }
    [[nodiscard]] static bool is_structure(const Type *type) noexcept { return type->is_structure(); }
    [[nodiscard]] static const Type *element(const Type *type) noexcept { return type->element(); }
    [[nodiscard]] static size_t dimension(const Type *type) noexcept { return type->dimension(); }
    [[nodiscard]] static size_t vector_storage_width(const Type *type) noexcept {
        return type->dimension() == 3 ? 4u : static_cast<size_t>(type->dimension());
    }
    [[nodiscard]] static size_t scalar_size(const Type *type, StoragePrecisionPolicy) noexcept {
        return type->size();
    }
    [[nodiscard]] static size_t scalar_alignment(const Type *type, StoragePrecisionPolicy) noexcept {
        return type->alignment();
    }
    [[nodiscard]] static size_t fallback_size(const Type *type) noexcept { return type->size(); }
    [[nodiscard]] static size_t fallback_alignment(const Type *type) noexcept { return type->alignment(); }

    template<typename Func>
    static void for_each_member(const Type *type, Func &&func) noexcept {
        for (const auto *member : type->members()) {
            func(member);
        }
    }
};

struct CompileTimeTypeLayoutAdapter {
    [[nodiscard]] static size_t align_up(size_t value, size_t alignment) noexcept {
        OC_ASSERT(alignment != 0u);
        return (value + alignment - 1u) / alignment * alignment;
    }

    template<typename Node>
    [[nodiscard]] static constexpr bool is_scalar(Node) noexcept {
        using raw_t = typename Node::type;
        return is_scalar_v<raw_t>;
    }

    template<typename Node>
    [[nodiscard]] static constexpr bool is_vector(Node) noexcept {
        using raw_t = typename Node::type;
        return is_vector_v<raw_t>;
    }

    template<typename Node>
    [[nodiscard]] static constexpr bool is_matrix(Node) noexcept {
        using raw_t = typename Node::type;
        return is_matrix_v<raw_t>;
    }

    template<typename Node>
    [[nodiscard]] static constexpr bool is_array(Node) noexcept {
        using raw_t = typename Node::type;
        return is_array_v<raw_t>;
    }

    template<typename Node>
    [[nodiscard]] static constexpr bool is_structure(Node) noexcept {
        using raw_t = typename Node::type;
        return is_struct_v<raw_t>;
    }

    template<typename Node>
    [[nodiscard]] static constexpr auto element(Node) noexcept {
        using raw_t = typename Node::type;
        if constexpr (is_vector_v<raw_t>) {
            return StaticTypeKey<type_element_t<raw_t>>{};
        } else if constexpr (is_matrix_v<raw_t>) {
            return StaticTypeKey<tuple_element_t<0, struct_member_tuple_t<raw_t>>>{};
        } else {
            return StaticTypeKey<array_element_t<raw_t>>{};
        }
    }

    template<typename Node>
    [[nodiscard]] static constexpr size_t dimension(Node) noexcept {
        using raw_t = typename Node::type;
        if constexpr (is_vector_v<raw_t>) {
            return vector_dimension_v<raw_t>;
        } else if constexpr (is_matrix_v<raw_t>) {
            return matrix_dimension_v<raw_t>;
        } else {
            return array_dimension_v<raw_t>;
        }
    }

    template<typename Node>
    [[nodiscard]] static constexpr size_t vector_storage_width(Node) noexcept {
        using raw_t = typename Node::type;
        constexpr size_t dim = vector_dimension_v<raw_t>;
        return dim == 3 ? 4u : dim;
    }

    template<typename Node>
    [[nodiscard]] static size_t scalar_size(Node, StoragePrecisionPolicy policy) noexcept {
        using raw_t = typename Node::type;
        return resolved_scalar_size<raw_t>(policy);
    }

    template<typename Node>
    [[nodiscard]] static size_t scalar_alignment(Node, StoragePrecisionPolicy policy) noexcept {
        using raw_t = typename Node::type;
        return resolved_scalar_alignment<raw_t>(policy);
    }

    template<typename Node>
    [[nodiscard]] static constexpr size_t fallback_size(Node) noexcept {
        using raw_t = typename Node::type;
        return sizeof(raw_t);
    }

    template<typename Node>
    [[nodiscard]] static constexpr size_t fallback_alignment(Node) noexcept {
        using raw_t = typename Node::type;
        return alignof(raw_t);
    }

    template<typename Node, typename Func>
    static void for_each_member(Node, Func &&func) noexcept {
        using raw_t = typename Node::type;
        for_each_struct_member_type<raw_t>([&](auto member_tag, size_t) {
            using member_t = std::remove_cvref_t<decltype(member_tag)>;
            func(StaticTypeKey<member_t>{});
        });
    }
};

}// namespace ocarina::detail