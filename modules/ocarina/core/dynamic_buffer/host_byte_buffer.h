//
// Created by Z on 12/04/2026.
//

#pragma once

#include "core/stl.h"

namespace ocarina {

class HostByteBuffer {
private:
    vector<std::byte> bytes_{};

    [[nodiscard]] bool is_valid_range(size_t offset, size_t size) const noexcept {
        return offset <= bytes_.size() && size <= bytes_.size() - offset;
    }

public:
    HostByteBuffer() = default;
    explicit HostByteBuffer(size_t size)
        : bytes_(size) {}

    void resize(size_t size) noexcept { bytes_.resize(size); }
    void reserve(size_t size) noexcept { bytes_.reserve(size); }
    void clear() noexcept { bytes_.clear(); }
    void reset(size_t size = 0) noexcept {
        bytes_.clear();
        bytes_.resize(size);
    }

    [[nodiscard]] size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] size_t capacity() const noexcept { return bytes_.capacity(); }
    [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }

    [[nodiscard]] std::byte *data() noexcept { return bytes_.data(); }
    [[nodiscard]] const std::byte *data() const noexcept { return bytes_.data(); }

    [[nodiscard]] span<std::byte> bytes_span() noexcept { return bytes_; }
    [[nodiscard]] span<const std::byte> bytes_span() const noexcept { return bytes_; }

    [[nodiscard]] span<std::byte> subspan(size_t offset, size_t size) noexcept {
        OC_ASSERT(is_valid_range(offset, size));
        return bytes_span().subspan(offset, size);
    }

    [[nodiscard]] span<const std::byte> subspan(size_t offset, size_t size) const noexcept {
        OC_ASSERT(is_valid_range(offset, size));
        return bytes_span().subspan(offset, size);
    }

    void copy_from(const void *src, size_t size, size_t dst_offset = 0) noexcept {
        OC_ASSERT(src != nullptr || size == 0);
        OC_ASSERT(is_valid_range(dst_offset, size));
        if (size == 0) {
            return;
        }
        oc_memcpy(data() + dst_offset, src, size);
    }

    void copy_to(void *dst, size_t size, size_t src_offset = 0) const noexcept {
        OC_ASSERT(dst != nullptr || size == 0);
        OC_ASSERT(is_valid_range(src_offset, size));
        if (size == 0) {
            return;
        }
        oc_memcpy(dst, data() + src_offset, size);
    }

    template<typename T>
    [[nodiscard]] T *data_as(size_t offset = 0) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        OC_ASSERT(is_valid_range(offset, sizeof(T)) || (sizeof(T) == 0 && offset <= bytes_.size()));
        return reinterpret_cast<T *>(data() + offset);
    }

    template<typename T>
    [[nodiscard]] const T *data_as(size_t offset = 0) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        OC_ASSERT(is_valid_range(offset, sizeof(T)) || (sizeof(T) == 0 && offset <= bytes_.size()));
        return reinterpret_cast<const T *>(data() + offset);
    }

    template<typename T>
    void store(size_t offset, const T &value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        OC_ASSERT(is_valid_range(offset, sizeof(T)));
        oc_memcpy(data() + offset, addressof(value), sizeof(T));
    }

    template<typename T>
    [[nodiscard]] T load(size_t offset) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        OC_ASSERT(is_valid_range(offset, sizeof(T)));
        T value{};
        oc_memcpy(addressof(value), data() + offset, sizeof(T));
        return value;
    }
};

}// namespace ocarina