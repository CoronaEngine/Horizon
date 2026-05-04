//
// Created by Zero on 30/04/2022.
//

#include "core/type_system/type_registry.h"
#include "core/util/logging.h"

namespace ocarina {

namespace detail {

[[nodiscard]] static TypeSystemCallbacks &type_system_callbacks() noexcept {
    static TypeSystemCallbacks callbacks;
    return callbacks;
}

void register_type_system_callbacks(TypeSystemCallbacks callbacks) noexcept {
    type_system_callbacks() = callbacks;
}

}// namespace detail

namespace {

[[nodiscard]] string resolve_real_description(StoragePrecisionPolicy policy) noexcept {
    switch (policy.policy) {
        case PrecisionPolicy::force_f16:
            return "half";
        case PrecisionPolicy::force_f32:
        default:
            return "float";
    }
}

[[nodiscard]] string resolve_type_description_locked(const Type *type,
                                                     StoragePrecisionPolicy policy) noexcept;

[[nodiscard]] size_t resolved_alignment_locked(const Type *type,
                                               StoragePrecisionPolicy policy) noexcept {
    if (type == nullptr) {
        return 0u;
    }
    if (type->tag() == Type::Tag::REAL) {
        const auto *resolved = type_registry().resolve_type(type, policy);
        return resolved == nullptr ? 0u : resolved->alignment();
    }
    if (type->is_vector() || type->is_matrix() || type->is_array() || type->is_buffer()) {
        const auto *resolved = type_registry().resolve_type(type, policy);
        return resolved == nullptr ? 0u : resolved->alignment();
    }
    if (type->is_scalar() || type->is_byte_buffer() || type->is_texture() ||
        type->is_bindless_array() || type->is_accel()) {
        return type->alignment();
    }
    if (type->is_structure()) {
        size_t align = 0u;
        for (const auto *member : type->members()) {
            align = std::max(align, resolved_alignment_locked(member, policy));
        }
        return align == 0u ? type->alignment() : align;
    }
    return type->alignment();
}

[[nodiscard]] string resolve_type_description_locked(const Type *type,
                                                     StoragePrecisionPolicy policy) noexcept {
    if (type == nullptr) {
        return {};
    }
    if (type->tag() == Type::Tag::REAL) {
        return resolve_real_description(policy);
    }
    if (type->is_scalar()) {
        return string(type->description());
    }
    if (type->is_vector()) {
        const auto *elem = type_registry().resolve_type(type->element(), policy);
        return elem == nullptr ? string{} : ocarina::format("vector<{},{}>", elem->description(), type->dimension());
    }
    if (type->is_matrix()) {
        const auto *col = type_registry().resolve_type(type->element(), policy);
        if (col == nullptr || !col->is_vector()) {
            return {};
        }
        const auto *scalar = col->element();
        return scalar == nullptr ? string{} : ocarina::format("matrix<{},{},{}>", scalar->description(), type->dimension(), col->dimension());
    }
    if (type->is_array()) {
        const auto *elem = type_registry().resolve_type(type->element(), policy);
        return elem == nullptr ? string{} : ocarina::format("array<{},{}>", elem->description(), type->dimension());
    }
    if (type->is_buffer()) {
        const auto *elem = type_registry().resolve_type(type->element(), policy);
        return elem == nullptr ? string{} : ocarina::format("buffer<{}>", elem->description());
    }
    if (type->is_structure()) {
        string ret = ocarina::format("struct<{},{},{},{}",
                                     type->cname(),
                                     resolved_alignment_locked(type, policy),
                                     type->is_builtin_struct(),
                                     type->is_param_struct());
        for (const auto *member : type->members()) {
            const auto desc = resolve_type_description_locked(member, policy);
            if (desc.empty()) {
                return {};
            }
            ret.append(",").append(desc);
        }
        ret.push_back('>');
        return ret;
    }
    if (type->is_byte_buffer() || type->is_texture() || type->is_bindless_array() || type->is_accel()) {
        return string(type->description());
    }
    OC_ASSERT(false);
    return {};
}

}// namespace

[[nodiscard]] const Type *TypeRegistry::find_by_hash_locked(uint64_t hash) const noexcept {
    if (auto iter = by_hash_.find(hash); iter != by_hash_.cend()) {
        return iter->second;
    }
    return nullptr;
}

[[nodiscard]] const Type *TypeRegistry::emplace_type_locked(ocarina::unique_ptr<Type> type) noexcept {
    const Type *ret = type.get();
    by_hash_.emplace(compute_type_hash(type->description()), ret);
    types_.push_back(ocarina::move(type));
    return ret;
}

[[nodiscard]] bool TypeRegistry::register_type(ocarina::unique_ptr<Type> type) noexcept {
    std::unique_lock lock{mutex_};
    const auto hash = type->hash();
    if (by_hash_.contains(hash)) {
        return false;
    }
    const auto *ptr = type.get();
    types_.emplace_back(std::move(type));
    by_hash_.emplace(hash, ptr);
    return true;
}

[[nodiscard]] const Type *TypeRegistry::find_by_hash(uint64_t hash) noexcept {
    std::unique_lock lock{mutex_};
    return find_by_hash_locked(hash);
}

[[nodiscard]] const Type *TypeRegistry::emplace_type(ocarina::unique_ptr<Type> type) noexcept {
    std::unique_lock lock{mutex_};
    return emplace_type_locked(ocarina::move(type));
}

[[nodiscard]] const Type *TypeRegistry::resolve_type(const Type *type,
                                                     StoragePrecisionPolicy policy) noexcept {
    if (type == nullptr) {
        return nullptr;
    }
    if (!type->is_dynamic()) {
        return type;
    }
    std::unique_lock lock{mutex_};
    ResolvedTypeKey key{.logical_type = type,
                        .policy = policy.policy,
                        .allow_real_in_storage = policy.allow_real_in_storage};
    if (auto iter = resolved_types_.find(key); iter != resolved_types_.cend()) {
        notify_type_access(iter->second);
        return iter->second;
    }
    const auto desc = resolve_type_description_locked(type, policy);
    const auto *resolved = desc.empty() ? nullptr : Type::from(desc);
    resolved_types_.emplace(key, resolved);
    return resolved;
}

[[nodiscard]] const Type *TypeRegistry::parse_type(std::string_view description) noexcept {
    std::unique_lock lock{mutex_};
    return detail::parse_type_locked(description);
}

[[nodiscard]] size_t TypeRegistry::count() noexcept {
    std::unique_lock lock{mutex_};
    return types_.size();
}

[[nodiscard]] const Type *TypeRegistry::at(uint32_t uid) noexcept {
    std::unique_lock lock{mutex_};
    return uid < types_.size() ? types_[uid].get() : nullptr;
}

[[nodiscard]] bool TypeRegistry::exists(uint64_t hash) noexcept {
    std::unique_lock lock{mutex_};
    return by_hash_.find(hash) != by_hash_.cend();
}

[[nodiscard]] ocarina::vector<const Type *> TypeRegistry::snapshot() noexcept {
    std::unique_lock lock{mutex_};
    ocarina::vector<const Type *> snapshot;
    snapshot.reserve(types_.size());
    for (const auto &type : types_) {
        snapshot.push_back(type.get());
    }
    return snapshot;
}

[[nodiscard]] TypeRegistry &type_registry() noexcept {
    static TypeRegistry registry;
    return registry;
}

[[nodiscard]] uint64_t compute_type_hash(ocarina::string_view desc) noexcept {
    return Hashable::compute_hash<Type>(hash64(desc));
}

void notify_type_access(const Type *type) noexcept {
    if (type == nullptr) {
        return;
    }
    auto &callbacks = detail::type_system_callbacks();
    if (callbacks.on_type_access != nullptr) {
        callbacks.on_type_access(type);
    }
}

}// namespace ocarina