//
// Created by zhu on 2023/4/21.
//

#pragma once

#include "core/util/util.h"
#include "../core/type_trait.h"
#include "dynamic_array.h"

namespace ocarina {

template<typename T>
class RegistrableManaged;

using buffer_ty = uint;

namespace detail {
template<typename U = buffer_ty>
requires(sizeof(U) == sizeof(float))
struct data_accessor_impl {
    mutable Uint offset;
    const RegistrableManaged<U> &datas;

    template<typename T>
    [[nodiscard]] DynamicArray<T> load_dynamic_array(uint size) const noexcept {
        auto ret = datas.template load_dynamic_array<T>(size, offset);
        offset += size * static_cast<uint>(sizeof(T));
        return ret;
    }

    template<typename Target>
    OC_NODISCARD auto byte_read() const noexcept {
        auto ret = datas.template byte_read<Target>(offset);
        offset += static_cast<uint>(sizeof(Target));
        return ret;
    }
};
}// namespace detail

using DataAccessor = detail::data_accessor_impl<>;

class Encodable {
public:
    /// for host
    virtual void encode(RegistrableManaged<buffer_ty> &data) const noexcept {}
    virtual void update(RegistrableManaged<buffer_ty> &data) const noexcept {}
    virtual void update() const noexcept {}
    virtual void invalidate() const noexcept {}
    [[nodiscard]] virtual uint compacted_size() const noexcept { return 0; }
    [[nodiscard]] virtual uint aligned_size() const noexcept {
        return mem_offset(cal_offset(0), alignment());
    }
    [[nodiscard]] virtual bool has_device_value() const noexcept { return true; }
    [[nodiscard]] virtual uint alignment() const noexcept { return sizeof(buffer_ty); }

    /**
     * calculate the offset of current data and store
     * @param prev_size
     * @return the aligned size of current add previous data
     */
    virtual uint cal_offset(uint prev_size) const noexcept { return prev_size; }
    virtual ~Encodable() = default;
    /// for device
    virtual void decode(const DataAccessor *da) const noexcept {}
    virtual void decode(const DynamicArray<buffer_ty> &array) const noexcept {}
    virtual void after_decode() const noexcept { on_after_decode(); }
    virtual void on_after_decode() const noexcept {}
};

enum EncodeType {
    Original,
    Uint8
};

template<typename value_ty, typename T = buffer_ty>
requires(is_std_vector_v<value_ty> && is_scalar_v<typename value_ty::value_type>) || is_basic_v<value_ty> ||
        (is_array_v<value_ty> && std::is_same_v<array_element_t<value_ty>, float>)
struct EncodedData final : public Encodable {
public:
    using host_ty = std::variant<value_ty, std::function<value_ty()>>;
    using buffer_type = T;
    static constexpr size_t max_alignment = alignof(uint);
    static constexpr uint buffer_stride = sizeof(buffer_ty);

private:
    host_ty host_value_{};
    optional<dsl_t<value_ty>> device_value_{};
    EncodeType encode_type_{Original};

    /// origin offset in buffer
    mutable uint offset_{InvalidUI32};
    mutable RegistrableManaged<T> *data_{nullptr};

public:
    explicit EncodedData(value_ty val = value_ty{}, EncodeType et = Original)
        : host_value_(std::move(val)), encode_type_(et) {}
    EncodedData &operator=(const value_ty &val) {
        host_value_ = val;
        return *this;
    }
    EncodedData &operator=(const std::function<value_ty()> &val) {
        host_value_ = val;
        return *this;
    }
    OC_MAKE_MEMBER_GETTER_SETTER(encode_type, )
    [[nodiscard]] bool has_device_value() const noexcept override { return device_value_.has_value(); }
    void after_decode() const noexcept override {
        Encodable::after_decode();
        (const_cast<decltype(device_value_) &>(device_value_)).reset();
    }
    [[nodiscard]] value_ty hv() const noexcept {
        if (host_value_.index() == 0) {
            return std::get<0>(host_value_);
        } else {
            return std::get<1>(host_value_)();
        }
    }
    [[nodiscard]] value_ty &hv() noexcept {
        OC_ASSERT(host_value_.index() == 0);
        return std::get<0>(host_value_);
    }
    [[nodiscard]] const dsl_t<value_ty> &dv() const noexcept {
        OC_ASSERT(has_device_value());
        return *device_value_;
    }
    [[nodiscard]] dsl_t<value_ty> &dv() noexcept {
        OC_ASSERT(has_device_value());
        return *device_value_;
    }
    [[nodiscard]] uint alignment() const noexcept override {
        switch (encode_type_) {
            case Original:
                return 4;
            case Uint8: {
                return 1;
            }
        }
        OC_ASSERT(false);
        return 1;
    }
    [[nodiscard]] dsl_t<value_ty> operator*() const noexcept {
        if (has_device_value()) {
            return dv();
        } else {
            return dsl_t<value_ty>(hv());
        }
    }

    [[nodiscard]] bool has_offset() const noexcept { return offset_ != InvalidUI32; }
    void invalidate() const noexcept override { offset_ = InvalidUI32; }

    template<typename Scalar>
    requires is_scalar_v<Scalar>
    void encode_scalar(RegistrableManaged<T> &data, Scalar scalar, uint i = 0u) const noexcept {
        switch (encode_type_) {
            case Original: {
                uint index = offset_ / sizeof(buffer_ty);
                assert(index + i < data.host_buffer().size());
                data.host_buffer()[index + i] = bit_cast<T>(scalar);
                break;
            }
            case Uint8: {
                OC_ASSERT(is_floating_point_v<Scalar> && scalar <= 1.f);
                uint index = (offset_ + i) / buffer_stride;
                uint ofs = (offset_ + i) % buffer_stride;
                uint val = scalar * 255;
                buffer_ty element = data.host_buffer()[index];
                uint mask = 0;
                val = val << (8 * (3 - ofs));
                mask = ~(0xff000000 >> (ofs * 8));
                element = mask & element;
                element = val | element;
                data.host_buffer()[index] = element;
                break;
            }
            default: break;
        }
    }

    void update(RegistrableManaged<T> &data) const noexcept override {
        OC_ASSERT(has_offset());
        if constexpr (is_scalar_v<value_ty>) {
            encode_scalar(data, hv());
        } else if constexpr (is_vector_v<value_ty>) {
            for (int i = 0; i < vector_dimension_v<value_ty>; ++i) {
                encode_scalar(data, hv()[i], i);
            }
        } else if constexpr (is_matrix_v<value_ty>) {
            uint count = 0;
            for (int i = 0; i < matrix_dimension_v<value_ty>; ++i) {
                for (int j = 0; j < matrix_dimension_v<value_ty>; ++j) {
                    encode_scalar(data, hv()[i][j], count);
                    ++count;
                }
            }
        } else if constexpr (is_std_vector_v<value_ty>) {
            for (int i = 0; i < hv().size(); ++i) {
                encode_scalar(data, hv()[i], i);
            }
        } else if constexpr (is_array_v<value_ty>) {
            for (int i = 0; i < array_dimension_v<value_ty>; ++i) {
                encode_scalar(data, hv()[i], i);
            }
        } else {
            static_assert(always_false_v<value_ty>);
        }
    }

    void update() const noexcept override {
        update(*data_);
    }

    void encode(RegistrableManaged<T> &data) const noexcept override {
        data_ = addressof(data);
        update(data);
    }

    [[nodiscard]] uint cal_offset(ocarina::uint prev_size) const noexcept override {
        offset_ = mem_offset(prev_size, alignment());
        uint ret = offset_ + compacted_size();
        return ret;
    }

    [[nodiscard]] uint compacted_size() const noexcept override {
        switch (encode_type_) {
            case Original: {
                if constexpr (is_scalar_v<value_ty>) {
                    static_assert(sizeof(value_ty) <= sizeof(float));
                    return sizeof(value_ty);
                } else if constexpr (is_vector_v<value_ty>) {
                    return sizeof(typename value_ty::scalar_type) * value_ty::dimension;
                } else if constexpr (is_matrix_v<value_ty>) {
                    return sizeof(typename value_ty::scalar_type) * value_ty::element_num;
                } else if constexpr (is_std_vector_v<value_ty>) {
                    return hv().size() * sizeof(typename value_ty::value_type);
                } else if constexpr (is_array_v<value_ty>) {
                    return array_dimension_v<value_ty> * sizeof(typename value_ty::value_type);
                } else {
                    static_assert(always_false_v<value_ty>);
                }
            }
            case Uint8: {
                if constexpr (is_scalar_v<value_ty>) {
                    static_assert(sizeof(value_ty) <= sizeof(float));
                    return sizeof(uint8_t);
                } else if constexpr (is_vector_v<value_ty>) {
                    return sizeof(uint8_t) * value_ty::dimension;
                } else if constexpr (is_matrix_v<value_ty>) {
                    return sizeof(uint8_t) * value_ty::element_num;
                } else if constexpr (is_std_vector_v<value_ty>) {
                    return hv().size() * sizeof(uint8_t);
                } else if constexpr (is_array_v<value_ty>) {
                    return array_dimension_v<value_ty> * sizeof(uint8_t);
                } else {
                    static_assert(always_false_v<value_ty>);
                }
            }
            default: break;
        }
        OC_ASSERT(0);
        return 0;
    }

    [[nodiscard]] uint stride() const noexcept {
        switch (encode_type_) {
            case Original: return 4;
            case Uint8: return 1;
            default: break;
        }
        OC_ASSERT(0);
        return 0;
    }

    template<typename Scalar>
    [[nodiscard]] Var<Scalar> decode_scalar(const DynamicArray<T> &array, const Uint &offset) const noexcept {
        Uint index = offset / buffer_stride;
        Var<Scalar> ret;
        switch (encode_type_) {
            case Original: {
                ret = as<Scalar>(array[index]);
                break;
            }
            case Uint8: {
                Uint sub_offset = offset % buffer_stride;
                Var<T> elm = array[index];
                Uint mask = 0xff000000 >> (sub_offset * 8);
                elm = elm & mask;
                elm = elm >> ((3 - sub_offset) * 8);
                ret = elm * (1.f / 255);
                break;
            }
            default: {
                OC_ASSERT(0);
                break;
            }
        }
        return ret;
    }

    [[nodiscard]] auto _decode(const DynamicArray<T> &array, const Uint &offset) const noexcept {
        if constexpr (is_scalar_v<value_ty>) {
            return decode_scalar<value_ty>(array, offset);
        } else if constexpr (is_vector_v<value_ty>) {
            Var<value_ty> ret;
            using element_ty = vector_element_t<value_ty>;
            for (int i = 0; i < vector_dimension_v<value_ty>; ++i) {
                ret[i] = decode_scalar<element_ty>(array, offset + i * stride());
            }
            return ret;
        } else if constexpr (is_matrix_v<value_ty>) {
            Var<value_ty> ret;
            uint cursor = 0u;
            for (int i = 0; i < value_ty::col_num; ++i) {
                for (int j = 0; j < value_ty::row_num; ++j) {
                    ret[i][j] = decode_scalar<float>(array, offset + cursor * stride());
                    ++cursor;
                }
            }
            return ret;
        } else if constexpr (is_std_vector_v<value_ty>) {
            using element_ty = typename value_ty::value_type;
            DynamicArray<element_ty> ret{hv().size()};
            for (int i = 0; i < hv().size(); ++i) {
                ret[i] = decode_scalar<element_ty>(array, offset + i * stride());
            }
            return ret;
        } else if constexpr (is_array_v<value_ty>) {
            using element_ty = typename value_ty::value_type;
            Var<value_ty> ret;
            for (int i = 0; i < array_dimension_v<value_ty>; ++i) {
                ret[i] = decode_scalar<element_ty>(array, offset + i * stride());
            }
            return ret;
        } else {
            static_assert(always_false_v<value_ty>);
        }
    }

    void decode(const DataAccessor *da) const noexcept override {
        const DynamicArray<T> array = da->template load_dynamic_array<T>(compacted_size() / sizeof(T));
        const_cast<decltype(device_value_) *>(&device_value_)->emplace(_decode(array, 0));
    }

    void decode(const DynamicArray<ocarina::buffer_ty> &array) const noexcept override {
        const_cast<decltype(device_value_) *>(&device_value_)->emplace(_decode(array, offset_));
    }
};

namespace detail {
OC_MAKE_AUTO_MEMBER_FUNC(encode)
OC_MAKE_AUTO_MEMBER_FUNC(invalidate)
OC_MAKE_AUTO_MEMBER_FUNC(update)
OC_MAKE_AUTO_MEMBER_FUNC(decode)
OC_MAKE_AUTO_MEMBER_FUNC(after_decode)
OC_MAKE_AUTO_MEMBER_FUNC(has_device_value)
OC_MAKE_AUTO_MEMBER_FUNC(compacted_size)
OC_MAKE_AUTO_MEMBER_FUNC(cal_offset)
OC_MAKE_AUTO_MEMBER_FUNC(alignment)
}// namespace detail

#define OC_ENCODE_ELEMENT(name) ocarina::detail::encode(name, datas);
#define OC_UPDATE_ELEMENT(name) ocarina::detail::update(name, datas);
#define OC_INVALIDATE_ELEMENT(name) ocarina::detail::invalidate(name);
#define OC_DECODE_ELEMENT_DA(name) ocarina::detail::decode(name, da);
#define OC_DECODE_ELEMENT(name) ocarina::detail::decode(name, array);
#define OC_RESET_DEVICE_ELEMENT(name) ocarina::detail::after_decode(name);
#define OC_VALID_ELEMENT(name) &&ocarina::detail::has_device_value(name)
#define OC_SIZE_ELEMENT(name) +ocarina::detail::compacted_size(name)
#define OC_CAL_OFFSET(name) ret = ocarina::detail::cal_offset(name, ret);
#define OC_ALIGNMENT(name) ret = ocarina::max(ret, ocarina::detail::alignment(name));

#define OC_ENCODABLE_FUNC(Super, ...)                                           \
    [[nodiscard]] uint compacted_size() const noexcept override {               \
        return Super::compacted_size() MAP(OC_SIZE_ELEMENT, __VA_ARGS__);       \
    }                                                                           \
    void encode(RegistrableManaged<buffer_ty> &datas) const noexcept override { \
        Super::encode(datas);                                                   \
        MAP(OC_ENCODE_ELEMENT, __VA_ARGS__)                                     \
    }                                                                           \
    void update(RegistrableManaged<buffer_ty> &datas) const noexcept override { \
        Super::update(datas);                                                   \
        MAP(OC_UPDATE_ELEMENT, __VA_ARGS__)                                     \
    }                                                                           \
    void decode(const DataAccessor *da) const noexcept override {               \
        Super::decode(da);                                                      \
        MAP(OC_DECODE_ELEMENT_DA, __VA_ARGS__)                                  \
    }                                                                           \
    void decode(const DynamicArray<buffer_ty> &array) const noexcept override { \
        Super::decode(array);                                                   \
        MAP(OC_DECODE_ELEMENT, __VA_ARGS__)                                     \
    }                                                                           \
    void invalidate() const noexcept override {                                 \
        Super::invalidate();                                                    \
        MAP(OC_INVALIDATE_ELEMENT, __VA_ARGS__)                                 \
    }                                                                           \
    void after_decode() const noexcept override {                               \
        Super::after_decode();                                                  \
        MAP(OC_RESET_DEVICE_ELEMENT, __VA_ARGS__)                               \
    }                                                                           \
    [[nodiscard]] bool has_device_value() const noexcept override {             \
        return Super::has_device_value() MAP(OC_VALID_ELEMENT, __VA_ARGS__);    \
    }                                                                           \
    uint cal_offset(uint prev_size) const noexcept override {                   \
        uint ret = Super::cal_offset(prev_size);                                \
        MAP(OC_CAL_OFFSET, __VA_ARGS__)                                         \
        return ret;                                                             \
    }                                                                           \
    [[nodiscard]] uint alignment() const noexcept override {                    \
        uint ret = Super::alignment();                                          \
        MAP(OC_ALIGNMENT, __VA_ARGS__)                                          \
        return ret;                                                             \
    }
}// namespace ocarina