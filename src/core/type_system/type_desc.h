//
// Created by Zero on 2024/11/3.
//

#pragma once

#include "core/type.h"
#include "core/util/string_util.h"

namespace horizon::core {
template<typename T>
class Buffer;

template<typename T>
class BufferDesc;

template<typename T>
class BufferView;

class Texture3D;
class Texture2D;
class ByteBuffer;

class Accel;

class BindlessArray;

template<typename T>
struct TypeDesc {
    static_assert(always_false_v<T>, "Invalid type.");
};

template<typename T>
concept generated_storage_type_desc = requires {
    typename T::oc_storage_source_type;
    typename T::oc_storage_tag_type;
    T::description();
    T::name();
};

template<typename T>
requires generated_storage_type_desc<T>
struct TypeDesc<T> {
    static decltype(auto) description() noexcept {
        return T::description();
    }

    static decltype(auto) name() noexcept {
        return T::name();
    }
};

#define OC_MAKE_VECTOR_DESC_NAME(S, N)                                 \
    template<>                                                         \
    struct TypeDesc<Vector<S, N>> {                                    \
        static constexpr horizon::core::string_view description() noexcept { \
            return horizon::core::string_view("vector<" #S "," #N ">");      \
        }                                                              \
        static constexpr horizon::core::string_view name() noexcept {        \
            return horizon::core::string_view(#S #N);                        \
        }                                                              \
    };

#define OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(S)          \
    template<>                                                         \
    struct TypeDesc<S> {                                               \
        static constexpr horizon::core::string_view description() noexcept { \
            using namespace std::string_view_literals;                 \
            return #S##sv;                                             \
        }                                                              \
        static constexpr horizon::core::string_view name() noexcept {        \
            return description();                                      \
        }                                                              \
    };                                                                 \
    OC_MAKE_VECTOR_DESC_NAME(S, 2)                                     \
    OC_MAKE_VECTOR_DESC_NAME(S, 3)                                     \
    OC_MAKE_VECTOR_DESC_NAME(S, 4)

OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(bool)
OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(float)
OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(real)
OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(half)
OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(int)
OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(uint)
OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(uchar)
OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(char)
OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(ulong)
OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION(ushort)

#undef OC_MAKE_VECTOR_DESC_NAME
#undef OC_MAKE_SCALAR_AND_VECTOR_TYPE_DESC_SPECIALIZATION

template<>
struct TypeDesc<void> {
    static constexpr horizon::core::string_view description() noexcept {
        using namespace std::string_view_literals;
        return "void"sv;
    }

    static constexpr horizon::core::string_view name() noexcept {
        return description();
    }
};

template<typename T, size_t N, size_t M>
struct TypeDesc<horizon::core::Matrix<T, N, M>> {
    static horizon::core::string &description() noexcept {
        static thread_local auto s = horizon::core::format(
            "matrix<{},{},{}>", TypeDesc<T>::description(),
            N, M);
        return s;
    }
    static horizon::core::string &name() noexcept {
        static thread_local auto s = horizon::core::format(
            "{}{}x{}", TypeDesc<T>::name(),
            N, M);
        return s;
    }
};

template<typename T, size_t N>
struct TypeDesc<horizon::core::array<T, N>> {
    static_assert(alignof(T) >= 4u);
    static horizon::core::string &description() noexcept {
        static thread_local auto s = horizon::core::format(
            "array<{},{}>",
            TypeDesc<T>::description(), N);
        return s;
    }

    static horizon::core::string_view name() noexcept {
        return description();
    }
};

template<typename T>
struct TypeDesc<Buffer<T>> {
    static_assert(alignof(T) >= 4u);
    static horizon::core::string &description() noexcept {
        static thread_local string str = horizon::core::format("buffer<{}>", TypeDesc<T>::description());
        return str;
    }
    static horizon::core::string_view name() noexcept {
        return description();
    }
};

template<typename T>
struct TypeDesc<BufferDesc<T>> : public TypeDesc<Buffer<T>> {};

template<>
struct TypeDesc<ByteBuffer> {
    static horizon::core::string_view description() noexcept {
        return "bytebuffer";
    }
    static horizon::core::string_view name() noexcept {
        return description();
    }
};

template<>
struct TypeDesc<BufferDesc<>> : public TypeDesc<ByteBuffer> {};

template<>
struct TypeDesc<Texture3D> {
    static horizon::core::string_view description() noexcept {
        return "texture3d";
    }
    static horizon::core::string_view name() noexcept {
        return description();
    }
};

template<>
struct TypeDesc<Texture2D> {
    static horizon::core::string_view description() noexcept {
        return "texture2d";
    }
    static horizon::core::string_view name() noexcept {
        return description();
    }
};

template<>
struct TypeDesc<Accel> {
    static horizon::core::string_view description() noexcept {
        return "accel";
    }
    static horizon::core::string_view name() noexcept {
        return description();
    }
};

template<typename T, size_t N>
struct TypeDesc<T[N]> : public TypeDesc<horizon::core::array<T, N>> {};

template<typename... T>
struct TypeDesc<horizon::core::tuple<T...>> {
    static horizon::core::string &description() noexcept {
        static thread_local horizon::core::string str = []() -> horizon::core::string {
            auto ret = horizon::core::format("struct<_Tuple,{},false,false", alignof(horizon::core::tuple<T...>));
            (ret.append(",").append(TypeDesc<T>::description()), ...);
            ret.append(">");
            return ret;
        }();
        return str;
    }
    static horizon::core::string_view name() noexcept {
        return description();
    }
};

#define OC_MAKE_STRUCT_MEMBER_FMT(member) ",{}"

#define OC_MAKE_STRUCT_MEMBER_DESC(member) \
    horizon::core::TypeDesc<std::remove_cvref_t<decltype(this_type::member)>>::description()

#define OC_MAKE_STRUCT_DESC(S, ...)                                                         \
    template<>                                                                              \
    struct horizon::core::TypeDesc<S> {                                                           \
        using this_type = S;                                                                \
        static horizon::core::string description() noexcept {                                     \
            static thread_local horizon::core::string s = horizon::core::format(                        \
                "struct<" #S ",{},{},{}" MAP(OC_MAKE_STRUCT_MEMBER_FMT, ##__VA_ARGS__) ">", \
                alignof(this_type), horizon::core::is_builtin_struct_v<this_type>,                \
                horizon::core::is_param_struct_v<this_type>,                                      \
                MAP_LIST(OC_MAKE_STRUCT_MEMBER_DESC, ##__VA_ARGS__));                       \
            return s;                                                                       \
        }                                                                                   \
        static constexpr string_view name() noexcept {                                      \
            return #S;                                                                      \
        }                                                                                   \
    };

template<>
struct TypeDesc<BindlessArray> {
    static horizon::core::string_view description() noexcept {
        return "bindlessArray";
    }
    static horizon::core::string_view name() noexcept {
        return description();
    }
};

#define OC_IS_DYNAMIC_SIZE(member, S) \
    horizon::core::is_dynamic_size<std::remove_cvref_t<decltype(S::member)>>

#define OC_MAKE_STRUCT_IS_DYNAMIC(S, ...) \
    template<>                            \
    struct horizon::core::is_dynamic_size<S> : std::disjunction<MAP_LIST_UD(OC_IS_DYNAMIC_SIZE, S, ##__VA_ARGS__)> {};

template<typename T>
const Type *Type::of() noexcept {
    using raw_type = std::remove_cvref_t<T>;
    const Type *ret = Type::from(TypeDesc<raw_type>::description());
    if constexpr (horizon::core::is_struct_v<T>) {
        if constexpr (requires {
                          horizon::core::struct_member_tuple<raw_type>::members;
                      }) {
            constexpr auto arr = horizon::core::struct_member_tuple<raw_type>::members;
            constexpr int num = sizeof(horizon::core::struct_member_tuple<raw_type>::members) / sizeof(arr[0]);
            const_cast<Type *>(ret)->update_member_name(arr, num);
        }
        using member_tuple = typename horizon::core::struct_member_tuple<raw_type>::type;
        for_each_struct_member_type<raw_type>([&](auto elm) {
            using elm_t = decltype(elm);
            auto t = Type::of<elm_t>();
        });
    }
    return ret;
}

template<typename T>
[[nodiscard]] string to_str(const T &val) noexcept {
    static string type_string = string(TypeDesc<T>::name());
    if constexpr (is_vector2_v<T>) {
        return horizon::core::format(type_string + "({}, {})", to_str(val.x), to_str(val.y));
    } else if constexpr (is_vector3_v<T>) {
        return horizon::core::format(type_string + "({}, {}, {})", to_str(val.x), to_str(val.y), to_str(val.z));
    } else if constexpr (is_vector4_v<T>) {
        return horizon::core::format(type_string + "({}, {}, {}, {})", to_str(val.x), to_str(val.y), to_str(val.z), to_str(val.w));
    } else if constexpr (is_matrix2_v<T>) {
        return horizon::core::format("[{},\n {}]", to_str(val[0]), to_str(val[1]));
    } else if constexpr (is_matrix3_v<T>) {
        return horizon::core::format("[{},\n {},\n {}]", to_str(val[0]), to_str(val[1]), to_str(val[2]));
    } else if constexpr (is_matrix4_v<T>) {
        return horizon::core::format("[{},\n {},\n {},\n {}]", to_str(val[0]), to_str(val[1]), to_str(val[2]), to_str(val[3]));
    } else if constexpr (is_scalar_v<T>) {
        if constexpr (is_half_v<T>) {
            return std::to_string(half2float(val));
        } else {
            return std::to_string(val);
        }
    } else if constexpr (is_struct_v<T>) {
        string ret = horizon::core::format("{}[", struct_member_tuple<T>::struct_name);
        for_each_struct_member(val, [&](const auto &elm, uint index) {
            if (index == struct_member_tuple<T>::offset_array.size() - 1) {
                ret += to_str(elm);
            } else {
                ret += to_str(elm) + ",";
            }
        });
        return ret + "]";
    } else {
        static_assert(always_false_v<T>);
        return "";
    }
}

}// namespace horizon::core