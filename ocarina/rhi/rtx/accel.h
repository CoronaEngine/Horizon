//
// Created by Zero on 2022/8/8.
//

#pragma once

#include "rhi/resources/resource.h"
#include "rhi/command.h"
#include "mesh.h"
#include "dsl/core/var.h"

namespace ocarina {
class OC_RHI_API Accel : public RHIResource {
private:
    uint triangle_num_{};
    uint vertex_num_{};

public:
    enum class BuildAction : uint {
        NONE,
        BUILD,
        UPDATE
    };

    class Impl {
    protected:
        AccelUsageTag usage_tag_{FAST_TRACE};
        vector<RHIMesh> meshes_;
        vector<float4x4> transforms_;
        BuildAction last_build_action_{BuildAction::NONE};
        uint build_count_{};
        uint update_count_{};

    public:
        explicit OC_RHI_API Impl(AccelUsageTag usage_tag = FAST_TRACE) noexcept;
        Impl(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl &operator=(Impl &&) = delete;
        virtual OC_RHI_API void add_instance(RHIMesh mesh, float4x4 transform) noexcept;
        virtual OC_RHI_API void set_transform(size_t index, float4x4 transform) noexcept;
        OC_RHI_API void mark_build() noexcept;
        OC_RHI_API void mark_update() noexcept;
        [[nodiscard]] AccelUsageTag usage_tag() const noexcept { return usage_tag_; }
        [[nodiscard]] BuildAction last_build_action() const noexcept { return last_build_action_; }
        [[nodiscard]] uint build_count() const noexcept { return build_count_; }
        [[nodiscard]] uint update_count() const noexcept { return update_count_; }
        [[nodiscard]] virtual handle_ty handle() const noexcept = 0;
        [[nodiscard]] virtual const void *handle_ptr() const noexcept = 0;
        [[nodiscard]] size_t mesh_num() const noexcept { return meshes_.size(); }
        [[nodiscard]] virtual size_t data_size() const noexcept = 0;
        virtual OC_RHI_API void clear() noexcept;
        [[nodiscard]] virtual size_t data_alignment() const noexcept = 0;
    };

public:
    Accel() = default;
    explicit Accel(Device::Impl *device, AccelUsageTag usage_tag = FAST_TRACE)
        : RHIResource(device, Tag::ACCEL, device->create_accel(usage_tag)) {}
    [[nodiscard]] uint triangle_num() const noexcept { return triangle_num_; }
    [[nodiscard]] uint vertex_num() const noexcept { return vertex_num_; }
    [[nodiscard]] Impl *impl() noexcept { return reinterpret_cast<Impl *>(handle_); }
    [[nodiscard]] const Impl *impl() const noexcept { return reinterpret_cast<const Impl *>(handle_); }

    void add_instance(RHIMesh mesh, float4x4 transform) noexcept;

    void set_transform(size_t index, float4x4 transform) noexcept;

    [[nodiscard]] size_t mesh_num() const noexcept { return impl()->mesh_num(); }
    [[nodiscard]] AccelUsageTag usage_tag() const noexcept { return impl()->usage_tag(); }
    [[nodiscard]] BuildAction last_build_action() const noexcept { return impl()->last_build_action(); }
    [[nodiscard]] uint build_count() const noexcept { return impl()->build_count(); }
    [[nodiscard]] uint update_count() const noexcept { return impl()->update_count(); }

    void clear() noexcept;

    [[nodiscard]] const Expression *expression() const noexcept override {
        const CapturedResource &captured_resource = Function::current()->get_captured_resource(Type::of<decltype(*this)>(),
                                                                               Variable::Tag::ACCEL,
                                                                               memory_block());
        return captured_resource.expression();
    }

    template<typename TRay>
    [[nodiscard]] Var<bool> trace_occlusion(const TRay &ray) const noexcept {
        return make_expr<Accel>(expression()).trace_occlusion(ray);
    }

    template<typename TRay>
    [[nodiscard]] Var<TriangleHit> trace_closest(const TRay &ray) const noexcept {
        return make_expr<Accel>(expression()).trace_closest(ray);
    }

    [[nodiscard]] handle_ty handle() const noexcept override { return impl()->handle(); }
    [[nodiscard]] const void *handle_ptr() const noexcept override { return impl()->handle_ptr(); }
    [[nodiscard]] TLASBuildCommand *build_bvh() noexcept {
        return TLASBuildCommand::create(handle_);
    }
    [[nodiscard]] TLASUpdateCommand *update_bvh() noexcept {
        return TLASUpdateCommand::create(handle_);
    }
};
}// namespace ocarina
