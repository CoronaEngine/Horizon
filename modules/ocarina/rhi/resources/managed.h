//
// Created by Zero on 24/10/2022.
//

#pragma once

#include "core/stl.h"
#include "buffer.h"
#include "texture.h"
#include "counted_buffer.h"
#include "core/image/image.h"

namespace ocarina {

template<typename T>
class DynamicArray;

template<typename T>
class Managed : public Buffer<T>, public ocarina::vector<T> {
public:
    using host_ty = ocarina::vector<T>;
    using device_ty = Buffer<T>;
    using element_ty = T;

public:
    Managed() = default;
    Managed(Device::Impl *device, size_t size, const string &desc = "")
        : device_ty(device, size, desc) {
        host_ty::reserve(size);
    }

    explicit Managed(size_t size) {
        host_ty::reserve(size);
    }

    // Move constructor
    Managed(Managed &&other) noexcept
        : device_ty(std::move(other)),
          host_ty(std::move(other)) {}

    // Move assignment
    Managed &operator=(Managed &&other) noexcept {
        device_ty::operator=(std::move(other));
        host_ty::operator=(std::move(other));
        return *this;
    }

    [[nodiscard]] device_ty &device_buffer() noexcept { return *this; }
    [[nodiscard]] const device_ty &device_buffer() const noexcept { return *this; }
    [[nodiscard]] host_ty &host_buffer() noexcept { return *this; }
    [[nodiscard]] const host_ty &host_buffer() const noexcept { return *this; }
    void set_host(host_ty val) noexcept { host_buffer() = std::move(val); }
    [[nodiscard]] const T *operator->() const { return host_ty::data(); }
    [[nodiscard]] T *operator->() { return host_ty::data(); }

    template<typename Index>
    requires concepts::integral<Index>
    [[nodiscard]] auto operator[](Index &&i) { return host_ty::operator[](OC_FORWARD(i)); }

    template<typename V>
    requires concepts::iterable<V>
    void append(V &&v) {
        host_ty::insert(host_ty::cend(), v.cbegin(), v.cend());
    }

    void reset_device_buffer_immediately(Device &d, const string &desc = "", size_t num = 0) {
        num = num == 0 ? host_ty ::size() : num;
        if (num == 0) {
            device_buffer() = {};
            return;
        }
        device_buffer() = d.template create_buffer<T>(num, desc);
    }

    /**
     * reset host and device memory
     * @param d device
     * @param num number of element
     */
    void reset_all(Device &d, size_t num, const string &desc = "") {
        reset_device_buffer_immediately(d, desc, num);
        host_buffer().resize(num);
    }

    void clear_all() {
        host_ty::clear();
        device_ty::destroy();
    }

    [[nodiscard]] BufferUploadCommand *upload(bool async = true) const noexcept {
        return device_ty::upload(host_ty::data(), async);
    }

    [[nodiscard]] BufferDownloadCommand *download(size_t offset = 0, size_t size = 0) noexcept {
        return device_buffer().view(offset, size).download(host_ty::data() + offset);
    }

    void upload_immediately() const noexcept {
        device_ty::upload_immediately(host_ty::data());
    }

    void download_immediately() noexcept {
        device_ty::download_immediately(host_ty::data());
    }

    [[nodiscard]] BufferUploadCommand *upload_sync() const noexcept {
        return device_ty::upload_sync(host_ty::data());
    }

    [[nodiscard]] BufferDownloadCommand *download_sync() noexcept {
        return device_ty::download_sync(host_ty::data());
    }
};

class ManagedTexture3D : public Texture3D, public Image {
public:
    using host_ty = ocarina::Image;
    using device_ty = ocarina::Texture3D;

public:
    ManagedTexture3D() = default;

    [[nodiscard]] device_ty &device_tex() noexcept { return *this; }
    [[nodiscard]] const device_ty &device_tex() const noexcept { return *this; }
    [[nodiscard]] host_ty &host_tex() noexcept { return *this; }
    [[nodiscard]] const host_ty &host_tex() const noexcept { return *this; }

    void allocate_on_device(Device &device, const string &name = "") noexcept {
        device_tex() = device.create_texture3d(Image::resolution(),
                                               Image::pixel_storage(),
                                               name);
    }

    void upload_immediately() const noexcept {
        device_ty::upload_immediately(pixel_ptr());
    }

    void download_immediately() noexcept {
        device_ty::download_immediately(pixel_ptr());
    }

    [[nodiscard]] TextureOpCommand *upload(bool async = true) const noexcept {
        return device_ty ::upload(pixel_ptr(), async);
    }

    [[nodiscard]] TextureOpCommand *download(bool async = true) noexcept {
        return device_ty::download(pixel_ptr(), async);
    }
};

template<typename T, AccessMode mode>
class ManagedList : public CountedBuffer<T, mode, ByteBuffer>, public vector<T> {
public:
    using host_ty = ocarina::vector<T>;
    using device_ty = CountedBuffer<T, mode, ByteBuffer>;
    using element_ty = T;

public:
    ManagedList() = default;
    explicit ManagedList(device_ty list) : device_ty(std::move(list)) {
        host_list().resize(device_list().capacity(), T{});
    }

    // Move constructor
    ManagedList(ManagedList &&other) noexcept
        : device_ty(std::move(other)),
          host_ty(std::move(other)) {}

    // Move assignment
    ManagedList &operator=(ManagedList &&other) noexcept {
        device_ty::operator=(std::move(other));
        host_ty::operator=(std::move(other));
        return *this;
    }

    [[nodiscard]] BufferUploadCommand *upload(bool async = true) const noexcept {
        return device_ty::storage_segment().upload(host_ty::data(), async);
    }

    [[nodiscard]] BufferDownloadCommand *download(bool async = true) noexcept {
        return device_ty::storage_segment().download(host_ty::data(), 0, async);
    }

    void upload_immediately() const noexcept {
        upload(false)->accept(*device_ty::buffer().device()->command_visitor());
    }

    void download_immediately() noexcept {
        download(false)->accept(*device_ty::buffer().device()->command_visitor());
    }

    [[nodiscard]] device_ty &device_list() noexcept { return *this; }
    [[nodiscard]] const device_ty &device_list() const noexcept { return *this; }
    [[nodiscard]] host_ty &host_list() noexcept { return *this; }
    [[nodiscard]] const host_ty &host_list() const noexcept { return *this; }

    void set_host(host_ty val) noexcept { host_list() = std::move(val); }
    [[nodiscard]] const T *operator->() const { return host_ty::data(); }
    [[nodiscard]] T *operator->() { return host_ty::data(); }

    template<typename Index>
    requires concepts::integral<Index>
    [[nodiscard]] auto operator[](Index &&i) { return host_ty::operator[](OC_FORWARD(i)); }
};

}// namespace ocarina