//
// Created by Zero on 30/04/2022.
//

#pragma once

#include "header.h"
#include <functional>
#include <deque>
#include <stack>
#include <queue>
#include <variant>
#include <span>
#include <vector>
#include <map>
#include <set>
#include <array>
#include <list>
#include <optional>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <string>
#include <unordered_set>
#include <string_view>
#include <filesystem>
#include <source_location>
#include <EASTL/tuple.h>
#include <EASTL/string.h>
#include <sys/stat.h>

#define OC_FORWARD(arg) std::forward<decltype(arg)>(arg)

#define OC_APPEND_SRC_LOCATION std::source_location src_location = std::source_location::current()
#define OC_SRC_LOCATION src_location

#define OC_DEFINE_TEMPLATE_VALUE(template_name) \
    template<typename T>                        \
    static constexpr auto template_name##_v = template_name<T>::value;

#define OC_DEFINE_TEMPLATE_VALUE_MULTI(template_name) \
    template<typename... Ts>                          \
    static constexpr auto template_name##_v = template_name<Ts...>::value;

#define OC_DEFINE_TEMPLATE_TYPE(template_name) \
    template<typename T>                       \
    using template_name##_t = typename template_name<T>::type;

#define OC_DEFINE_TEMPLATE_TYPE_MULTI(template_name) \
    template<typename... Ts>                         \
    using template_name##_t = typename template_name<Ts...>::type;

namespace ocarina {

namespace detail {
OC_CORE_API void *allocator_allocate(size_t size, size_t alignment) noexcept;
OC_CORE_API void allocator_deallocate(void *p, size_t alignment) noexcept;
OC_CORE_API void *allocator_reallocate(void *p, size_t size, size_t alignment) noexcept;
}// namespace detail

template<typename... T>
struct always_false : std::false_type {};

template<typename... T>
constexpr auto always_false_v = always_false<T...>::value;

template<typename... T>
struct always_true : std::true_type {};

template<typename... T>
constexpr auto always_true_v = always_true<T...>::value;

template<typename T = std::byte>
struct allocator {
    using value_type = T;
    constexpr allocator() noexcept = default;
    template<typename U>
    constexpr explicit allocator(allocator<U>) noexcept {}
    [[nodiscard]] auto allocate(std::size_t n) const noexcept {
        return static_cast<T *>(detail::allocator_allocate(sizeof(T) * n, alignof(T)));
    }
    void deallocate(T *p, size_t) const noexcept {
        detail::allocator_deallocate(p, alignof(T));
    }
    template<typename R>
    [[nodiscard]] constexpr bool operator==(allocator<R>) const noexcept {
        return std::is_same_v<T, R>;
    }
};

template<typename T = std::byte>
[[nodiscard]] inline auto allocate(size_t n = 1u, bool use_ea = true) noexcept {
    if (use_ea) {
        return allocator<T>{}.allocate(n);
    } else {
        return reinterpret_cast<T *>(_aligned_malloc(sizeof(T) * n, alignof(T)));
    }
}

template<typename T>
inline void deallocate(T *p) {
    using type = std::remove_cvref_t<T>;
    allocator<type>{}.deallocate(const_cast<type *>(p), 0u);
}

template<typename T, typename... Args>
constexpr T *construct_at(T *p, Args &&...args) {
    return ::new (const_cast<void *>(static_cast<const volatile void *>(p)))
        T(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
[[nodiscard]] inline auto new_with_allocator(Args &&...args) {
    return ocarina::construct_at(allocate<T>(), std::forward<Args>(args)...);
}

template<typename T>
inline void delete_with_allocator(T *p, bool use_ea = true) noexcept {
    if (p == nullptr) {
        return;
    }
    std::destroy_at(p);
    if (use_ea) {
        deallocate(p);
    } else {
        _aligned_free(p);
    }
}

template<typename T = std::byte>
OC_NODISCARD T *new_array(size_t num) noexcept {
    return new T[num];
}

template<typename T>
void delete_array(T *ptr) noexcept {
    delete[] ptr;
}

// io
using std::cerr;
using std::cout;
using std::endl;

// ptr
using eastl::move_only_function;
using std::const_pointer_cast;
using std::dynamic_pointer_cast;
using std::enable_shared_from_this;
using std::function;
using std::make_shared;
using std::make_unique;
using std::reinterpret_pointer_cast;
using std::shared_ptr;
using std::static_pointer_cast;
using std::unique_ptr;
using std::weak_ptr;

template<typename To, typename From>
[[nodiscard]] std::unique_ptr<To> dynamic_unique_pointer_cast(std::unique_ptr<From> &&from) {
    if (To *casted = dynamic_cast<To *>(from.get())) {
        from.release();
        return std::unique_ptr<To>(casted);
    } else {
        return std::unique_ptr<To>(nullptr);
    }
}

template<typename To, typename From>
[[nodiscard]] std::unique_ptr<To> static_unique_pointer_cast(std::unique_ptr<From> &&from) {
    To *casted = static_cast<To *>(from.get());
    from.release();
    return std::unique_ptr<To>(casted);
}

struct unique_allocator {
    unique_allocator() = default;
    template<typename T>
    [[nodiscard]] T *allocate(size_t n) const noexcept {
        return static_cast<T *>(::operator new(n * sizeof(T)));
    }
    template<typename T>
    void deallocate(T *p, size_t) const noexcept {
        deallocate(p);
    }
};

template<typename T, typename Alloc>
struct unique_deleter {
    Alloc alloc;
    void operator()(T *ptr) {
        if (ptr) {
            ptr->~T();
            alloc.deallocate(ptr, 1);
        }
    }
};

template<typename T, typename Alloc = unique_allocator, typename... Args>
std::unique_ptr<T> make_unique_with_allocator(Args &&...args) {
    //auto deleter = [](T *p) {
    //    std::destroy_at(p);
    //    deallocate(p);
    //};
    Alloc alloc;
    //return {ptr, std::bind(deleter, std::placeholders::_1, allocate<T>, 1)};
    T *rawPtr = alloc.template allocate<T>(1);
    new (rawPtr) T(std::forward<Args>(args)...);
    return std::unique_ptr<T>(rawPtr);//std::unique_ptr<T, unique_deleter<T, Alloc>>(rawPtr, unique_deleter<T, Alloc>{alloc});
}

template<typename... Ts>
using UP = unique_ptr<Ts...>;

template<typename T>
using SP = shared_ptr<T>;

template<typename T>
using WP = weak_ptr<T>;

inline void oc_memcpy(void *dst, const void *src, size_t size) {
#ifdef _MSC_VER
    std::memcpy(dst, src, size);
#else
    std::wmemcpy(reinterpret_cast<wchar_t *>(dst), reinterpret_cast<const wchar_t *>(src), size);
#endif
}

struct MemoryBlock {
public:
    const void *address{};
    size_t size{};
    size_t alignment{};
    size_t max_member_size = 8;

public:
    MemoryBlock() = default;
    MemoryBlock(const void *address, size_t size,
                size_t alignment, size_t max_member_size)
        : address(address), size(size),
          alignment(alignment), max_member_size(max_member_size) {}
};

// string
using std::string;
using std::string_view;
using std::to_string;

template<class To, class From>
requires(sizeof(To) == sizeof(From) &&
         std::is_trivially_copyable_v<From> &&
         std::is_trivially_copyable_v<To>)
[[nodiscard]] constexpr To bit_cast(const From &src) noexcept {
    return *reinterpret_cast<const To *>(&src);
}

inline size_t substr_count(string_view str, string_view target) noexcept {
    string::size_type pos = 0;
    size_t ret = 0;
    while ((pos = str.find(target, pos)) != string::npos) {
        ret += 1;
        pos += target.length();
    }
    return ret;
}

// range and container
using std::array;
using std::deque;
using std::list;
using std::map;
using std::multimap;
using std::optional;
using std::queue;
using std::set;
using std::span;
using std::stack;
using std::unordered_map;
using std::unordered_set;
using std::vector;

#if 1
// tuple
using eastl::get;
using eastl::tuple;
#else
using std::get;
using std::tuple;
#endif

namespace detail {
template<typename T>
struct tuple_size_impl {
    static_assert(ocarina::always_false_v<T>);
};

template<typename... Ts>
struct tuple_size_impl<std::tuple<Ts...>> : public std::tuple_size<std::tuple<Ts...>> {};

template<typename... Ts>
struct tuple_size_impl<eastl::tuple<Ts...>> : public eastl::tuple_size<eastl::tuple<Ts...>> {};
}// namespace detail
template<typename T>
using tuple_size = typename detail::tuple_size_impl<std::remove_cvref_t<T>>;

template<typename T>
constexpr auto tuple_size_v = tuple_size<T>::value;

template<size_t i, typename T>
struct tuple_element {
    static_assert(ocarina::always_false_v<T>);
};

template<size_t i, typename... Ts>
struct tuple_element<i, std::tuple<Ts...>> : public std::tuple_element<i, std::tuple<Ts...>> {};

template<size_t i, typename... Ts>
struct tuple_element<i, eastl::tuple<Ts...>> : public eastl::tuple_element<i, eastl::tuple<Ts...>> {};

template<size_t i, typename T>
using tuple_element_t = typename tuple_element<i, T>::type;

template<size_t i, typename... Ts>
auto tuple_get(const std::tuple<Ts...> &tp) noexcept {
    return std::get<i>(tp);
}

template<size_t i, typename... Ts>
auto tuple_get(const eastl::tuple<Ts...> &tp) noexcept {
    return eastl::get<i>(tp);
}

template<size_t i = 0, typename Tuple, typename Func>
void traverse_tuple(Tuple &&tuple, Func &&func) noexcept {
    if constexpr (i < tuple_size_v<Tuple>) {
        if constexpr (std::invocable<Func, decltype(ocarina::tuple_get<i>(OC_FORWARD(tuple))), size_t>) {
            func(ocarina::tuple_get<i>(OC_FORWARD(tuple)), i);
        } else {
            func(ocarina::tuple_get<i>(OC_FORWARD(tuple)));
        }
        traverse_tuple<i + 1>(OC_FORWARD(tuple), OC_FORWARD(func));
    }
}

// sequence
using std::index_sequence;
using std::index_sequence_for;
using std::integer_sequence;
using std::make_index_sequence;
using std::make_integer_sequence;

// other
using std::addressof;
using std::function;
using std::make_pair;
using std::monostate;
using std::move;
using std::pair;
using std::variant;
using std::visit;
namespace fs = std::filesystem;

[[nodiscard]] fs::path parent_path(const fs::path &p, int levels);

void clear_directory(const std::filesystem::path &dir_path);

std::string get_file_name(const std::string &file_path);

inline std::string get_file_directory(const std::string &file_path) {
    std::string file_name = get_file_name(file_path);
    return file_path.substr(0, file_path.length() - file_name.length());
}

[[nodiscard]] std::string wstring_to_string(const wchar_t *source);

inline std::wstring string_to_wstring(const std::string &source) {
    return std::wstring(source.begin(), source.end());
}

inline bool is_file_exist(const char *full_file_path) {
    struct stat buffer;
    return (stat(full_file_path, &buffer) == 0);
}

template<typename T>
struct deep_copy_shared_ptr {
private:
    ocarina::shared_ptr<T> ptr_{};

public:
    deep_copy_shared_ptr() = default;
    template<typename U>
    requires std::is_convertible_v<U *, T *>
    explicit deep_copy_shared_ptr(const shared_ptr<U> &u) : ptr_(u) {}
    template<typename U>
    requires std::is_convertible_v<U *, T *>
    deep_copy_shared_ptr(const deep_copy_shared_ptr<U> &u) : ptr_(u.ptr()) {}
    [[nodiscard]] const T &operator*() const noexcept { return *ptr_; }
    [[nodiscard]] T &operator*() noexcept { return *ptr_; }
    [[nodiscard]] const T *operator->() const noexcept { return ptr_.get(); }
    [[nodiscard]] T *operator->() noexcept { return ptr_.get(); }
    [[nodiscard]] const T *get() const noexcept { return ptr_.get(); }
    [[nodiscard]] T *get() noexcept { return ptr_.get(); }
    [[nodiscard]] const ocarina::shared_ptr<T> &ptr() const noexcept { return ptr_; }
    [[nodiscard]] ocarina::shared_ptr<T> &ptr() noexcept { return ptr_; }

    template<typename Other>
    requires std::is_convertible_v<Other *, T *>
    deep_copy_shared_ptr<T> &operator=(const deep_copy_shared_ptr<Other> &other) noexcept {
        if (ptr_) {
            *ptr_ = *other.ptr();
        } else {
            ptr_ = other.ptr();
        }
        return *this;
    }

    deep_copy_shared_ptr<T> &operator=(const deep_copy_shared_ptr<T> &other) noexcept {
        if (ptr_) {
            *ptr_ = *other.ptr();
        } else {
            ptr_ = other.ptr();
        }
        return *this;
    }
};

template<typename T, typename... Args>
[[nodiscard]] deep_copy_shared_ptr<T> make_deep_copy_shared(Args &&...args) noexcept {
    return deep_copy_shared_ptr<T>(make_shared<T>(OC_FORWARD(args)...));
}

template<typename T>
struct deep_copy_unique_ptr {
private:
    ocarina::unique_ptr<T> ptr_{};

public:
    deep_copy_unique_ptr() = default;
    template<typename U>
    requires std::is_convertible_v<U *, T *>
    explicit deep_copy_unique_ptr(unique_ptr<U> &&u) : ptr_(ocarina::move(u)) {}
    template<typename U>
    requires std::is_convertible_v<U *, T *>
    deep_copy_unique_ptr(deep_copy_unique_ptr<U> &&u) : ptr_(ocarina::move(u.ptr())) {}
    [[nodiscard]] const T &operator*() const noexcept { return *ptr_; }
    [[nodiscard]] T &operator*() noexcept { return *ptr_; }
    [[nodiscard]] const T *operator->() const noexcept { return ptr_.get(); }
    [[nodiscard]] T *operator->() noexcept { return ptr_.get(); }
    [[nodiscard]] const T *get() const noexcept { return ptr_.get(); }
    [[nodiscard]] T *get() noexcept { return ptr_.get(); }
    [[nodiscard]] const ocarina::unique_ptr<T> &ptr() const noexcept { return ptr_; }
    [[nodiscard]] ocarina::unique_ptr<T> &ptr() noexcept { return ptr_; }

    template<typename Other>
    requires std::is_convertible_v<Other *, T *>
    deep_copy_unique_ptr<T> &operator=(const deep_copy_unique_ptr<Other> &other) noexcept {
        OC_ASSERT(ptr_ && other.ptr());
        *ptr_ = *other.ptr();
        return *this;
    }

    deep_copy_unique_ptr<T> &operator=(const deep_copy_unique_ptr<T> &other) noexcept {
        OC_ASSERT(ptr_ && other.ptr());
        *ptr_ = *other.ptr();
        return *this;
    }
};

template<typename T, typename... Args>
[[nodiscard]] deep_copy_unique_ptr<T> make_deep_copy_unique(Args &&...args) noexcept {
    return deep_copy_unique_ptr<T>(make_unique<T>(OC_FORWARD(args)...));
}

template<typename T>
using DCUP = deep_copy_unique_ptr<T>;

template<typename T>
using DCSP = deep_copy_shared_ptr<T>;

}// namespace ocarina