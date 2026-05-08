//
// Created by GitHub Copilot on 2026/4/13.
//

#pragma once

#include <cstdint>

#include "core/dynamic_buffer/dynamic_buffer_layout_codec.h"
#include "core/type_system/precision_policy.h"
#include "core/type.h"

namespace ocarina {

/// Continuous byte range in canonical host storage.
struct ByteRegion {
    /// Inclusive begin byte offset.
    size_t begin_byte{0u};
    /// Exclusive end byte offset.
    size_t end_byte{0u};

    [[nodiscard]] size_t size() const noexcept {
        return end_byte - begin_byte;
    }

    [[nodiscard]] bool empty() const noexcept {
        return begin_byte >= end_byte;
    }
};

/// Copy segment between canonical storage and a temporary staging buffer.
struct ByteSegment {
    /// Begin byte inside the host storage buffer.
    size_t storage_begin_byte{0u};
    /// Begin byte inside the staging buffer.
    size_t staging_begin_byte{0u};
    /// Number of bytes covered by this segment.
    size_t size_in_bytes{0u};

    [[nodiscard]] bool empty() const noexcept {
        return size_in_bytes == 0u;
    }
};

/// Logical path to a subfield inside a reflected dynamic type.
///
/// The path is expressed in reflection space instead of byte offsets so the same
/// description can be resolved against different storage policies and layouts.
class TypedFieldPath {
public:
    /// One navigation step from the current logical node to a child node.
    enum class StepKind : uint8_t {
        /// Enter the Nth structure member.
        member,
        /// Enter the Nth array element.
        index,
        /// Enter the Nth vector or matrix component.
        component
    };

    struct Step {
        /// Category of navigation performed by this step.
        StepKind kind{};
        /// Zero-based member/index/component ordinal.
        uint32_t value{0u};
    };

private:
    /// Ordered logical navigation from the root record to the target field.
    vector<Step> steps_{};

public:
    TypedFieldPath() = default;

    /// Raw steps consumed by layout-plan field resolution.
    [[nodiscard]] span<const Step> steps() const noexcept { return steps_; }
    [[nodiscard]] bool empty() const noexcept { return steps_.empty(); }

    /// Return a new path with one structure-member hop appended.
    [[nodiscard]] TypedFieldPath append_member(uint32_t member_index) const;
    /// Return a new path with one array-element hop appended.
    [[nodiscard]] TypedFieldPath append_index(uint32_t element_index) const;
    /// Return a new path with one vector or matrix component hop appended.
    [[nodiscard]] TypedFieldPath append_component(uint32_t component_index) const;

    /// Convenience constructor for a single member step.
    [[nodiscard]] static TypedFieldPath member(uint32_t member_index) noexcept;
};

template<uint32_t Index>
struct FieldMemberStep {};

template<uint32_t Index>
struct FieldIndexStep {};

template<uint32_t Index>
struct FieldComponentStep {};

template<typename... Steps>
class StaticTypedFieldPath;

namespace detail {

template<typename Step>
struct TypedFieldPathStaticStep;

template<uint32_t Index>
struct TypedFieldPathStaticStep<FieldMemberStep<Index>> {
    inline static constexpr TypedFieldPath::Step value{
        .kind = TypedFieldPath::StepKind::member,
        .value = Index};
};

template<uint32_t Index>
struct TypedFieldPathStaticStep<FieldIndexStep<Index>> {
    inline static constexpr TypedFieldPath::Step value{
        .kind = TypedFieldPath::StepKind::index,
        .value = Index};
};

template<uint32_t Index>
struct TypedFieldPathStaticStep<FieldComponentStep<Index>> {
    inline static constexpr TypedFieldPath::Step value{
        .kind = TypedFieldPath::StepKind::component,
        .value = Index};
};

}// namespace detail

/// Compile-time field path that exposes the same step view as TypedFieldPath.
template<typename... Steps>
class StaticTypedFieldPath {
private:
    inline static constexpr ocarina::array<TypedFieldPath::Step, sizeof...(Steps)> steps_{
        detail::TypedFieldPathStaticStep<Steps>::value...
    };

public:
    [[nodiscard]] constexpr span<const TypedFieldPath::Step> steps() const noexcept {
        return {steps_.data(), steps_.size()};
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return steps_.empty();
    }
};

template<typename... Steps>
/// Build a compile-time checked field path from step tags.
[[nodiscard]] constexpr StaticTypedFieldPath<Steps...> make_typed_field_path() noexcept {
    return {};
}

/// Resolved layout metadata for one logical dynamic-buffer element type.
class DynamicBufferLayoutPlan {
private:
    /// User-facing logical record type.
    const Type *logical_type_{nullptr};
    /// Precision policy used to derive the storage type.
    StoragePrecisionPolicy policy_{};
    /// Encoded byte size for one element in canonical storage.
    size_t element_size_bytes_{0u};
    /// Required alignment for one encoded element.
    size_t element_alignment_{0u};
    /// True when the resolved storage type lowers logical precision.
    bool has_precision_lowering_{false};

public:
    DynamicBufferLayoutPlan() = default;
    DynamicBufferLayoutPlan(const Type *logical_type,
                            StoragePrecisionPolicy policy,
                            size_t element_size_bytes,
                            size_t element_alignment,
                            bool has_precision_lowering) noexcept
        : logical_type_(logical_type),
          policy_(policy),
          element_size_bytes_(element_size_bytes),
          element_alignment_(element_alignment),
          has_precision_lowering_(has_precision_lowering) {}

    /// Build a layout plan from the logical type and the storage policy.
    [[nodiscard]] static DynamicBufferLayoutPlan create(const Type *logical_type,
                                                        StoragePrecisionPolicy policy);

    [[nodiscard]] const Type *logical_type() const noexcept { return logical_type_; }
    /// Type that is actually encoded into storage after precision resolution.
    [[nodiscard]] const Type *resolved_type() const noexcept { return Type::resolve(logical_type_, policy_); }
    [[nodiscard]] StoragePrecisionPolicy policy() const noexcept { return policy_; }
    [[nodiscard]] size_t element_size_bytes() const noexcept { return element_size_bytes_; }
    [[nodiscard]] size_t element_alignment() const noexcept { return element_alignment_; }
    [[nodiscard]] bool has_precision_lowering() const noexcept { return has_precision_lowering_; }

    /// Total storage bytes required for the given element count.
    [[nodiscard]] size_t storage_bytes(size_t element_count) const noexcept;

    /// Exact type identity check used by typed host-buffer wrappers.
    [[nodiscard]] bool is_compatible_with(const Type *type) const noexcept {
        return logical_type_ == type;
    }

    /// Continuous byte range for one full logical record.
    [[nodiscard]] ByteRegion record_region(size_t index) const noexcept;
    /// Continuous byte range for one logical subfield within one record.
    [[nodiscard]] ByteRegion field_region(size_t index,
                                          span<const TypedFieldPath::Step> steps) const noexcept;
    [[nodiscard]] ByteRegion field_region(size_t index,
                                          const TypedFieldPath &path) const noexcept {
        return field_region(index, path.steps());
    }
    template<typename... Steps>
    [[nodiscard]] ByteRegion field_region(size_t index,
                                          const StaticTypedFieldPath<Steps...> &path) const noexcept {
        return field_region(index, path.steps());
    }
    /// Storage/staging copy segments for one full record.
    [[nodiscard]] vector<ByteSegment> record_segments(size_t index) const noexcept;
    /// Storage/staging copy segments for one logical subfield.
    [[nodiscard]] vector<ByteSegment> field_segments(size_t index,
                                                     span<const TypedFieldPath::Step> steps) const noexcept;
    [[nodiscard]] vector<ByteSegment> field_segments(size_t index,
                                                     const TypedFieldPath &path) const noexcept {
        return field_segments(index, path.steps());
    }
    template<typename... Steps>
    [[nodiscard]] vector<ByteSegment> field_segments(size_t index,
                                                     const StaticTypedFieldPath<Steps...> &path) const noexcept {
        return field_segments(index, path.steps());
    }
    /// Logical type reached by the given field path.
    [[nodiscard]] const Type *field_logical_type(span<const TypedFieldPath::Step> steps) const noexcept;
    [[nodiscard]] const Type *field_logical_type(const TypedFieldPath &path) const noexcept {
        return field_logical_type(path.steps());
    }
    template<typename... Steps>
    [[nodiscard]] const Type *field_logical_type(const StaticTypedFieldPath<Steps...> &path) const noexcept {
        return field_logical_type(path.steps());
    }
    /// Resolved storage type reached by the given field path.
    [[nodiscard]] const Type *field_resolved_type(span<const TypedFieldPath::Step> steps) const noexcept;
    [[nodiscard]] const Type *field_resolved_type(const TypedFieldPath &path) const noexcept {
        return field_resolved_type(path.steps());
    }
    template<typename... Steps>
    [[nodiscard]] const Type *field_resolved_type(const StaticTypedFieldPath<Steps...> &path) const noexcept {
        return field_resolved_type(path.steps());
    }
};

}// namespace ocarina