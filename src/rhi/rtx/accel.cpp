//
// Created by Z on 2025/11/29.
//

#include "accel.h"

namespace ocarina {

Accel::Impl::Impl(AccelUsageTag usage_tag) noexcept : usage_tag_(usage_tag) {}

void Accel::Impl::add_instance(RHIMesh mesh, float4x4 transform) noexcept {
    meshes_.push_back(ocarina::move(mesh));
    transforms_.push_back(transform);
}

void Accel::Impl::set_transform(size_t index, float4x4 transform) noexcept {
    transforms_[index] = transform;
}

void Accel::Impl::mark_build() noexcept {
    last_build_action_ = BuildAction::BUILD;
    ++build_count_;
}

void Accel::Impl::mark_update() noexcept {
    last_build_action_ = BuildAction::UPDATE;
    ++update_count_;
}

void Accel::Impl::clear() noexcept {
    meshes_.clear();
    transforms_.clear();
    last_build_action_ = BuildAction::NONE;
    build_count_ = 0;
    update_count_ = 0;
}

void Accel::add_instance(RHIMesh mesh, float4x4 transform) noexcept {
    triangle_num_ += mesh.triangle_num();
    vertex_num_ += mesh.vertex_num();
    impl()->add_instance(ocarina::move(mesh), transform);
}

void Accel::set_transform(size_t index, float4x4 transform) noexcept {
    impl()->set_transform(index, transform);
}

void Accel::clear() noexcept {
    if (impl()) {
        impl()->clear();
    }
    triangle_num_ = 0;
    vertex_num_ = 0;
}

}// namespace ocarina