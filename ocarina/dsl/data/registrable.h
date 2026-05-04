//
// Created by zhu on 2023/4/27.
//

#pragma once

#include "encodable.h"
#include "rhi/resources/managed.h"
#include "../diagnostics/printer.h"
#include "core/runtime/platform.h"
#include "rhi/resources/byte_buffer.h"

namespace ocarina {

class Registrable : public Encodable {
protected:
    EncodedData<uint> index_{InvalidUI32};
    EncodedData<uint> length_{InvalidUI32};
    BindlessArray *bindless_array_{};

public:
    Registrable() = default;
    explicit Registrable(BindlessArray *bindless_array)
        : bindless_array_(bindless_array) {}
    OC_ENCODABLE_FUNC(Encodable, index_, length_)
    void set_bindless_array(BindlessArray &bindless_array) noexcept {
        bindless_array_ = &bindless_array;
    }
    [[nodiscard]] BindlessArray *bindless_array() const noexcept {
        return bindless_array_;
    }
    [[nodiscard]] bool has_registered() const noexcept { return index_.hv() != InvalidUI32; }
    [[nodiscard]] const EncodedData<uint> &index() const noexcept { return index_; }
    [[nodiscard]] const EncodedData<uint> &length() const noexcept { return length_; }

protected:
    template<typename T, typename Index>
    requires concepts::all_integral<expr_value_t<Index>>
    OC_NODISCARD auto _read(Index &&index) const noexcept {
        Uint buffer_index = *index_;
        Uint access_index = OC_FORWARD(index);
        return bindless_array_->buffer_var<T>(buffer_index).read(access_index);
    }
};

template<typename T>
class RegistrableBuffer : public Buffer<T>,
                          public Registrable {
public:
    using Super = Buffer<T>;

public:
    explicit RegistrableBuffer(BindlessArray &bindless_array)
        : Super(), Registrable(&bindless_array) {}

    RegistrableBuffer() = default;

    void register_self(size_t offset = 0, size_t size = 0) noexcept {
        BufferView<T> buffer_view = super().view(offset, size);
        if (has_registered()) {
            bindless_array_->set_buffer(index_.hv(), buffer_view);
        } else {
            index_ = bindless_array_->emplace(buffer_view);
        }
        length_ = [=]() {
            return static_cast<uint>(buffer_view.size());
        };
    }

    uint register_view(size_t offset, size_t size = 0) {
        BufferView<T> buffer_view = super().view(offset, size);
        return bindless_array_->emplace(buffer_view);
    }

    uint register_view_index(uint index, size_t offset, size_t size = 0) {
        BufferView<T> buffer_view = super().view(offset, size);
        index += index_.hv();
        bindless_array_->set_buffer(index, buffer_view);
        return index;
    }

    void update_buffer(Super buffer) {
        super() = std::move(buffer);
        register_self();
    }

    [[nodiscard]] Super &super() noexcept { return *this; }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    OC_NODISCARD auto read(Index &&index) const noexcept {
        if (!has_registered()) {
            return Super::read(OC_FORWARD(index));
        }
        return _read<T>(OC_FORWARD(index));
    }

    template<typename Index, typename Val>
    requires concepts::integral<expr_value_t<Index>> && ocarina::is_same_v<T, expr_value_t<Val>>
    void write(Index &&index, Val &&elm) {
        if (!has_registered()) {
            Super::write(OC_FORWARD(index), OC_FORWARD(elm));
        } else {
            bindless_array_->buffer_var<T>(*index_).write(OC_FORWARD(index), OC_FORWARD(elm));
        }
    }
};

class RegistrableByteBuffer : public ByteBuffer, public Registrable {
public:
    using Super = ByteBuffer;

public:
    explicit RegistrableByteBuffer(BindlessArray &bindless_array)
        : Super(), Registrable(&bindless_array) {}

    RegistrableByteBuffer() = default;
    [[nodiscard]] Super &super() noexcept { return *this; }
    void register_self(size_t offset = 0, size_t size = 0) noexcept {
        ByteBufferView buffer_view = super().view(offset, size);
        if (has_registered()) {
            bindless_array_->set_buffer(index_.hv(), buffer_view);
        } else {
            index_ = bindless_array_->emplace(buffer_view);
        }
        length_ = [=]() {
            return static_cast<uint>(buffer_view.size());
        };
    }

    uint register_view(size_t offset, size_t size = 0) {
        ByteBufferView buffer_view = super().view(offset, size);
        return bindless_array_->emplace(buffer_view);
    }

    uint register_view_index(uint index, size_t offset, size_t size = 0) {
        ByteBufferView buffer_view = super().view(offset, size);
        index += index_.hv();
        bindless_array_->set_buffer(index, buffer_view);
        return index;
    }

    void update_buffer(Super buffer) {
        super() = std::move(buffer);
        register_self();
    }

    template<typename Target, typename Offset>
    requires concepts::integral<expr_value_t<Offset>>
    OC_NODISCARD auto load_as(Offset &&offset) const noexcept {
        if (!has_registered()) {
            return Super::load_as<Target>(OC_FORWARD(offset));
        }
        Uint buffer_index = *index_;
        return bindless_array_->byte_buffer_var(buffer_index).template load_as<Target>(OC_FORWARD(offset));
    }

    template<typename Elm, typename Offset, typename Size = uint>
    requires is_integral_expr_v<Offset>
    void store(Offset &&offset, const Elm &val, bool check_boundary = true) noexcept {
        if (!has_registered()) {
            Super::store(OC_FORWARD(offset), val);
        } else {
            Uint buffer_index = *index_;
            bindless_array_->byte_buffer_var(buffer_index).store(OC_FORWARD(offset), val);
        }
    }
};

template<typename T, AccessMode mode = AOS>
class RegistrableCountedBuffer : public CountedBuffer<T, mode, ByteBuffer>,
                                 public Registrable {
public:
    using Super = CountedBuffer<T, mode, ByteBuffer>;

public:
    RegistrableCountedBuffer() = default;
    explicit RegistrableCountedBuffer(BindlessArray &bindless_array)
        : Super(), Registrable(&bindless_array) {}

    explicit RegistrableCountedBuffer(Super &&list) : Super(std::move(list)) {}

    void set_buffer(Super &&list) noexcept {
        super() = std::move(list);
    }

    void update_buffer(Super buffer) {
        super() = std::move(buffer);
        register_self();
    }

    [[nodiscard]] const Super &super() const noexcept { return *this; }
    [[nodiscard]] Super &super() noexcept { return *this; }

    [[nodiscard]] const Super *operator->() const noexcept { return &super(); }
    [[nodiscard]] Super *operator->() noexcept { return &super(); }

    void register_self() noexcept {
        if (has_registered()) {
            bindless_array_->set_buffer(index_.hv(), Super::buffer());
        } else {
            index_ = bindless_array_->emplace(Super::buffer());
        }
        length_ = [&]() {
            return static_cast<uint>(Super::capacity());
        };
    }

    void unregister() noexcept {
        if (has_registered()) {
            (*bindless_array_)->remove_buffer(index_.hv());
            index_ = InvalidUI32;
        }
    }

    /// for dsl start
    template<typename Size = uint>
    [[nodiscard]] Var<Size> size_in_byte() const noexcept {
        if (has_registered()) {
            Uint buffer_index = *index_;
            return bindless_array_->byte_buffer_var(buffer_index).size_in_byte();
        }
        return Super::buffer().expr().size_in_byte();
    }

    template<typename Size = uint>
    [[nodiscard]] Var<Size> storage_size_in_byte() const noexcept {
        return size_in_byte() - sizeof(uint);
    }

    [[nodiscard]] auto bindless_buffer() const noexcept {
        Uint buffer_index = *index_;
        BindlessArrayByteBuffer buffer = bindless_array_->byte_buffer_var(buffer_index);
        return buffer;
    }

    [[nodiscard]] auto bindless_list() const noexcept {
        BindlessArrayByteBuffer buffer = bindless_buffer();
        return create_list<T, mode>(buffer);
    }

    template<typename Size = uint>
    [[nodiscard]] Var<Size> &count() noexcept {
        if (has_registered()) {
            return bindless_list().template count<Size>();
        }
        return Super::template count<Size>();
    }

    template<typename Arg, typename Index = uint>
    requires std::is_same_v<T, remove_device_t<Arg>>
    Var<Index> push_back(const Arg &arg) noexcept {
        Var<Index> index = advance_index();
        write(index, arg);
        return index;
    }

    template<typename Index = uint>
    requires is_integral_expr_v<Index>
    [[nodiscard]] Var<Index> advance_index() noexcept {
        Var<Index> old_index = atomic_add(count<Index>(), 1);
        return old_index;
    }

    template<typename Index, typename Size = uint>
    requires is_integral_expr_v<Index>
    [[nodiscard]] Var<T> read(const Index &index) const noexcept {
        if (has_registered()) {
            return bindless_list().read(index);
        }
        return Super::read(index);
    }

    template<typename Index, typename Size = uint>
    requires is_integral_expr_v<Index>
    [[nodiscard]] Var<T> at(const Index &index) const noexcept {
        return read(index);
    }

    template<typename Index, typename Size = uint>
    requires is_integral_expr_v<Index>
    [[nodiscard]] Var<T> &at(const Index &index) noexcept {
        if (has_registered()) {
            return bindless_list().at(index);
        }
        return Super::at(index);
    }

    template<typename Index, typename Arg, typename Size = uint>
    requires std::is_same_v<T, remove_device_t<Arg>> && is_integral_expr_v<Index>
    void write(const Index &index, const Arg &arg) noexcept {
        if (has_registered()) {
            bindless_list().write(index, arg);
        } else {
            Super::write(index, arg);
        }
    }
    /// for dsl end
};

template<typename T>
class RegistrableManaged : public Managed<T>,
                           public Registrable {
public:
    using Super = Managed<T>;

public:
    RegistrableManaged() = default;
    explicit RegistrableManaged(BindlessArray &bindless_array) : Registrable(&bindless_array) {}
    void register_self() noexcept {
        if (has_registered()) {
            bindless_array_->set_buffer(index_.hv(), Super::device_buffer());
        } else {
            index_ = bindless_array_->emplace(Super::device_buffer());
        }
        length_ = [&]() {
            return static_cast<uint>(Super::device_buffer().size());
        };
    }

    uint register_view(size_t offset, size_t size = 0) {
        BufferView<T> buffer_view = Super::device_buffer().view(offset, size);
        return bindless_array_->emplace(buffer_view);
    }

    uint register_view_index(uint index, size_t offset, size_t size = 0) {
        BufferView<T> buffer_view = super().view(offset, size);
        index += index_.hv();
        bindless_array_->set_buffer(index, buffer_view);
        return index;
    }

    void update_buffer(Super buffer) {
        super().device_buffer() = std::move(buffer);
        super().host_buffer().resize(buffer.size());
        register_self();
    }

    [[nodiscard]] const Super &super() const noexcept { return *this; }
    [[nodiscard]] Super &super() noexcept { return *this; }

    void unregister() noexcept {
        if (has_registered()) {
            (*bindless_array_)->remove_buffer(index_.hv());
            index_ = InvalidUI32;
        }
    }

    template<typename Index>
    requires concepts::all_integral<expr_value_t<Index>>
    OC_NODISCARD auto read(Index &&index) const noexcept {
        if (!has_registered()) {
            return Super::read(OC_FORWARD(index));
        }
        return _read<T>(OC_FORWARD(index));
    }

    template<typename Target, typename Offset>
    requires is_integral_expr_v<Offset>
    OC_NODISCARD auto byte_read(Offset &&offset) const noexcept {
        OC_ASSERT(has_registered());
        return bindless_array_->byte_buffer_var(*index_).template read<Target>(OC_FORWARD(offset));
    }

    template<typename Elm, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] DynamicArray<Elm> load_dynamic_array(uint size, Offset &&offset) const noexcept {
        OC_ASSERT(has_registered());
        return bindless_array_->byte_buffer_var(*index_).template load_dynamic_array<Elm>(size, OC_FORWARD(offset));
    }

    template<typename Index, typename Val>
    requires concepts::integral<expr_value_t<Index>> && ocarina::is_same_v<T, expr_value_t<Val>>
    void write(Index &&index, Val &&elm) {
        if (!has_registered()) {
            Super::write(OC_FORWARD(index), OC_FORWARD(elm));
        } else {
            bindless_array_->buffer_var<T>(*index_).write(OC_FORWARD(index), OC_FORWARD(elm));
        }
    }
};

class RegistrableTexture3D : public ManagedTexture3D, public Registrable {
public:
    RegistrableTexture3D() = default;
    explicit RegistrableTexture3D(BindlessArray &bindless_array) : Registrable(&bindless_array) {}
    void register_self() noexcept {
        if (has_registered()) {
            bindless_array_->set_texture3d(index_.hv(), *this);
        } else {
            index_ = bindless_array_->emplace(*this);
        }
        length_ = 0;
    }

    void unregister() noexcept {
        if (has_registered()) {
            (*bindless_array_)->remove_texture3d(index_.hv());
            index_ = InvalidUI32;
        }
    }

    template<typename... Args>
    OC_NODISCARD auto sample(uint channel_num, Args &&...args) const noexcept {
        if (has_registered()) {
            return bindless_array_->tex3d_var(*index_).sample(channel_num, OC_FORWARD(args)...);
        } else {
            return Texture3D::sample(channel_num, OC_FORWARD(args)...);
        }
    }

    template<typename Target, typename... Args>
    OC_NODISCARD auto read(Args &&...args) const noexcept {
        return Texture3D::read<Target>(OC_FORWARD(args)...);
    }

    template<typename... Args>
    OC_NODISCARD auto write(Args &&...args) noexcept {
        return Texture3D::write(OC_FORWARD(args)...);
    }
};
}// namespace ocarina