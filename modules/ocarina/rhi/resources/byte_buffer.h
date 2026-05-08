//
// Created by Zero on 2024/2/22.
//

#pragma once

#include "resource.h"
#include "rhi/command.h"
#include "rhi/stats.h"
#include "bindless_array.h"
#include "buffer.h"

namespace ocarina {

class ByteBuffer;

class ByteBufferView : public BufferView<std::byte> {
public:
    using Super = BufferView<std::byte>;
    using Super::Super;

public:
    explicit ByteBufferView(const ByteBuffer &buffer);
    template<typename T>
    explicit ByteBufferView(const BufferView<T> &bv)
        : Super(bv.handle(), bv.offset_in_byte(),
                bv.size_in_byte(), bv.total_size_in_byte()) {}
};

class ByteBuffer : public RHIResource {
private:
    /// just for construct memory block
    mutable BufferDesc<> descriptor_{};
    size_t size_{};

public:
    ByteBuffer() = default;
    ByteBuffer(Device::Impl *device, size_t size, const string &desc = "")
        : RHIResource(device, Tag::BUFFER, device->create_buffer(size, desc)),
          size_(size) {}

    ByteBuffer(ByteBufferView buffer_view)
        : RHIResource(nullptr, Tag::BUFFER, buffer_view.handle()),
          size_(buffer_view.size()) {}

    /// head of the buffer
    [[nodiscard]] handle_ty head() const noexcept { return handle(); }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    template<typename Size = size_t>
    [[nodiscard]] Size size_in_byte() const noexcept { return size(); }
    [[nodiscard]] uint offset_in_byte() const noexcept { return 0; }

    void destroy() override {
        _destroy();
        size_ = 0;
    }

    const BufferDesc<std::byte> &descriptor() const noexcept {
        descriptor_.handle = reinterpret_cast<std::byte *>(handle_);
        descriptor_.offset = 0u;
        descriptor_.size = size_;
        return descriptor_;
    }

    const BufferDesc<std::byte> *descriptor_ptr() const noexcept {
        return &descriptor();
    }

    [[nodiscard]] size_t data_alignment() const noexcept override {
        return alignof(decltype(descriptor_));
    }

    [[nodiscard]] size_t data_size() const noexcept override {
        return sizeof(descriptor_);
    }

    [[nodiscard]] MemoryBlock memory_block() const noexcept override {
        return {descriptor_ptr(), data_size(), data_alignment(), max_member_size()};
    }

    template<typename U = uint>
    [[nodiscard]] const Expression *expression() const noexcept {
        const CapturedResource &captured_resource = Function::current()->get_captured_resource(Type::of<decltype(*this)>(),
                                                                                               Variable::Tag::BYTE_BUFFER,
                                                                                               memory_block());
        return captured_resource.expression();
    }

    [[nodiscard]] ByteBufferView view(size_t offset = 0, size_t size = 0) const noexcept {
        size = size == 0 ? size_ - offset : size;
        return ByteBufferView(handle_, offset, size, size_);
    }

    template<typename T>
    [[nodiscard]] BufferView<T> view_as(size_t offset = 0, size_t size = 0) const noexcept {
        return view().template view_as<T>(offset, size);
    }

    template<typename... Args>
    [[nodiscard]] BufferCopyCommand *copy_from(Args &&...args) const noexcept {
        return view(0, size_).copy_from(OC_FORWARD(args)...);
    }
    template<typename... Args>
    [[nodiscard]] BufferDownloadCommand *download(Args &&...args) const noexcept {
        return view(0, size_).download(OC_FORWARD(args)...);
    }
    template<typename... Args>
    [[nodiscard]] BufferUploadCommand *upload(Args &&...args) const noexcept {
        return view(0, size_).upload(OC_FORWARD(args)...);
    }
    void upload_immediately(const void *data) const noexcept {
        upload(data, false)->accept(*device_->command_visitor());
    }
    void download_immediately(void *data) const noexcept {
        download(data, false)->accept(*device_->command_visitor());
    }
    template<typename... Args>
    [[nodiscard]] BufferByteSetCommand *byte_set(Args &&...args) const noexcept {
        return view(0, size_).byte_set(OC_FORWARD(args)...);
    }
    template<typename... Args>
    [[nodiscard]] BufferByteSetCommand *reset(Args &&...args) const noexcept {
        return view(0, size_).reset(OC_FORWARD(args)...);
    }

    /// for dsl start
    template<uint N = 1, typename Elm = uint, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] auto load(Offset &&offset) const noexcept {
        auto expr = make_expr<ByteBuffer>(expression());
        return expr.template load<N, Elm>(OC_FORWARD(offset));
    }

    template<typename Elm = uint, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] auto load2(Offset &&offset) const noexcept {
        return load<2, Elm>(OC_FORWARD(offset));
    }

    template<typename Elm = uint, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] auto load3(Offset &&offset) const noexcept {
        return load<3, Elm>(OC_FORWARD(offset));
    }

    template<typename Elm = uint, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] auto load4(Offset &&offset) const noexcept {
        return load<4, Elm>(OC_FORWARD(offset));
    }

    [[nodiscard]] Expr<ByteBuffer> expr() const noexcept {
        return make_expr<ByteBuffer>(expression());
    }

    template<typename Target, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] Var<Target> load_as(Offset &&offset) const noexcept {
        return expr().template load_as<Target>(OC_FORWARD(offset));
    }

    template<typename Target, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] Var<Target> &load_as(Offset &&offset) noexcept {
        return expr().template load_as<Target>(OC_FORWARD(offset));
    }

    template<typename Elm, typename Offset>
    requires is_integral_expr_v<Offset>
    void store(Offset &&offset, const Elm &val) noexcept {
        expr().store(OC_FORWARD(offset), val);
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] auto soa_view_var(const Var<int_type> &view_size = InvalidUI32) const noexcept {
        return expr().soa_view_var<Elm>(view_size);
    }

    template<typename Elm, typename int_type = uint>
    [[nodiscard]] auto aos_view_var(const Var<int_type> &view_size = InvalidUI32) const noexcept {
        return expr().aos_view_var<Elm>(view_size);
    }

    template<typename Target = uint, typename Index>
    requires concepts::integral<expr_value_t<Index>>
    [[nodiscard]] detail::AtomicRef<Target> atomic(Index &&index) const noexcept {
        return make_expr<ByteBuffer>(expression()).atomic<Target>(OC_FORWARD(index));
    }
    /// for dsl end
};

inline ByteBufferView::ByteBufferView(const ocarina::ByteBuffer &buffer)
    : Super(buffer.handle(), buffer.size()) {}

template<typename T, ocarina::AccessMode mode>
CountedBuffer<T, mode> Device::create_list(size_t size, const std::string &name) const noexcept {
    return CountedBuffer<T, mode>(create_byte_buffer(sizeof(T) * size + sizeof(uint), name));
}

}// namespace ocarina