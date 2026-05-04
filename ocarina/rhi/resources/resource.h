//
// Created by Zero on 03/07/2022.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include "rhi/device.h"

namespace ocarina {

using handle_ty = uint64_t;

class OC_RHI_API RHIResource : public concepts::Noncopyable {
public:
    enum Tag : uint8_t {
        BUFFER,
        TEXTURE3D,
        TEXTURE2D,
        BINDLESS_ARRAY,
        STREAM,
        SHADER,
        MESH,
        ACCEL,
    };

protected:
    Tag tag_{};
    handle_ty handle_{};
    Device::Impl *device_{nullptr};

protected:
    RHIResource(Device::Impl *device, Tag tag, handle_ty handle)
        : device_(device), tag_(tag), handle_(handle) {}

    void _destroy();

public:
    RHIResource() = default;

    RHIResource(RHIResource &&other) noexcept {
        if (&other == this) { return; }
        tag_ = other.tag_;
        device_ = other.device_;
        handle_ = other.handle_;
        other.device_ = nullptr;
    }

    RHIResource &operator=(RHIResource &&other) noexcept {
        if (&other == this) { return *this; }
        _destroy();
        tag_ = other.tag_;
        device_ = other.device_;
        handle_ = other.handle_;
        other.device_ = nullptr;
        return *this;
    }
    [[nodiscard]] virtual const Expression *expression() const noexcept {
        OC_ASSERT(0);
        return nullptr;
    }
    [[nodiscard]] Tag tag() const noexcept { return tag_; }
    [[nodiscard]] virtual handle_ty handle() const noexcept { return handle_; }
    virtual void set_device(Device::Impl *device) noexcept { device_ = device; }
    OC_MAKE_MEMBER_GETTER(device, )
    [[nodiscard]] virtual const void *handle_ptr() const noexcept { return &handle_; }
    [[nodiscard]] virtual void *handle_ptr() noexcept { return &handle_; }
    // size of data on device side
    [[nodiscard]] virtual size_t data_size() const noexcept { return sizeof(handle_ty); }
    // alignment of data on device side
    [[nodiscard]] virtual size_t data_alignment() const noexcept { return sizeof(handle_ty); }
    [[nodiscard]] virtual size_t max_member_size() const noexcept { return sizeof(handle_ty); }
    [[nodiscard]] virtual MemoryBlock memory_block() const noexcept {
        return {handle_ptr(), data_size(), data_alignment(), max_member_size()};
    }
    [[nodiscard]] bool valid() const noexcept { return bool(device_); }
    virtual void destroy() { _destroy(); }
    virtual ~RHIResource() { _destroy(); }
};

class ExportableResource : public RHIResource {
protected:
    bool exported_{true};
    explicit ExportableResource(Device::Impl *device, Tag tag,
                                handle_ty handle, bool exported)
        : RHIResource(device, tag, handle), exported_(exported) {}

public:
    using RHIResource::RHIResource;

    struct Data {
        handle_ty handle;
        size_t size;
    };

#if _WIN32 || _WIN64
    virtual void import_handle(uint64_t handle) { OC_ASSERT(0); };
    virtual uint64_t export_handle() {
        OC_ASSERT(0);
        return 0;
    };
#endif
    OC_MAKE_MEMBER_GETTER(exported, )
};

}// namespace ocarina