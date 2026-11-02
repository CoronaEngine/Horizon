//
// Created by Z on 2026/4/13.
//

#pragma once

#include "core/dynamic_buffer/host_dynamic_buffer.h"
#include "rhi/command.h"
#include "rhi/resources/byte_buffer.h"

namespace ocarina {

namespace detail {

template<typename T>
[[nodiscard]] shared_ptr<HostByteBuffer> encode_dynamic_buffer_values(span<const T> values,
                                                                      StoragePrecisionPolicy policy,
                                                                      DynamicBufferLayout layout = DynamicBufferLayout::AOS) {
    auto bytes = make_shared<HostByteBuffer>();
    DynamicBufferLayoutCodec<T>::encode(values.data(),
                                        values.size(),
                                        *bytes,
                                        policy,
                                        layout);
    return bytes;
}

template<typename T>
[[nodiscard]] shared_ptr<HostByteBuffer> allocate_dynamic_buffer_bytes(size_t count,
                                                                       StoragePrecisionPolicy policy,
                                                                       DynamicBufferLayout layout = DynamicBufferLayout::AOS) {
    auto bytes = make_shared<HostByteBuffer>();
    bytes->resize(DynamicBufferLayoutCodec<T>::storage_bytes(count,
                                                             policy,
                                                             layout));
    return bytes;
}

template<typename T>
void decode_dynamic_buffer_values(const HostByteBuffer &bytes,
                                  size_t count,
                                  T *dst,
                                  StoragePrecisionPolicy policy,
                                  DynamicBufferLayout layout = DynamicBufferLayout::AOS) {
    DynamicBufferLayoutCodec<T>::decode(bytes,
                                        count,
                                        dst,
                                        policy,
                                        layout);
}

}// namespace detail

struct DynamicBufferUploadStats {
    size_t uploaded_segment_count{0u};
    size_t uploaded_bytes{0u};
    bool full_upload{false};
};

template<typename T>
class DynamicBuffer;

template<typename T>
class DynamicBufferView;

namespace detail {

class RawDynamicBuffer;
class RawDynamicBufferView;

class RawDynamicBufferView {
private:
    ByteBufferView byte_view_{};
    size_t element_offset_{0u};
    size_t element_count_{0u};
    size_t total_element_count_{0u};
    size_t element_stride_{0u};
    size_t element_alignment_{0u};
    DynamicBufferLayout layout_{DynamicBufferLayout::AOS};
    StoragePrecisionPolicy policy_{};
    const Type *type_{nullptr};
    bool type_is_resolved_{false};

public:
    RawDynamicBufferView() = default;
    RawDynamicBufferView(ByteBufferView byte_view,
                         size_t element_offset,
                         size_t element_count,
                         size_t total_element_count,
                         size_t element_stride,
                         size_t element_alignment,
                         DynamicBufferLayout layout,
                         const Type *type,
                         bool type_is_resolved,
                         StoragePrecisionPolicy policy) noexcept
        : byte_view_(byte_view),
          element_offset_(element_offset),
          element_count_(element_count),
          total_element_count_(total_element_count),
          element_stride_(element_stride),
          element_alignment_(element_alignment),
          layout_(layout),
          policy_(policy),
          type_(type),
          type_is_resolved_(type_is_resolved) {}

    [[nodiscard]] handle_ty handle() const noexcept { return byte_view_.handle(); }
    [[nodiscard]] size_t size() const noexcept { return element_count_; }
    [[nodiscard]] size_t size_in_byte() const noexcept { return byte_view_.size(); }
    [[nodiscard]] size_t offset() const noexcept { return element_offset_; }
    [[nodiscard]] size_t offset_in_byte() const noexcept { return byte_view_.offset(); }
    [[nodiscard]] size_t total_size() const noexcept { return total_element_count_; }
    [[nodiscard]] size_t total_size_in_byte() const noexcept { return byte_view_.total_size(); }
    [[nodiscard]] size_t element_stride() const noexcept { return element_stride_; }
    [[nodiscard]] size_t element_alignment() const noexcept { return element_alignment_; }
    [[nodiscard]] DynamicBufferLayout layout() const noexcept { return layout_; }
    [[nodiscard]] StoragePrecisionPolicy policy() const noexcept { return policy_; }
    [[nodiscard]] const Type *logical_type() const noexcept { return type_is_resolved_ ? nullptr : type_; }
    [[nodiscard]] const Type *resolved_type() const noexcept {
        return type_is_resolved_ ? type_ : Type::resolve(type_, policy_);
    }
    [[nodiscard]] bool empty() const noexcept { return element_count_ == 0u; }

    [[nodiscard]] ByteBufferView byte_view() const noexcept {
        return ByteBufferView(handle(), offset_in_byte(), size_in_byte(), total_size_in_byte());
    }

    template<typename T>
    [[nodiscard]] BufferView<T> aos_view() const noexcept {
        OC_ASSERT(layout_ == DynamicBufferLayout::AOS);
        OC_ASSERT(resolved_type() == Type::of<T>());
        OC_ASSERT(element_stride_ == sizeof(T));
        return byte_view().template view_as<T>();
    }

    [[nodiscard]] RawDynamicBufferView subview(size_t offset, size_t size = 0u) const noexcept {
        OC_ASSERT(layout_ == DynamicBufferLayout::AOS);
        size = size == 0u ? element_count_ - offset : size;
        return RawDynamicBufferView{ByteBufferView(handle(),
                                                   offset_in_byte() + offset * element_stride_,
                                                   size * element_stride_,
                                                   total_size_in_byte()),
                                    element_offset_ + offset,
                                    size,
                                    total_element_count_,
                                    element_stride_,
                                    element_alignment_,
                                    layout_,
                                    type_,
                                    type_is_resolved_,
                                    policy_};
    }

    template<typename Arg>
    requires is_buffer_or_view_v<Arg> && std::is_same_v<buffer_element_t<Arg>, std::byte>
    [[nodiscard]] BufferCopyCommand *copy_from(const Arg &src, uint dst_offset = 0u) const noexcept {
        OC_ASSERT(layout_ == DynamicBufferLayout::AOS);
        return BufferCopyCommand::create(src.handle(), handle(),
                                         src.offset_in_byte(),
                                         offset_in_byte() + dst_offset,
                                         src.size_in_byte(), true);
    }

    template<typename Arg>
    requires is_buffer_or_view_v<Arg> && std::is_same_v<buffer_element_t<Arg>, std::byte>
    [[nodiscard]] BufferCopyCommand *copy_to(Arg &dst, uint src_offset = 0u) const noexcept {
        OC_ASSERT(layout_ == DynamicBufferLayout::AOS);
        return BufferCopyCommand::create(handle(), dst.handle(),
                                         offset_in_byte() + src_offset,
                                         dst.offset_in_byte(),
                                         dst.size_in_byte(), true);
    }

    [[nodiscard]] BufferUploadCommand *upload(const void *data, bool async = true) const noexcept {
        return BufferUploadCommand::create(data, handle(), offset_in_byte(), size_in_byte(), async);
    }

    [[nodiscard]] BufferUploadCommand *upload_sync(const void *data) const noexcept {
        return upload(data, false);
    }

    [[nodiscard]] BufferDownloadCommand *download(void *data, bool async = true) const noexcept {
        return BufferDownloadCommand::create(data, handle(), offset_in_byte(), size_in_byte(), async);
    }

    [[nodiscard]] BufferDownloadCommand *download_sync(void *data) const noexcept {
        return download(data, false);
    }

    [[nodiscard]] BufferByteSetCommand *byte_set(uchar value, bool async = true) const noexcept {
        return BufferByteSetCommand::create(handle(), size_in_byte(), value, async);
    }

    [[nodiscard]] BufferByteSetCommand *reset(bool async = true) const noexcept {
        return byte_set(0u, async);
    }
};

class RawDynamicBuffer {
private:
    Device::Impl *device_{nullptr};
    ByteBuffer byte_buffer_{};
    const Type *type_{nullptr};
    bool type_is_resolved_{false};
    StoragePrecisionPolicy policy_{};
    DynamicBufferLayout layout_{DynamicBufferLayout::AOS};
    size_t element_stride_{0u};
    size_t element_alignment_{0u};
    size_t element_count_{0u};
    size_t element_capacity_{0u};
    string name_{};

private:
    [[nodiscard]] size_t storage_bytes_for_count(size_t element_count) const noexcept {
        const auto *resolved = resolved_type();
        if (resolved == nullptr) {
            return 0u;
        }
        if (layout_ == DynamicBufferLayout::AOS) {
            return resolved->size() * element_count;
        }
        return detail::runtime_soa_storage_bytes(resolved, element_count, policy_);
    }

    void assign_layout_plan(const DynamicBufferLayoutPlan &layout_plan,
                            DynamicBufferLayout layout) noexcept {
        type_ = layout_plan.logical_type();
        type_is_resolved_ = false;
        policy_ = layout_plan.policy();
        layout_ = layout;
        element_stride_ = layout_plan.element_size_bytes();
        element_alignment_ = layout_plan.element_alignment();
    }

    void assign_resolved_layout(const Type *resolved_type,
                                DynamicBufferLayout layout) noexcept {
        type_ = resolved_type;
        type_is_resolved_ = true;
        policy_ = StoragePrecisionPolicy{.policy = PrecisionPolicy::force_f32, .allow_real_in_storage = true};
        layout_ = layout;
        element_stride_ = resolved_type->size();
        element_alignment_ = resolved_type->alignment();
    }

    void validate_byte_size(size_t byte_size) const noexcept {
        OC_ASSERT(layout_ == DynamicBufferLayout::AOS);
        OC_ASSERT(element_stride_ > 0u || byte_size == 0u);
        OC_ASSERT(element_stride_ == 0u || byte_size % element_stride_ == 0u);
    }

    [[nodiscard]] bool is_compatible_with(const HostDynamicBufferUploadView &view) const noexcept {
        return layout() == view.layout &&
               logical_type() == view.logical_type &&
               resolved_type() == view.resolved_type() &&
               policy().policy == view.policy.policy &&
               policy().allow_real_in_storage == view.policy.allow_real_in_storage;
    }

    void reserve_for_count(size_t required_count) noexcept {
        if (required_count <= element_capacity_) {
            return;
        }
        auto *device = this->device();
        OC_ASSERT(device != nullptr);
        ByteBuffer new_buffer{device, storage_bytes_for_count(required_count), name_};
        if (valid() && size_in_byte() != 0u) {
            auto *copy = BufferCopyCommand::create(handle(), new_buffer.handle(), 0u, 0u, size_in_byte(), false);
            copy->accept(*device->command_visitor());
        }
        if (byte_buffer_.valid()) {
            byte_buffer_.destroy();
        }
        byte_buffer_ = std::move(new_buffer);
        element_capacity_ = required_count;
    }

public:
    RawDynamicBuffer() = default;

    RawDynamicBuffer(Device::Impl *device,
                     const Type *logical_type,
                     StoragePrecisionPolicy policy,
                     size_t element_count,
                     DynamicBufferLayout layout = DynamicBufferLayout::AOS,
                     const string &name = "")
        : RawDynamicBuffer(device, DynamicBufferLayoutPlan::create(logical_type, policy), element_count, layout, name) {}

    RawDynamicBuffer(Device::Impl *device,
                     const Type *resolved_type,
                     size_t element_count,
                     DynamicBufferLayout layout = DynamicBufferLayout::AOS,
                     const string &name = "")
        : device_(device),
          byte_buffer_(element_count == 0u ?
                           ByteBuffer{} :
                           ByteBuffer(device,
                                      layout == DynamicBufferLayout::AOS ?
                                          resolved_type->size() * element_count :
                                          detail::runtime_soa_storage_bytes(resolved_type, element_count,
                                                                            StoragePrecisionPolicy{.policy = PrecisionPolicy::force_f32,
                                                                                                   .allow_real_in_storage = true}),
                                      name)),
          element_count_(element_count),
          element_capacity_(element_count),
          name_(name) {
        assign_resolved_layout(resolved_type, layout);
    }

    RawDynamicBuffer(Device::Impl *device,
                     const DynamicBufferLayoutPlan &layout_plan,
                     size_t element_count,
                     DynamicBufferLayout layout = DynamicBufferLayout::AOS,
                     const string &name = "")
        : device_(device),
          byte_buffer_(element_count == 0u ?
                           ByteBuffer{} :
                           ByteBuffer(device,
                                      layout == DynamicBufferLayout::AOS ?
                                          layout_plan.storage_bytes(element_count) :
                                          detail::runtime_soa_storage_bytes(layout_plan.resolved_type(),
                                                                            element_count,
                                                                            layout_plan.policy()),
                                      name)),
          element_count_(element_count),
          element_capacity_(element_count),
          name_(name) {
        assign_layout_plan(layout_plan, layout);
    }

    [[nodiscard]] bool valid() const noexcept { return byte_buffer_.valid(); }
    [[nodiscard]] handle_ty handle() const noexcept { return byte_buffer_.handle(); }
    [[nodiscard]] Device::Impl *device() const noexcept {
        return byte_buffer_.valid() ? byte_buffer_.device() : device_;
    }
    [[nodiscard]] const string &name() const noexcept { return name_; }
    void set_name(string name) noexcept { name_ = std::move(name); }
    [[nodiscard]] const Type *logical_type() const noexcept { return type_is_resolved_ ? nullptr : type_; }
    [[nodiscard]] const Type *resolved_type() const noexcept {
        return type_is_resolved_ ? type_ : Type::resolve(type_, policy_);
    }
    [[nodiscard]] StoragePrecisionPolicy policy() const noexcept { return policy_; }
    [[nodiscard]] DynamicBufferLayout layout() const noexcept { return layout_; }
    [[nodiscard]] size_t element_stride() const noexcept { return element_stride_; }
    [[nodiscard]] size_t element_alignment() const noexcept { return element_alignment_; }
    [[nodiscard]] size_t size() const noexcept { return element_count_; }
    [[nodiscard]] size_t capacity() const noexcept { return element_capacity_; }
    [[nodiscard]] bool empty() const noexcept { return element_count_ == 0u; }
    [[nodiscard]] size_t size_in_byte() const noexcept { return storage_bytes_for_count(element_count_); }
    [[nodiscard]] size_t capacity_in_byte() const noexcept { return storage_bytes_for_count(element_capacity_); }
    [[nodiscard]] uint offset_in_byte() const noexcept { return 0u; }

    void destroy() noexcept {
        byte_buffer_.destroy();
        element_count_ = 0u;
        element_capacity_ = 0u;
    }

    void reset_layout(const Type *logical_type,
                      StoragePrecisionPolicy policy,
                      DynamicBufferLayout layout = DynamicBufferLayout::AOS) noexcept {
        const auto layout_plan = DynamicBufferLayoutPlan::create(logical_type, policy);
        destroy();
        assign_layout_plan(layout_plan, layout);
    }

    void reserve(size_t element_capacity) noexcept {
        reserve_for_count(element_capacity);
    }

    [[nodiscard]] ByteBufferView byte_view(size_t offset_in_byte = 0u, size_t size_in_byte = 0u) const noexcept {
        size_in_byte = size_in_byte == 0u ? this->size_in_byte() - offset_in_byte : size_in_byte;
        return ByteBufferView(handle(), offset_in_byte, size_in_byte, this->size_in_byte());
    }

    [[nodiscard]] RawDynamicBufferView view(size_t offset = 0u, size_t size = 0u) const noexcept {
        if (layout_ != DynamicBufferLayout::AOS) {
            OC_ASSERT(offset == 0u);
            OC_ASSERT(size == 0u || size == element_count_);
            return RawDynamicBufferView{byte_view(),
                                        0u,
                                        element_count_,
                                        element_count_,
                                        element_stride_,
                                        element_alignment_,
                                        layout_,
                                        type_,
                                        type_is_resolved_,
                                        policy_};
        }
        size = size == 0u ? element_count_ - offset : size;
        return RawDynamicBufferView{byte_view(offset * element_stride_, size * element_stride_),
                                    offset,
                                    size,
                                    element_count_,
                                    element_stride_,
                                    element_alignment_,
                                    layout_,
                                    type_,
                                    type_is_resolved_,
                                    policy_};
    }

    [[nodiscard]] RawDynamicBufferView subview(size_t offset, size_t size = 0u) const noexcept {
        return view().subview(offset, size);
    }

    template<typename T>
    [[nodiscard]] BufferView<T> aos_view() const noexcept {
        OC_ASSERT(layout_ == DynamicBufferLayout::AOS);
        return view().template aos_view<T>();
    }

    [[nodiscard]] const Expression *expression() const noexcept {
        return byte_buffer_.expression();
    }

    [[nodiscard]] Expr<ByteBuffer> expr() const noexcept {
        return byte_buffer_.expr();
    }

    template<typename Target, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] Var<Target> load_as(Offset &&offset) const noexcept {
        return byte_buffer_.template load_as<Target>(OC_FORWARD(offset));
    }

    template<typename Elm, typename Offset>
    requires is_integral_expr_v<Offset>
    void store(Offset &&offset, const Elm &val) noexcept {
        byte_buffer_.store(OC_FORWARD(offset), val);
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] auto aos_view_var() const noexcept {
        return byte_buffer_.template aos_view_var<Elm>();
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] auto soa_view_var() const noexcept {
        return byte_buffer_.template soa_view_var<Elm>();
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] auto aos_view_var(const Var<int_type> &view_size) const noexcept {
        return byte_buffer_.template aos_view_var<Elm>(view_size);
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] auto soa_view_var(const Var<int_type> &view_size) const noexcept {
        return byte_buffer_.template soa_view_var<Elm>(view_size);
    }

    [[nodiscard]] CommandBatch upload(const void *data,
                                      size_t byte_size,
                                      size_t element_count,
                                      bool async = true) noexcept {
        OC_ASSERT(byte_size == storage_bytes_for_count(element_count));
        reserve_for_count(element_count);
        element_count_ = element_count;
        if (byte_size == 0u) {
            return {};
        }
        return {BufferUploadCommand::create(data, handle(), 0u, byte_size, async)};
    }

    [[nodiscard]] CommandBatch upload(const void *data, size_t byte_size, bool async = true) noexcept {
        validate_byte_size(byte_size);
        const auto required_count = element_stride_ == 0u ? 0u : byte_size / element_stride_;
        return upload(data, byte_size, required_count, async);
    }

    [[nodiscard]] CommandBatch upload(span<const std::byte> bytes, bool async = true) noexcept {
        OC_ASSERT(layout_ == DynamicBufferLayout::AOS);
        return upload(bytes.data(), bytes.size(), async);
    }

    [[nodiscard]] CommandBatch upload(span<const std::byte> bytes,
                                      size_t element_count,
                                      bool async = true) noexcept {
        return upload(bytes.data(), bytes.size(), element_count, async);
    }

    void upload_immediately(const void *data, size_t byte_size) noexcept {
        auto commands = upload(data, byte_size, false);
        commands.accept(*device()->command_visitor());
    }

    void upload_immediately(const void *data,
                            size_t byte_size,
                            size_t element_count) noexcept {
        auto commands = upload(data, byte_size, element_count, false);
        commands.accept(*device()->command_visitor());
    }

    void upload_immediately(span<const std::byte> bytes) noexcept {
        upload_immediately(bytes.data(), bytes.size());
    }

    void upload_immediately(span<const std::byte> bytes,
                            size_t element_count) noexcept {
        upload_immediately(bytes.data(), bytes.size(), element_count);
    }

    [[nodiscard]] bool needs_recreate(const HostDynamicBufferUploadView &view) const noexcept {
        return !valid() ||
               view.bytes.size() > capacity_in_byte() ||
               !is_compatible_with(view);
    }

    [[nodiscard]] DynamicBufferUploadStats sync_immediately(const HostDynamicBufferUploadView &view,
                                                            bool force_full_upload = false) noexcept {
        DynamicBufferUploadStats stats;
        if (view.logical_type != nullptr && !is_compatible_with(view)) {
            reset_layout(view.logical_type, view.policy, view.layout);
        }
        if (view.bytes.empty()) {
            destroy();
            return stats;
        }
        const bool full_upload_required = force_full_upload || needs_recreate(view) || view.dirty_segments.empty();
        reserve(view.element_count);
        if (full_upload_required) {
            upload_immediately(view.bytes, view.element_count);
            stats.full_upload = true;
            stats.uploaded_segment_count = view.bytes.empty() ? 0u : 1u;
            stats.uploaded_bytes = view.bytes.size();
            return stats;
        }
        element_count_ = view.element_count;
        for (const auto &segment : view.dirty_segments) {
            if (segment.empty()) {
                continue;
            }
            auto upload_cmd = byte_view(segment.begin_byte, segment.size())
                                  .upload_sync(view.bytes.data() + segment.begin_byte);
            upload_cmd->accept(*device()->command_visitor());
            stats.uploaded_segment_count++;
            stats.uploaded_bytes += segment.size();
        }
        return stats;
    }

    template<typename T>
    [[nodiscard]] DynamicBufferUploadStats sync_immediately(HostDynamicBuffer<T> &buffer,
                                                            bool force_full_upload = false) noexcept {
        const auto view = buffer.upload_view();
        auto stats = sync_immediately(view, force_full_upload);
        buffer.clear_dirty();
        return stats;
    }

    [[nodiscard]] BufferDownloadCommand *download(void *data, bool async = true) const noexcept {
        return BufferDownloadCommand::create(data, handle(), 0u, size_in_byte(), async);
    }

    void download_immediately(void *data) const noexcept {
        download(data, false)->accept(*device()->command_visitor());
    }

    [[nodiscard]] BufferByteSetCommand *byte_set(uchar value, bool async = true) const noexcept {
        return BufferByteSetCommand::create(handle(), size_in_byte(), value, async);
    }

    [[nodiscard]] BufferByteSetCommand *reset(bool async = true) const noexcept {
        return byte_set(0u, async);
    }

    template<typename Arg>
    requires is_buffer_or_view_v<Arg> && std::is_same_v<buffer_element_t<Arg>, std::byte>
    [[nodiscard]] BufferCopyCommand *copy_from(const Arg &src, uint dst_offset = 0u) const noexcept {
        return BufferCopyCommand::create(src.handle(), handle(),
                                         src.offset_in_byte(),
                                         dst_offset,
                                         src.size_in_byte(), true);
    }

    template<typename Arg>
    requires is_buffer_or_view_v<Arg> && std::is_same_v<buffer_element_t<Arg>, std::byte>
    [[nodiscard]] BufferCopyCommand *copy_to(Arg &dst, uint src_offset = 0u) const noexcept {
        return BufferCopyCommand::create(handle(), dst.handle(),
                                         src_offset,
                                         dst.offset_in_byte(),
                                         size_in_byte(), true);
    }
};

}// namespace detail

template<typename T>
class DynamicBufferView {
private:
    detail::RawDynamicBufferView view_{};

private:
    [[nodiscard]] size_t aos_byte_offset(size_t element_offset) const noexcept {
        OC_ASSERT(layout() == DynamicBufferLayout::AOS);
        return offset_in_byte() + element_offset * element_stride();
    }

public:
    DynamicBufferView() = default;

private:
    explicit DynamicBufferView(detail::RawDynamicBufferView view) noexcept
        : view_(std::move(view)) {
        OC_ASSERT(view_.logical_type() == nullptr || view_.logical_type() == Type::of<T>());
    }

    template<typename>
    friend class DynamicBuffer;

public:
    [[nodiscard]] handle_ty handle() const noexcept { return view_.handle(); }
    [[nodiscard]] size_t size() const noexcept { return view_.size(); }
    [[nodiscard]] size_t size_in_byte() const noexcept { return view_.size_in_byte(); }
    [[nodiscard]] size_t offset() const noexcept { return view_.offset(); }
    [[nodiscard]] size_t offset_in_byte() const noexcept { return view_.offset_in_byte(); }
    [[nodiscard]] size_t total_size() const noexcept { return view_.total_size(); }
    [[nodiscard]] size_t total_size_in_byte() const noexcept { return view_.total_size_in_byte(); }
    [[nodiscard]] size_t element_stride() const noexcept { return view_.element_stride(); }
    [[nodiscard]] size_t element_alignment() const noexcept { return view_.element_alignment(); }
    [[nodiscard]] DynamicBufferLayout layout() const noexcept { return view_.layout(); }
    [[nodiscard]] StoragePrecisionPolicy policy() const noexcept { return view_.policy(); }
    [[nodiscard]] const Type *logical_type() const noexcept { return view_.logical_type(); }
    [[nodiscard]] const Type *resolved_type() const noexcept { return view_.resolved_type(); }
    [[nodiscard]] bool empty() const noexcept { return view_.empty(); }

    [[nodiscard]] ByteBufferView byte_view() const noexcept { return view_.byte_view(); }
    [[nodiscard]] DynamicBufferView subview(size_t offset, size_t size = 0u) const noexcept {
        return DynamicBufferView{view_.subview(offset, size)};
    }

    template<typename Arg>
    requires is_buffer_or_view_v<Arg>
    [[nodiscard]] BufferCopyCommand *copy_from(const Arg &src, uint dst_offset = 0u) const noexcept {
        return BufferCopyCommand::create(src.handle(), handle(),
                                         src.offset_in_byte(),
                                         aos_byte_offset(dst_offset),
                                         src.size_in_byte(), true);
    }

    template<typename Arg>
    requires is_buffer_or_view_v<Arg>
    [[nodiscard]] BufferCopyCommand *copy_to(Arg &dst, uint src_offset = 0u) const noexcept {
        return BufferCopyCommand::create(handle(), dst.handle(),
                                         aos_byte_offset(src_offset),
                                         dst.offset_in_byte(),
                                         dst.size_in_byte(), true);
    }

    [[nodiscard]] CommandBatch upload(const T *data,
                                      bool async = true,
                                      DynamicBufferLayout layout = DynamicBufferLayout::AOS) const {
        return upload(span<const T>{data, size()}, async, layout);
    }

    [[nodiscard]] CommandBatch upload(span<const T> values,
                                      bool async = true,
                                      DynamicBufferLayout layout = DynamicBufferLayout::AOS) const {
        OC_ASSERT(values.size() == size());
        OC_ASSERT(layout == this->layout());
        auto bytes = detail::encode_dynamic_buffer_values(values, policy(), layout);
        CommandBatch commands;
        commands << view_.upload(bytes->data(), async);
        commands << keep_alive(async, bytes);
        return commands;
    }

    [[nodiscard]] CommandBatch download(T *data,
                                        bool async = true,
                                        DynamicBufferLayout layout = DynamicBufferLayout::AOS) const {
        OC_ASSERT(layout == this->layout());
        auto bytes = detail::allocate_dynamic_buffer_bytes<T>(size(), policy(), layout);
        CommandBatch commands;
        commands << view_.download(bytes->data(), async);
        commands << HostFunctionCommand::create(async, [bytes, data, count = size(), policy = policy(), layout] {
            detail::decode_dynamic_buffer_values<T>(*bytes, count, data, policy, layout);
        });
        return commands;
    }

    [[nodiscard]] BufferByteSetCommand *byte_set(uchar value, bool async = true) const noexcept {
        return view_.byte_set(value, async);
    }

    [[nodiscard]] BufferByteSetCommand *reset(bool async = true) const noexcept {
        return view_.reset(async);
    }
};

template<typename T>
class DynamicBuffer {
private:
    detail::RawDynamicBuffer buffer_{};
    mutable BufferDesc<T> descriptor_{};

private:
    [[nodiscard]] Expr<Buffer<T>> as_buffer_expr() const noexcept {
        return make_expr<Buffer<T>>(buffer_expression());
    }

public:
    DynamicBuffer() = default;

private:
    explicit DynamicBuffer(detail::RawDynamicBuffer buffer) noexcept
        : buffer_(std::move(buffer)) {
        OC_ASSERT(buffer_.logical_type() == nullptr || buffer_.logical_type() == Type::of<T>());
    }

    friend class Device;

public:
    [[nodiscard]] bool valid() const noexcept { return buffer_.valid(); }
    [[nodiscard]] handle_ty handle() const noexcept { return buffer_.handle(); }
    [[nodiscard]] Device::Impl *device() const noexcept { return buffer_.device(); }
    [[nodiscard]] const string &name() const noexcept { return buffer_.name(); }
    void set_name(string name) noexcept { buffer_.set_name(std::move(name)); }
    [[nodiscard]] const Type *logical_type() const noexcept { return buffer_.logical_type(); }
    [[nodiscard]] const Type *resolved_type() const noexcept { return buffer_.resolved_type(); }
    [[nodiscard]] StoragePrecisionPolicy policy() const noexcept { return buffer_.policy(); }
    [[nodiscard]] DynamicBufferLayout layout() const noexcept { return buffer_.layout(); }
    [[nodiscard]] size_t element_stride() const noexcept { return buffer_.element_stride(); }
    [[nodiscard]] size_t element_alignment() const noexcept { return buffer_.element_alignment(); }
    [[nodiscard]] size_t size() const noexcept { return buffer_.size(); }
    [[nodiscard]] size_t capacity() const noexcept { return buffer_.capacity(); }
    [[nodiscard]] bool empty() const noexcept { return buffer_.empty(); }
    [[nodiscard]] size_t size_in_byte() const noexcept { return buffer_.size_in_byte(); }
    [[nodiscard]] size_t capacity_in_byte() const noexcept { return buffer_.capacity_in_byte(); }
    [[nodiscard]] uint offset_in_byte() const noexcept { return buffer_.offset_in_byte(); }

    const BufferDesc<T> &descriptor() const noexcept {
        OC_ASSERT(layout() == DynamicBufferLayout::AOS);
        descriptor_.handle = reinterpret_cast<T *>(handle());
        descriptor_.offset = 0u;
        descriptor_.size = size();
        return descriptor_;
    }

    const BufferDesc<T> *descriptor_ptr() const noexcept {
        return &descriptor();
    }

    [[nodiscard]] size_t data_alignment() const noexcept {
        return alignof(decltype(descriptor_));
    }

    [[nodiscard]] size_t data_size() const noexcept {
        return sizeof(descriptor_);
    }

    [[nodiscard]] MemoryBlock memory_block() const noexcept {
        return {descriptor_ptr(), data_size(), data_alignment(), sizeof(handle_ty)};
    }

    void destroy() noexcept { buffer_.destroy(); }
    void reserve(size_t element_capacity) noexcept { buffer_.reserve(element_capacity); }

    [[nodiscard]] ByteBufferView byte_view(size_t offset_in_byte = 0u, size_t size_in_byte = 0u) const noexcept {
        return buffer_.byte_view(offset_in_byte, size_in_byte);
    }

    [[nodiscard]] DynamicBufferView<T> view(size_t offset = 0u, size_t size = 0u) const noexcept {
        return DynamicBufferView<T>{buffer_.view(offset, size)};
    }

    [[nodiscard]] DynamicBufferView<T> subview(size_t offset, size_t size = 0u) const noexcept {
        return DynamicBufferView<T>{buffer_.subview(offset, size)};
    }

    [[nodiscard]] const Expression *expression() const noexcept { return buffer_.expression(); }
    [[nodiscard]] Expr<ByteBuffer> expr() const noexcept { return buffer_.expr(); }

    auto operator[](int i) { return T{}; }

    [[nodiscard]] const Expression *buffer_expression() const noexcept {
        OC_ASSERT(layout() == DynamicBufferLayout::AOS);
        const CapturedResource &captured_resource = Function::current()->get_captured_resource(Type::of<Buffer<T>>(),
                                                                                               Variable::Tag::BUFFER,
                                                                                               memory_block());
        return captured_resource.expression();
    }

    template<typename Index>
    requires concepts::all_integral<expr_value_t<Index>>
    [[nodiscard]] auto at(Index &&index) const noexcept {
        auto expr = as_buffer_expr();
        return expr.at(OC_FORWARD(index));
    }

    template<typename Index>
    requires concepts::all_integral<expr_value_t<Index>>
    [[nodiscard]] auto &at(Index &&index) noexcept {
        auto expr = as_buffer_expr();
        return expr.at(OC_FORWARD(index));
    }

    template<typename Index>
    requires concepts::all_integral<expr_value_t<Index>>
    [[nodiscard]] auto read(Index &&index, bool check_boundary = true) const {
        auto expr = as_buffer_expr();
        return expr.read(OC_FORWARD(index), check_boundary);
    }

    template<typename... Index>
    requires concepts::all_integral<expr_value_t<Index>...>
    [[nodiscard]] auto read_multi(Index &&...index) const {
        return as_buffer_expr().read_multi(OC_FORWARD(index)...);
    }

    template<typename Index, typename Val>
    requires concepts::integral<expr_value_t<Index>> && concepts::static_convertible<T, expr_value_t<Val>>
    void write(Index &&index, Val &&elm, bool check_boundary = true) {
        auto expr = as_buffer_expr();
        expr.write(OC_FORWARD(index), OC_FORWARD(elm), check_boundary);
    }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    [[nodiscard]] detail::AtomicRef<T> atomic(Index &&index) const noexcept {
        return as_buffer_expr().atomic(OC_FORWARD(index));
    }

    template<typename Target, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] Var<Target> load_as(Offset &&offset) const noexcept {
        return buffer_.template load_as<Target>(OC_FORWARD(offset));
    }

    template<typename Elm, typename Offset>
    requires is_integral_expr_v<Offset>
    void store(Offset &&offset, const Elm &val) noexcept {
        buffer_.store(OC_FORWARD(offset), val);
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] auto aos_view_var() const noexcept {
        return buffer_.template aos_view_var<Elm, int_type>();
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] auto soa_view_var() const noexcept {
        return buffer_.template soa_view_var<Elm, int_type>();
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] auto aos_view_var(const Var<int_type> &view_size) const noexcept {
        return buffer_.template aos_view_var<Elm, int_type>(view_size);
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] auto soa_view_var(const Var<int_type> &view_size) const noexcept {
        return buffer_.template soa_view_var<Elm, int_type>(view_size);
    }

    [[nodiscard]] CommandBatch upload(const T *data,
                                      size_t count,
                                      bool async = true,
                                      DynamicBufferLayout layout = DynamicBufferLayout::AOS) {
        return upload(span<const T>{data, count}, async, layout);
    }

    [[nodiscard]] CommandBatch upload(span<const T> values,
                                      bool async = true,
                                      DynamicBufferLayout layout = DynamicBufferLayout::AOS) {
        OC_ASSERT(layout == this->layout());
        auto bytes = detail::encode_dynamic_buffer_values(values, policy(), layout);
        CommandBatch commands = buffer_.upload(bytes->data(), bytes->size(), values.size(), async);
        commands << keep_alive(async, bytes);
        return commands;
    }

    void upload_immediately(const T *data,
                            size_t count,
                            DynamicBufferLayout layout = DynamicBufferLayout::AOS) noexcept {
        upload_immediately(span<const T>{data, count}, layout);
    }

    void upload_immediately(span<const T> values,
                            DynamicBufferLayout layout = DynamicBufferLayout::AOS) noexcept {
        OC_ASSERT(layout == this->layout());
        HostByteBuffer bytes;
        DynamicBufferLayoutCodec<T>::encode(values.data(),
                                            values.size(),
                                            bytes,
                                            policy(),
                                            layout);
        buffer_.upload_immediately(bytes.bytes_span(), values.size());
    }

    [[nodiscard]] CommandBatch download(T *data,
                                        bool async = true,
                                        DynamicBufferLayout layout = DynamicBufferLayout::AOS) const {
        OC_ASSERT(layout == this->layout());
        auto bytes = detail::allocate_dynamic_buffer_bytes<T>(size(), policy(), layout);
        CommandBatch commands;
        commands << buffer_.download(bytes->data(), async);
        commands << HostFunctionCommand::create(async, [bytes, data, count = size(), policy = policy(), layout] {
            detail::decode_dynamic_buffer_values<T>(*bytes, count, data, policy, layout);
        });
        return commands;
    }

    void download_immediately(T *data,
                              DynamicBufferLayout layout = DynamicBufferLayout::AOS) const noexcept {
        OC_ASSERT(layout == this->layout());
        HostByteBuffer bytes;
        bytes.resize(DynamicBufferLayoutCodec<T>::storage_bytes(size(),
                                                                policy(),
                                                                layout));
        buffer_.download_immediately(bytes.data());
        detail::decode_dynamic_buffer_values<T>(bytes, size(), data, policy(), layout);
    }

    [[nodiscard]] DynamicBufferUploadStats sync_immediately(const HostDynamicBufferUploadView &view,
                                                            bool force_full_upload = false) noexcept {
        return buffer_.sync_immediately(view, force_full_upload);
    }

    template<typename U>
    [[nodiscard]] DynamicBufferUploadStats sync_immediately(HostDynamicBuffer<U> &host_buffer,
                                                            bool force_full_upload = false) noexcept {
        return buffer_.sync_immediately(host_buffer, force_full_upload);
    }

    [[nodiscard]] BufferByteSetCommand *byte_set(uchar value, bool async = true) const noexcept {
        return buffer_.byte_set(value, async);
    }

    [[nodiscard]] BufferByteSetCommand *reset(bool async = true) const noexcept {
        return buffer_.reset(async);
    }

    template<typename Arg>
    requires is_buffer_or_view_v<Arg> && std::is_same_v<buffer_element_t<Arg>, std::byte>
    [[nodiscard]] BufferCopyCommand *copy_from(const Arg &src, uint dst_offset = 0u) const noexcept {
        return buffer_.copy_from(src, dst_offset);
    }

    template<typename Arg>
    requires is_buffer_or_view_v<Arg> && std::is_same_v<buffer_element_t<Arg>, std::byte>
    [[nodiscard]] BufferCopyCommand *copy_to(Arg &dst, uint src_offset = 0u) const noexcept {
        return buffer_.copy_to(dst, src_offset);
    }
};

}// namespace ocarina