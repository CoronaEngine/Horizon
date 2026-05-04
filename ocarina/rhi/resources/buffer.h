//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "resource.h"
#include "rhi/command.h"
#include "rhi/stats.h"
#include "bindless_array.h"

namespace ocarina {

template<typename T>
class Buffer;

template<typename T>
class BufferView {
private:
    handle_ty handle_{};
    size_t offset_{};
    size_t size_{};
    size_t total_size_{};

    mutable BufferDesc<T> descriptor_{};

public:
    BufferView() = default;
    BufferView(const Buffer<T> &buffer);
    [[nodiscard]] handle_ty handle() const { return handle_; }
    [[nodiscard]] size_t size() const { return size_; }
    [[nodiscard]] static constexpr size_t element_size() noexcept { return sizeof(T); }
    [[nodiscard]] size_t size_in_byte() const noexcept { return size_ * element_size(); }
    [[nodiscard]] size_t offset() const noexcept { return offset_; }
    [[nodiscard]] size_t offset_in_byte() const noexcept { return offset_ * element_size(); }
    [[nodiscard]] size_t total_size_in_byte() const noexcept { return total_size_ * element_size(); }
    OC_MAKE_MEMBER_GETTER(total_size, )

    const BufferDesc<T> &descriptor() const noexcept {
        descriptor_.handle = reinterpret_cast<T *>(handle());
        descriptor_.offset = offset_;
        descriptor_.size = size_;
        return descriptor_;
    }

    const BufferDesc<T> *descriptor_ptr() const noexcept {
        return &descriptor();
    }

    template<typename Dst>
    [[nodiscard]] BufferView<Dst> view_as(size_t offset = 0, size_t size = 0) const noexcept {
        size = size == 0 ? size_ - offset : size;
        return BufferView<Dst>(handle_,
                               (offset_ + offset) * sizeof(T) / sizeof(Dst),
                               size * sizeof(T) / sizeof(Dst),
                               total_size_ * sizeof(T) / sizeof(Dst));
    }

    BufferView(handle_ty handle, size_t offset, size_t size, size_t total_size)
        : handle_(handle), offset_(offset), size_(size), total_size_(total_size) {}

    BufferView(handle_ty handle, size_t total_size)
        : handle_(handle), offset_(0), total_size_(total_size), size_(total_size) {}

    [[nodiscard]] BufferView<T> subview(size_t offset, size_t size) const noexcept {
        return BufferView<T>(handle_, offset_ + offset, size, total_size_);
    }

    template<typename Arg>
    requires is_buffer_or_view_v<Arg> && std::is_same_v<buffer_element_t<Arg>, T>
    [[nodiscard]] BufferCopyCommand *copy_from(const Arg &src, uint dst_offset = 0) noexcept {
        return BufferCopyCommand::create(src.handle(), handle(),
                                         src.offset_in_byte(),
                                         (dst_offset + offset_) * element_size(),
                                         src.size_in_byte(), true);
    }

    template<typename Arg>
    requires is_buffer_or_view_v<Arg> && std::is_same_v<buffer_element_t<Arg>, T>
    [[nodiscard]] BufferCopyCommand *copy_to(Arg &dst, uint src_offset = 0) noexcept {
        return BufferCopyCommand::create(handle(), dst.handle(),
                                         (src_offset + offset_) * element_size(),
                                         dst.offset_in_byte(),
                                         dst.size_in_byte(), true);
    }

    [[nodiscard]] BufferUploadCommand *upload(const void *data, bool async = true) const noexcept {
        return BufferUploadCommand::create(data, handle(), offset_in_byte(), size_in_byte(), async);
    }

    [[nodiscard]] BufferUploadCommand *upload_sync(const void *data) const noexcept {
        return upload(data, false);
    }

    [[nodiscard]] BufferDownloadCommand *download(void *data, uint src_offset = 0, bool async = true) const noexcept {
        return BufferDownloadCommand::create(data, handle(), (offset_ + src_offset) * element_size(),
                                             size_in_byte(), async);
    }

    [[nodiscard]] BufferDownloadCommand *download_sync(void *data) const noexcept {
        return download(data, 0, false);
    }

    [[nodiscard]] BufferByteSetCommand *byte_set(uchar value, bool async = true) const noexcept {
        return BufferByteSetCommand::create(handle(), size_in_byte(), value, async);
    }

    [[nodiscard]] BufferByteSetCommand *reset(bool async = true) const noexcept {
        return byte_set(0, async);
    }
};

template<typename T = std::byte>
class Buffer : public ExportableResource {
    static_assert(is_valid_buffer_element_v<T>);
    static constexpr bool use_for_dsl = is_dsl_basic_v<T>;

public:
    using element_type = T;
    using Super = ExportableResource;

protected:
    size_t size_{};
    mutable BufferDesc<T> descriptor_{};
    string name_;

public:
    Buffer() = default;

    [[nodiscard]] static constexpr size_t element_size() noexcept { return sizeof(T); }

    Buffer(Device::Impl *device, size_t size, const string &desc = "", bool exported = false)
        : Super(device, Tag::BUFFER, device->create_buffer(size * element_size(), desc, exported), exported),
          size_(size), name_(desc) {
        descriptor_ptr();
    }

    OC_MAKE_MEMBER_GETTER_SETTER(name, )

        Buffer(BufferView<T> buffer_view)
        : Super(nullptr, Tag::BUFFER, buffer_view.handle()),
          size_(buffer_view.size()) {
        descriptor_ptr();
    }

    Buffer(Device::Impl *device, size_t size, handle_ty handle)
        : Super(device, Tag::BUFFER, handle), size_(size) {
        descriptor_ptr();
    }

    void destroy() override {
        _destroy();
        size_ = 0;
    }

    void import_handle(uint64_t handle) override {
        device_->import_handle(handle, size_in_byte());
    };

    uint64_t export_handle() override {
        return device_->export_handle(handle());
    };

    [[nodiscard]] BufferView<T> view(size_t offset = 0, size_t size = 0) const noexcept {
        size = size == 0 ? size_ - offset : size;
        return BufferView<T>(handle_, offset, size, size_);
    }

    template<typename Dst>
    [[nodiscard]] BufferView<Dst> view_as(size_t offset = 0, size_t size = 0) const noexcept {
        return view().template view_as<Dst>(offset, size);
    }

    // Move constructor
    Buffer(Buffer &&other) noexcept
        : Super(std::move(other)) {
        this->size_ = other.size_;
        this->name_ = std::move(other.name_);
        this->descriptor_ = other.descriptor_;
    }

    // Move assignment
    Buffer &operator=(Buffer &&other) noexcept {
        Super::operator=(std::move(other));
        this->size_ = other.size_;
        this->name_ = std::move(other.name_);
        this->descriptor_ = other.descriptor_;
        return *this;
    }

    const BufferDesc<T> &descriptor() const noexcept {
        descriptor_.handle = reinterpret_cast<T *>(handle_);
        descriptor_.offset = 0u;
        descriptor_.size = size_;
        return descriptor_;
    }

    const BufferDesc<T> *descriptor_ptr() const noexcept {
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

    template<typename U>
    [[nodiscard]] auto ptr() const noexcept {
        if constexpr (std::is_same_v<U, handle_ty>) {
            return handle();
        } else {
            return reinterpret_cast<U>(handle());
        }
    }

    template<typename U>
    [[nodiscard]] auto ptr() noexcept {
        if constexpr (std::is_same_v<U, handle_ty>) {
            return handle();
        } else {
            return reinterpret_cast<U>(handle());
        }
    }

    template<typename U = void *>
    [[nodiscard]] auto address(size_t offset) const noexcept {
        return (U)(handle() + offset * element_size());
    }

    void set_size(size_t size) noexcept { size_ = size; }

    [[nodiscard]] uint offset_in_byte() const noexcept { return 0; }

    /// for dsl trait
    auto operator[](int i) { return T{}; }

    template<typename U = T>
    [[nodiscard]] const Expression *expression() const noexcept {
        const CapturedResource &captured_resource = Function::current()->get_captured_resource(Type::of<decltype(*this)>(),
                                                                                               Variable::Tag::BUFFER,
                                                                                               memory_block());
        return captured_resource.expression();
    }

    /// for dsl start
    template<typename Index>
    requires concepts::all_integral<expr_value_t<Index>>
    OC_NODISCARD auto at(Index &&index) const noexcept {
        const auto expr = make_expr<Buffer<T>>(expression());
        return expr.at(OC_FORWARD(index));
    }

    template<typename Index>
    requires concepts::all_integral<expr_value_t<Index>>
    OC_NODISCARD auto &at(Index &&index) noexcept {
        auto expr = make_expr<Buffer<T>>(expression());
        return expr.at(OC_FORWARD(index));
    }

    template<typename Index>
    requires concepts::all_integral<expr_value_t<Index>>
    OC_NODISCARD auto
    read(Index &&index, bool check_boundary = true) const {
        auto expr = make_expr<Buffer<T>>(expression());
        return expr.read(OC_FORWARD(index), check_boundary);
    }

    template<typename... Index>
    requires concepts::all_integral<expr_value_t<Index>...>
    OC_NODISCARD auto
    read_multi(Index &&...index) const {
        return make_expr<Buffer<T>>(expression()).read_multi(OC_FORWARD(index)...);
    }

    template<typename Index, typename Val>
    requires concepts::integral<expr_value_t<Index>> && concepts::static_convertible<element_type, expr_value_t<Val>>
    void write(Index &&index, Val &&elm, bool check_boundary = true) {
        auto expr = make_expr<Buffer<T>>(expression());
        expr.write(OC_FORWARD(index), OC_FORWARD(elm), check_boundary);
    }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    [[nodiscard]] detail::AtomicRef<T> atomic(Index &&index) const noexcept {
        return make_expr<Buffer<T>>(expression()).atomic(OC_FORWARD(index));
    }
    /// for dsl end

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t size_in_byte() const noexcept { return size_ * sizeof(T); }

    [[nodiscard]] CommandBatch reallocate(size_t size, bool async = true) {
        return {BufferReallocateCommand::create(this, size * element_size(), async),
                HostFunctionCommand::create(async,
                                            [this, size] {
                                                this->size_ = size;
                                            })};
    }

    template<typename... Args>
    [[nodiscard]] BufferUploadCommand *upload(Args &&...args) const noexcept {
        return view(0, size_).upload(OC_FORWARD(args)...);
    }

    template<typename... Args>
    [[nodiscard]] BufferDownloadCommand *download(Args &&...args) const noexcept {
        return view(0, size_).download(OC_FORWARD(args)...);
    }

    template<typename... Args>
    [[nodiscard]] BufferByteSetCommand *byte_set(Args &&...args) const noexcept {
        return view(0, size_).byte_set(OC_FORWARD(args)...);
    }

    [[nodiscard]] BufferByteSetCommand *reset(bool async = true) const noexcept {
        return byte_set(0, async);
    }

    template<typename... Args>
    [[nodiscard]] BufferCopyCommand *copy_from(Args &&...args) const noexcept {
        return view(0, size_).copy_from(OC_FORWARD(args)...);
    }

    template<typename... Args>
    [[nodiscard]] BufferCopyCommand *copy_to(Args &&...args) const noexcept {
        return view(0, size_).copy_to(OC_FORWARD(args)...);
    }

    template<typename... Args>
    [[nodiscard]] BufferUploadCommand *upload_sync(Args &&...args) const noexcept {
        return view(0, size_).upload_sync(OC_FORWARD(args)...);
    }

    template<typename... Args>
    [[nodiscard]] BufferDownloadCommand *download_sync(Args &&...args) const noexcept {
        return view(0, size_).download_sync(OC_FORWARD(args)...);
    }

    void upload_immediately(const void *data) const noexcept {
        upload_sync(data)->accept(*device_->command_visitor());
    }

    void upload_immediately(const void *data, size_t offset, size_t size) const noexcept {
        view(offset, size).upload_sync(data)->accept(*device_->command_visitor());
    }

    void download_immediately(void *data) const noexcept {
        download_sync(data)->accept(*device_->command_visitor());
    }

    void reset_immediately() const noexcept {
        reset(false)->accept(*device_->command_visitor());
    }
};

template<typename T>
BufferView<T>::BufferView(const Buffer<T> &buffer)
    : BufferView(buffer.handle(), buffer.size()) {}

}// namespace ocarina