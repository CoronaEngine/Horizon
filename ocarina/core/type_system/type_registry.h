#pragma once

#include "core/type.h"

#include <mutex>

namespace ocarina {

class TypeRegistry {
private:
    struct ResolvedTypeKey {
        const Type *logical_type{nullptr};
        PrecisionPolicy policy{PrecisionPolicy::force_f32};
        bool allow_real_in_storage{false};

        [[nodiscard]] bool operator==(const ResolvedTypeKey &rhs) const noexcept {
            return logical_type == rhs.logical_type &&
                   policy == rhs.policy &&
                   allow_real_in_storage == rhs.allow_real_in_storage;
        }
    };

    struct ResolvedTypeKeyHash {
        [[nodiscard]] size_t operator()(const ResolvedTypeKey &key) const noexcept {
            size_t seed = reinterpret_cast<size_t>(key.logical_type);
            seed ^= static_cast<size_t>(key.policy) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
            seed ^= static_cast<size_t>(key.allow_real_in_storage) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
            return seed;
        }
    };

private:
    ocarina::vector<ocarina::unique_ptr<Type>> types_;
    ocarina::unordered_map<uint64_t, const Type *> by_hash_;
    ocarina::unordered_map<ResolvedTypeKey, const Type *, ResolvedTypeKeyHash> resolved_types_;
    std::recursive_mutex mutex_;

private:
    [[nodiscard]] const Type *find_by_hash_locked(uint64_t hash) const noexcept;
    [[nodiscard]] const Type *emplace_type_locked(ocarina::unique_ptr<Type> type) noexcept;

public:
    [[nodiscard]] bool register_type(ocarina::unique_ptr<Type> type) noexcept;
    [[nodiscard]] const Type *find_by_hash(uint64_t hash) noexcept;
    [[nodiscard]] const Type *emplace_type(ocarina::unique_ptr<Type> type) noexcept;
    [[nodiscard]] const Type *resolve_type(const Type *type,
                                           StoragePrecisionPolicy policy) noexcept;
    [[nodiscard]] const Type *parse_type(std::string_view description) noexcept;
    [[nodiscard]] size_t count() noexcept;
    [[nodiscard]] const Type *at(uint32_t uid) noexcept;
    [[nodiscard]] bool exists(uint64_t hash) noexcept;
    [[nodiscard]] ocarina::vector<const Type *> snapshot() noexcept;
};

[[nodiscard]] TypeRegistry &type_registry() noexcept;

[[nodiscard]] uint64_t compute_type_hash(ocarina::string_view desc) noexcept;

void notify_type_access(const Type *type) noexcept;

namespace detail {

[[nodiscard]] const Type *parse_type_locked(ocarina::string_view desc) noexcept;

}

}// namespace ocarina