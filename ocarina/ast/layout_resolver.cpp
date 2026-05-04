//
// Created by Z on 2026/4/11.
//

#include "layout_resolver.h"

namespace ocarina {

LayoutResolver::LayoutResolver(StoragePrecisionPolicy policy) noexcept : policy_(policy) {}

const Type *LayoutResolver::resolve(const Type *type) const noexcept {
	const auto desc = resolve_description(type);
	if (desc.empty()) {
		return nullptr;
	}
	return Type::from(desc);
}

string LayoutResolver::resolve_description(const Type *type) const noexcept {
	if (type == nullptr) {
		return {};
	}
	if (type->tag() == Type::Tag::REAL) {
		return resolve_real_description();
	}
	if (type->is_scalar()) {
		return string(type->description());
	}
	if (type->is_vector()) {
		return resolve_vector_description(type);
	}
	if (type->is_matrix()) {
		return resolve_matrix_description(type);
	}
	if (type->is_array()) {
		return resolve_array_description(type);
	}
	if (type->is_buffer()) {
		return resolve_buffer_description(type);
	}
	if (type->is_structure()) {
		return resolve_structure_description(type);
	}
	if (type->is_byte_buffer() || type->is_texture() || type->is_bindless_array() || type->is_accel()) {
		return string(type->description());
	}
	return {};
}

string LayoutResolver::resolve_real_description() const noexcept {
	if (!policy_.allow_real_in_storage) {
		return {};
	}
	switch (policy_.policy) {
		case PrecisionPolicy::force_f16:
			return string(TypeDesc<half>::description());
		case PrecisionPolicy::force_f32:
			return string(TypeDesc<float>::description());
		default:
			return string(TypeDesc<float>::description());
	}
}

const Type *LayoutResolver::resolve_real_container_element(const Type *type) const noexcept {
	const auto *elem = resolve(type);
	if (elem == nullptr) {
		return nullptr;
	}
	return elem;
}

string LayoutResolver::resolve_vector_description(const Type *type) const noexcept {
	OC_ASSERT(type != nullptr && type->is_vector());
	const auto *elem = resolve(type->element());
	if (elem == nullptr) {
		return {};
	}
	return ocarina::format("vector<{},{}>", elem->description(), type->dimension());
}

string LayoutResolver::resolve_matrix_description(const Type *type) const noexcept {
	OC_ASSERT(type != nullptr && type->is_matrix());
	const auto *col = resolve(type->element());
	if (col == nullptr || !col->is_vector()) {
		return {};
	}
	const auto *scalar = col->element();
	if (scalar == nullptr) {
		return {};
	}
	return ocarina::format("matrix<{},{},{}>", scalar->description(), type->dimension(), col->dimension());
}

string LayoutResolver::resolve_array_description(const Type *type) const noexcept {
	OC_ASSERT(type != nullptr && type->is_array());
	const auto *elem = resolve_real_container_element(type->element());
	if (elem == nullptr) {
		return {};
	}
	return ocarina::format("array<{},{}>", elem->description(), type->dimension());
}

string LayoutResolver::resolve_buffer_description(const Type *type) const noexcept {
	OC_ASSERT(type != nullptr && type->is_buffer());
	const auto *elem = resolve_real_container_element(type->element());
	if (elem == nullptr) {
		return {};
	}
	string ret = ocarina::format("buffer<{}>", elem->description());
	return ret;
}

size_t LayoutResolver::resolved_alignment(const Type *type) const noexcept {
	if (type == nullptr) {
		return 0u;
	}
	if (type->tag() == Type::Tag::REAL) {
		const auto *resolved = resolve(type);
		return resolved == nullptr ? 0u : resolved->alignment();
	}
	if (type->is_scalar() || type->is_byte_buffer() || type->is_texture() || type->is_bindless_array() || type->is_accel()) {
		return type->alignment();
	}
	if (type->is_vector() || type->is_matrix() || type->is_array() || type->is_buffer()) {
		const auto *elem = resolve_real_container_element(type->element());
		return elem == nullptr ? 0u : elem->alignment();
	}
	if (type->is_structure()) {
		size_t align = 0u;
		for (const auto *member : type->members()) {
			const size_t member_align = resolved_alignment(member);
			if (member_align == 0u) {
				return 0u;
			}
			align = std::max(align, member_align);
		}
		return align == 0u ? type->alignment() : align;
	}
	return type->alignment();
}

string LayoutResolver::resolve_structure_description(const Type *type) const noexcept {
	OC_ASSERT(type != nullptr && type->is_structure());

	string ret = ocarina::format("struct<{},{},{},{}",
								 type->cname(),
								 resolved_alignment(type),
								 type->is_builtin_struct(),
								 type->is_param_struct());
	for (const auto *member : type->members()) {
		const auto desc = resolve_description(member);
		if (desc.empty()) {
			return {};
		}
		ret.append(",").append(desc);
	}
	ret.push_back('>');
	return ret;
}

}