//
// Created by Z on 11/04/2026.
//

#pragma once

#include <algorithm>

#include "core/type_system/precision_policy.h"
#include "core/stl.h"
#include "core/type_system/type_desc.h"

namespace ocarina {

class LayoutResolver {
private:
    StoragePrecisionPolicy policy_{};

public:
    explicit LayoutResolver(StoragePrecisionPolicy policy = {}) noexcept;
    OC_MAKE_MEMBER_GETTER_SETTER(policy, &)
    [[nodiscard]] const Type *resolve(const Type *type) const noexcept;
    template<typename T>
    [[nodiscard]] const Type *resolve() const noexcept {
        return resolve(Type::of<T>());
    }
    [[nodiscard]] string resolve_description(const Type *type) const noexcept;

private:
    [[nodiscard]] string resolve_real_description() const noexcept;
    [[nodiscard]] const Type *resolve_real_container_element(const Type *type) const noexcept;
    [[nodiscard]] string resolve_vector_description(const Type *type) const noexcept;
    [[nodiscard]] string resolve_matrix_description(const Type *type) const noexcept;
    [[nodiscard]] string resolve_array_description(const Type *type) const noexcept;
    [[nodiscard]] string resolve_buffer_description(const Type *type) const noexcept;
    [[nodiscard]] size_t resolved_alignment(const Type *type) const noexcept;
    [[nodiscard]] string resolve_structure_description(const Type *type) const noexcept;
};

}// namespace ocarina