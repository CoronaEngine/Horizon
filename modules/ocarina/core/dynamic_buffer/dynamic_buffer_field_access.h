//
// Created by GitHub Copilot on 2026/4/17.
//

#pragma once

#include "core/dynamic_buffer/dynamic_buffer_layout_common.h"
#include "core/dynamic_buffer/dynamic_buffer_layout_plan.h"

namespace ocarina::detail {

[[nodiscard]] inline StoragePrecisionPolicy runtime_field_policy() noexcept {
    return StoragePrecisionPolicy{.policy = PrecisionPolicy::force_f32,
                                  .allow_real_in_storage = true};
}

[[nodiscard]] inline size_t resolved_runtime_field_size(const Type *type) noexcept {
    OC_ASSERT(type != nullptr);
    return runtime_resolved_layout_size(type, runtime_field_policy());
}

[[nodiscard]] inline size_t resolved_runtime_field_alignment(const Type *type) noexcept {
    OC_ASSERT(type != nullptr);
    return runtime_resolved_layout_alignment(type, runtime_field_policy());
}

[[nodiscard]] inline size_t soa_runtime_field_storage_bytes(const Type *type,
                                                            size_t count) noexcept {
    if (type == nullptr) {
        return 0u;
    }
    return runtime_soa_storage_bytes(type, count, runtime_field_policy());
}

enum class FieldOffsetMode : uint8_t {
    AOS,
    SOA,
};

struct RuntimeFieldAccessInfo {
    const Type *logical_type{nullptr};
    const Type *resolved_type{nullptr};
    size_t offset{0u};
    size_t size_in_bytes{0u};
    size_t record_stride{0u};

    [[nodiscard]] bool valid() const noexcept {
        return logical_type != nullptr && resolved_type != nullptr;
    }
};

[[nodiscard]] inline size_t runtime_field_member_offset(const Type *resolved_type,
                                                        uint32_t member_index,
                                                        FieldOffsetMode mode,
                                                        size_t element_count) noexcept {
    size_t offset = 0u;
    for (uint32_t index = 0u; index < member_index; ++index) {
        const auto *member = resolved_type->members()[index];
        if (mode == FieldOffsetMode::AOS) {
            offset = RuntimeTypeLayoutAdapter::align_up(offset,
                                                        resolved_runtime_field_alignment(member));
            offset += resolved_runtime_field_size(member);
        } else {
            offset += soa_runtime_field_storage_bytes(member, element_count);
        }
    }
    return offset;
}

[[nodiscard]] inline size_t runtime_field_element_offset(const Type *resolved_elem,
                                                         uint32_t element_index,
                                                         FieldOffsetMode mode,
                                                         size_t element_count) noexcept {
    if (mode == FieldOffsetMode::AOS) {
        return element_index * resolved_runtime_field_size(resolved_elem);
    }
    return element_index * soa_runtime_field_storage_bytes(resolved_elem, element_count);
}

[[nodiscard]] inline RuntimeFieldAccessInfo resolve_runtime_field_access_info(const Type *logical_type,
                                                                              const Type *resolved_type,
                                                                              span<const TypedFieldPath::Step> steps,
                                                                              FieldOffsetMode mode,
                                                                              size_t element_count,
                                                                              size_t current_offset = 0u) noexcept {
    if (logical_type == nullptr || resolved_type == nullptr) {
        return {};
    }
    size_t record_stride = 0u;
    while (!steps.empty()) {
        const auto step = steps.front();
        steps = steps.subspan(1u);
        if (logical_type->is_structure()) {
            if (step.kind != TypedFieldPath::StepKind::member) {
                return {};
            }
            const auto logical_members = logical_type->members();
            const auto resolved_members = resolved_type->members();
            if (logical_members.size() != resolved_members.size() || step.value >= logical_members.size()) {
                return {};
            }
            current_offset += runtime_field_member_offset(resolved_type, step.value, mode, element_count);
            const auto *resolved_member = resolved_members[step.value];
            if (mode == FieldOffsetMode::AOS) {
                current_offset = RuntimeTypeLayoutAdapter::align_up(current_offset,
                                                                    resolved_runtime_field_alignment(resolved_member));
            }
            logical_type = logical_members[step.value];
            resolved_type = resolved_member;
            continue;
        }
        if (logical_type->is_array()) {
            if (step.kind != TypedFieldPath::StepKind::index || step.value >= logical_type->dimension()) {
                return {};
            }
            const auto *resolved_elem = resolved_type->element();
            current_offset += runtime_field_element_offset(resolved_elem, step.value, mode, element_count);
            logical_type = logical_type->element();
            resolved_type = resolved_elem;
            continue;
        }
        if (logical_type->is_vector()) {
            if (step.kind != TypedFieldPath::StepKind::component || step.value >= logical_type->dimension()) {
                return {};
            }
            const auto *logical_elem = logical_type->element();
            const auto *resolved_elem = resolved_type->element();
            current_offset += step.value * resolved_runtime_field_size(resolved_elem);
            if (mode == FieldOffsetMode::SOA && steps.empty()) {
                record_stride = resolved_runtime_field_size(resolved_type);
            }
            logical_type = logical_elem;
            resolved_type = resolved_elem;
            continue;
        }
        if (logical_type->is_matrix()) {
            if ((step.kind != TypedFieldPath::StepKind::index && step.kind != TypedFieldPath::StepKind::component) ||
                step.value >= logical_type->dimension()) {
                return {};
            }
            const auto *resolved_elem = resolved_type->element();
            current_offset += runtime_field_element_offset(resolved_elem, step.value, mode, element_count);
            logical_type = logical_type->element();
            resolved_type = resolved_elem;
            continue;
        }
        return {};
    }
    return RuntimeFieldAccessInfo{logical_type,
                                  resolved_type,
                                  current_offset,
                                  resolved_runtime_field_size(resolved_type),
                                  record_stride == 0u ? resolved_runtime_field_size(resolved_type) : record_stride};
}

inline void collect_runtime_soa_segments(const Type *resolved_type,
                                         size_t element_count,
                                         size_t index,
                                         size_t storage_offset,
                                         size_t staging_offset,
                                         vector<ByteSegment> &segments) noexcept {
    if (resolved_type == nullptr) {
        return;
    }
    if (resolved_type->is_scalar() || resolved_type->is_vector()) {
        const auto stride = resolved_runtime_field_size(resolved_type);
        segments.emplace_back(ByteSegment{
            .storage_begin_byte = storage_offset + index * stride,
            .staging_begin_byte = staging_offset,
            .size_in_bytes = stride});
        return;
    }
    if (resolved_type->is_matrix() || resolved_type->is_array()) {
        const auto *resolved_elem = resolved_type->element();
        size_t current_storage = storage_offset;
        size_t current_staging = staging_offset;
        for (uint32_t element_index = 0u; element_index < resolved_type->dimension(); ++element_index) {
            collect_runtime_soa_segments(resolved_elem,
                                         element_count,
                                         index,
                                         current_storage,
                                         current_staging,
                                         segments);
            current_storage += soa_runtime_field_storage_bytes(resolved_elem, element_count);
            current_staging += soa_runtime_field_storage_bytes(resolved_elem, 1u);
        }
        return;
    }
    if (resolved_type->is_structure()) {
        size_t current_storage = storage_offset;
        size_t current_staging = staging_offset;
        for (const auto *member : resolved_type->members()) {
            collect_runtime_soa_segments(member,
                                         element_count,
                                         index,
                                         current_storage,
                                         current_staging,
                                         segments);
            current_storage += soa_runtime_field_storage_bytes(member, element_count);
            current_staging += soa_runtime_field_storage_bytes(member, 1u);
        }
        return;
    }
    const auto size = resolved_runtime_field_size(resolved_type);
    segments.emplace_back(ByteSegment{
        .storage_begin_byte = storage_offset + index * size,
        .staging_begin_byte = staging_offset,
        .size_in_bytes = size});
}

inline void collect_runtime_soa_field_segments(const RuntimeFieldAccessInfo &info,
                                               size_t element_count,
                                               size_t index,
                                               vector<ByteSegment> &segments) noexcept {
    if (!info.valid()) {
        return;
    }
    if (info.resolved_type->is_scalar()) {
        const auto size = resolved_runtime_field_size(info.resolved_type);
        segments.emplace_back(ByteSegment{
            .storage_begin_byte = info.offset + index * info.record_stride,
            .staging_begin_byte = 0u,
            .size_in_bytes = size});
        return;
    }
    collect_runtime_soa_segments(info.resolved_type,
                                 element_count,
                                 index,
                                 info.offset,
                                 0u,
                                 segments);
}

}// namespace ocarina::detail