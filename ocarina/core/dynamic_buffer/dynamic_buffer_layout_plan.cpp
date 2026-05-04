//
// Created by GitHub Copilot on 2026/4/13.
//

#include "dynamic_buffer_layout_plan.h"

#include "core/dynamic_buffer/dynamic_buffer_field_access.h"
#include "core/dynamic_buffer/dynamic_buffer_layout_common.h"
#include "core/type_system/type_desc.h"

namespace ocarina {

namespace {

/// Resolve the storage scalar used for logical real under the active policy.
[[nodiscard]] string resolve_real_description(StoragePrecisionPolicy policy) noexcept {
    if (!policy.allow_real_in_storage) {
        return {};
    }
    switch (policy.policy) {
        case PrecisionPolicy::force_f16:
            return string(TypeDesc<half>::description());
        case PrecisionPolicy::force_f32:
        default:
            return string(TypeDesc<float>::description());
    }
}

[[nodiscard]] const Type *resolve_type_description(const Type *type,
                                                   StoragePrecisionPolicy policy) noexcept;

/// Resolve the encoded element type for aggregate containers whose scalar may change.
[[nodiscard]] const Type *resolve_real_container_element(const Type *type,
                                                         StoragePrecisionPolicy policy) noexcept {
    const auto *resolved = resolve_type_description(type, policy);
    if (resolved == nullptr) {
        return nullptr;
    }
    return resolved;
}

/// Compute alignment after recursive precision resolution.
[[nodiscard]] size_t resolved_alignment(const Type *type,
                                        StoragePrecisionPolicy policy) noexcept {
    if (type == nullptr) {
        return 0u;
    }
    if (type->tag() == Type::Tag::REAL) {
        const auto *resolved = resolve_type_description(type, policy);
        return resolved == nullptr ? 0u : resolved->alignment();
    }
    if (type->is_scalar() || type->is_byte_buffer() || type->is_texture() ||
        type->is_bindless_array() || type->is_accel()) {
        return type->alignment();
    }
    if (type->is_vector() || type->is_matrix() || type->is_array() || type->is_buffer()) {
        const auto *elem = resolve_real_container_element(type->element(), policy);
        return elem == nullptr ? 0u : elem->alignment();
    }
    if (type->is_structure()) {
        size_t align = 0u;
        for (const auto *member : type->members()) {
            align = std::max(align, resolved_alignment(member, policy));
        }
        return align == 0u ? type->alignment() : align;
    }
    return type->alignment();
}

/// Rebuild a type description string after applying storage-precision resolution.
[[nodiscard]] string resolve_description(const Type *type,
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
        const auto *elem = resolve_type_description(type->element(), policy);
        return elem == nullptr ? string{} : ocarina::format("vector<{},{}>", elem->description(), type->dimension());
    }
    if (type->is_matrix()) {
        const auto *col = resolve_type_description(type->element(), policy);
        if (col == nullptr || !col->is_vector()) {
            return {};
        }
        const auto *scalar = col->element();
        return scalar == nullptr ? string{} : ocarina::format("matrix<{},{},{}>",
                                                               scalar->description(),
                                                               type->dimension(),
                                                               col->dimension());
    }
    if (type->is_array()) {
        const auto *elem = resolve_real_container_element(type->element(), policy);
        return elem == nullptr ? string{} : ocarina::format("array<{},{}>", elem->description(), type->dimension());
    }
    if (type->is_buffer()) {
        const auto *elem = resolve_real_container_element(type->element(), policy);
        return elem == nullptr ? string{} : ocarina::format("buffer<{}>", elem->description());
    }
    if (type->is_structure()) {
        string ret = ocarina::format("struct<{},{},{},{}",
                                     type->cname(),
                                     resolved_alignment(type, policy),
                                     type->is_builtin_struct(),
                                     type->is_param_struct());
        for (const auto *member : type->members()) {
            const auto desc = resolve_description(member, policy);
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
    return {};
}

/// Reify the resolved description back into a registered Type instance.
[[nodiscard]] const Type *resolve_type_description(const Type *type,
                                                   StoragePrecisionPolicy policy) noexcept {
    const auto desc = resolve_description(type, policy);
    return desc.empty() ? nullptr : Type::from(desc);
}

/// Detect whether any logical real becomes a narrower storage representation.
[[nodiscard]] bool has_precision_lowering_recursive(const Type *logical, const Type *resolved) noexcept {
    if (logical == nullptr || resolved == nullptr) {
        return false;
    }
    if (logical->tag() == Type::Tag::REAL) {
        return resolved->tag() == Type::Tag::HALF;
    }
    if ((logical->is_vector() || logical->is_matrix() || logical->is_array()) &&
        (resolved->is_vector() || resolved->is_matrix() || resolved->is_array())) {
        return has_precision_lowering_recursive(logical->element(), resolved->element());
    }
    if (logical->is_structure() && resolved->is_structure()) {
        const auto logical_members = logical->members();
        const auto resolved_members = resolved->members();
        if (logical_members.size() != resolved_members.size()) {
            return false;
        }
        for (size_t index = 0; index < logical_members.size(); ++index) {
            if (has_precision_lowering_recursive(logical_members[index], resolved_members[index])) {
                return true;
            }
        }
    }
    return false;
}

struct FieldRegionInfo {
    /// Logical field type addressed by the path.
    const Type *logical_type{nullptr};
    /// Encoded storage type addressed by the path.
    const Type *resolved_type{nullptr};
    /// Byte offset from the beginning of one encoded record.
    size_t offset_in_record{0u};
    /// Encoded field size in bytes.
    size_t size_in_bytes{0u};

    [[nodiscard]] bool valid() const noexcept {
        return logical_type != nullptr && resolved_type != nullptr;
    }
};

/// Resolve one logical field path into byte offset, byte size, and type information.
[[nodiscard]] FieldRegionInfo resolve_field_region_info(const Type *logical_type,
                                                        const Type *resolved_type,
                                                        span<const TypedFieldPath::Step> steps,
                                                        size_t current_offset) noexcept {
    auto info = detail::resolve_runtime_field_access_info(logical_type,
                                                          resolved_type,
                                                          steps,
                                                          detail::FieldOffsetMode::AOS,
                                                          1u,
                                                          current_offset);
    if (!info.valid()) {
        return {};
    }
    return FieldRegionInfo{info.logical_type, info.resolved_type, info.offset, info.size_in_bytes};
}

}// namespace

TypedFieldPath TypedFieldPath::append_member(uint32_t member_index) const {
    auto copy = *this;
    copy.steps_.push_back(Step{.kind = StepKind::member, .value = member_index});
    return copy;
}

TypedFieldPath TypedFieldPath::append_index(uint32_t element_index) const {
    auto copy = *this;
    copy.steps_.push_back(Step{.kind = StepKind::index, .value = element_index});
    return copy;
}

TypedFieldPath TypedFieldPath::append_component(uint32_t component_index) const {
    auto copy = *this;
    copy.steps_.push_back(Step{.kind = StepKind::component, .value = component_index});
    return copy;
}

TypedFieldPath TypedFieldPath::member(uint32_t member_index) noexcept {
    TypedFieldPath path;
    path.steps_.push_back(Step{.kind = StepKind::member, .value = member_index});
    return path;
}

DynamicBufferLayoutPlan DynamicBufferLayoutPlan::create(const Type *logical_type,
                                                        StoragePrecisionPolicy policy) {
    OC_ASSERT(logical_type != nullptr);
    const auto *resolved_type = Type::resolve(logical_type, policy);
    OC_ASSERT(resolved_type != nullptr);
    return DynamicBufferLayoutPlan{logical_type,
                                   policy,
                                   detail::resolved_runtime_field_size(resolved_type),
                                   detail::resolved_runtime_field_alignment(resolved_type),
                                   has_precision_lowering_recursive(logical_type, resolved_type)};
}

size_t DynamicBufferLayoutPlan::storage_bytes(size_t element_count) const noexcept {
    return element_size_bytes_ * element_count;
}

ByteRegion DynamicBufferLayoutPlan::record_region(size_t index) const noexcept {
    const auto begin = index * element_size_bytes_;
    return ByteRegion{begin, begin + element_size_bytes_};
}

ByteRegion DynamicBufferLayoutPlan::field_region(size_t index,
                                                 span<const TypedFieldPath::Step> steps) const noexcept {
    auto info = resolve_field_region_info(logical_type_, resolved_type(), steps, 0u);
    OC_ASSERT(info.valid());
    const auto record = record_region(index);
    return ByteRegion{record.begin_byte + info.offset_in_record,
                      record.begin_byte + info.offset_in_record + info.size_in_bytes};
}

vector<ByteSegment> DynamicBufferLayoutPlan::record_segments(size_t index) const noexcept {
    vector<ByteSegment> segments;
    const auto region = record_region(index);
    segments.emplace_back(ByteSegment{
        .storage_begin_byte = region.begin_byte,
        .staging_begin_byte = 0u,
        .size_in_bytes = region.size()});
    return segments;
}

vector<ByteSegment> DynamicBufferLayoutPlan::field_segments(size_t index,
                                                            span<const TypedFieldPath::Step> steps) const noexcept {
    vector<ByteSegment> segments;
    const auto region = field_region(index, steps);
    segments.emplace_back(ByteSegment{
        .storage_begin_byte = region.begin_byte,
        .staging_begin_byte = 0u,
        .size_in_bytes = region.size()});
    return segments;
}

const Type *DynamicBufferLayoutPlan::field_logical_type(span<const TypedFieldPath::Step> steps) const noexcept {
    auto info = resolve_field_region_info(logical_type_, resolved_type(), steps, 0u);
    OC_ASSERT(info.valid());
    return info.logical_type;
}

const Type *DynamicBufferLayoutPlan::field_resolved_type(span<const TypedFieldPath::Step> steps) const noexcept {
    auto info = resolve_field_region_info(logical_type_, resolved_type(), steps, 0u);
    OC_ASSERT(info.valid());
    return info.resolved_type;
}

}// namespace ocarina