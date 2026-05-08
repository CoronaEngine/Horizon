//
// Created by Zero on 24/07/2022.
//

#pragma once

#include "core/stl.h"
#include "math/basic_types.h"
#include "core/concepts.h"

namespace ocarina {

enum ColorSpace {
    LINEAR,
    SRGB
};

enum EToneMap {
    Gamma,
    Filmic,
    Reinhard,
    Linear
};

enum struct PixelStorage : uint {
    BYTE1,
    BYTE2,
    BYTE4,

    UINT1,
    UINT2,
    UINT4,

    FLOAT1,
    FLOAT2,
    FLOAT4,

    UNKNOWN
};

enum struct ImageWrap : uint8_t {
    Repeat,
    Black,
    Clamp
};

namespace detail {

template<typename T>
struct PixelStorageImpl {

    template<typename U>
    static constexpr auto always_false = false;

    static_assert(always_false<T>, "Unsupported type for pixel format.");
};

#define MAKE_PIXEL_FORMAT_OF_TYPE(Type, f)               \
    template<>                                           \
    struct PixelStorageImpl<Type> {                      \
        static constexpr auto storage = PixelStorage::f; \
        static constexpr auto pixel_size = sizeof(Type); \
    };

MAKE_PIXEL_FORMAT_OF_TYPE(uchar, BYTE1)
MAKE_PIXEL_FORMAT_OF_TYPE(uchar2, BYTE2)
MAKE_PIXEL_FORMAT_OF_TYPE(uchar4, BYTE4)
MAKE_PIXEL_FORMAT_OF_TYPE(float, FLOAT1)
MAKE_PIXEL_FORMAT_OF_TYPE(float2, FLOAT2)
MAKE_PIXEL_FORMAT_OF_TYPE(float4, FLOAT4)

#undef MAKE_PIXEL_FORMAT_OF_TYPE
}// namespace detail

template<typename T>
using PixelStorageImpl = detail::PixelStorageImpl<T>;

OC_NDSC_INLINE size_t pixel_size(PixelStorage pixel_storage) noexcept {
    switch (pixel_storage) {
        case PixelStorage::BYTE1: return sizeof(uchar);
        case PixelStorage::BYTE2: return sizeof(uchar2);
        case PixelStorage::BYTE4: return sizeof(uchar4);
        case PixelStorage::FLOAT1: return sizeof(float);
        case PixelStorage::FLOAT2: return sizeof(float2);
        case PixelStorage::FLOAT4: return sizeof(float4);
        case PixelStorage::UINT1: break;
        case PixelStorage::UINT2: break;
        case PixelStorage::UINT4: break;
        case PixelStorage::UNKNOWN: break;
    }
    OC_ASSERT(0);
    return 0;
}

OC_NDSC_INLINE bool is_8bit(PixelStorage pixel_format) noexcept {
    return pixel_format == PixelStorage::BYTE1 || pixel_format == PixelStorage::BYTE2 || pixel_format == PixelStorage::BYTE4;
}

OC_NDSC_INLINE bool is_32bit(PixelStorage pixel_format) noexcept {
    return pixel_format == PixelStorage::FLOAT1 || pixel_format == PixelStorage::FLOAT2 || pixel_format == PixelStorage::FLOAT4;
}

OC_NDSC_INLINE size_t channel_num(PixelStorage pixel_storage) {
    if (pixel_storage == PixelStorage::BYTE1 || pixel_storage == PixelStorage::FLOAT1) { return 1u; }
    if (pixel_storage == PixelStorage::BYTE2 || pixel_storage == PixelStorage::FLOAT2) { return 2u; }
    return 4u;
}

OC_NDSC_INLINE uint32_t format_size_in_bytes(PixelStorage pixel_storage) {
    switch (pixel_storage) {
        case ocarina::PixelStorage::BYTE1:
            return 1;
        case ocarina::PixelStorage::BYTE2:
            return 2;
        case ocarina::PixelStorage::BYTE4:
            return 4;
        case ocarina::PixelStorage::UINT1:
        case ocarina::PixelStorage::FLOAT1:
            return 4;
        case ocarina::PixelStorage::UINT2:
        case ocarina::PixelStorage::FLOAT2:
            return 8;
        case ocarina::PixelStorage::UINT4:
        case ocarina::PixelStorage::FLOAT4:
            return 16;
        case ocarina::PixelStorage::UNKNOWN:
            return 0;
        default:
            return 4;
    }
}

class OC_CORE_API ImageBase : public concepts::Noncopyable {
protected:
    PixelStorage pixel_storage_{PixelStorage::UNKNOWN};
    uint2 resolution_{};
    vector<float> average_{};

public:
    ImageBase(PixelStorage pixel_format, uint2 resolution)
        : pixel_storage_(pixel_format),
          resolution_(resolution) {
        average_.resize(channel_num());
    }
    ImageBase(ImageBase &&other) noexcept {
        pixel_storage_ = other.pixel_storage_;
        resolution_ = other.resolution_;
        average_ = ocarina::move(other.average_);
    }
    ImageBase() = default;
    ImageBase &operator=(ImageBase &&) = default;
    [[nodiscard]] int channel_num() const { return ::ocarina::channel_num(pixel_storage_); }
    [[nodiscard]] uint2 resolution() const { return resolution_; }
    [[nodiscard]] uint width() const { return resolution_.x; }
    [[nodiscard]] uint height() const { return resolution_.y; }

    template<size_t N = 4>
    [[nodiscard]] const auto &average() const noexcept {
        OC_ASSERT(N <= channel_num());
        if constexpr (N == 1) {
            return *(reinterpret_cast<const float *>(average_.data()));
        } else {
            return *(reinterpret_cast<const Vector<float, N> *>(average_.data()));
        }
    }
    template<size_t N = 4>
    [[nodiscard]] auto &average() noexcept {
        OC_ASSERT(N <= channel_num());
        if constexpr (N == 1) {
            return *(reinterpret_cast<float *>(average_.data()));
        } else {
            return *(reinterpret_cast<Vector<float, N> *>(average_.data()));
        }
    }
    [[nodiscard]] auto average_vector() const noexcept { return average_; }
    [[nodiscard]] PixelStorage pixel_storage() const { return pixel_storage_; }
    [[nodiscard]] size_t pitch_byte_size() const { return resolution_.x * pixel_size(pixel_storage_); }
    [[nodiscard]] size_t pixel_num() const { return resolution_.x * resolution_.y; }
    [[nodiscard]] size_t size_in_bytes() const {
        return pixel_size(pixel_storage_) * pixel_num();
    }
};

}// namespace ocarina