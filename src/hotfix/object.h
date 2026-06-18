//
// Created by Zero on 2024/7/31.
//

#pragma once

#include "core/util/hash.h"
#include "macro.h"
#include "hotfix/support.h"

namespace vision::inline hotfix {
#define HOTFIX_VIRTUAL virtual

#define VS_HOTFIX_MOVE_ATTR(attr_name) \
    attr_name = ocarina::move(old_obj_->attr_name);

#define VS_HOTFIX_MOVE_ATTRS(...)                                                  \
    auto old_obj_ = dynamic_cast<std::remove_cvref_t<decltype(*this)> *>(old_obj); \
    MAP(VS_HOTFIX_MOVE_ATTR, ##__VA_ARGS__)

#define VS_HOTFIX_MAKE_RESTORE(Super, ...)                   \
    void restore(RuntimeObject *old_obj) noexcept override { \
        Super::restore(old_obj);                             \
        VS_HOTFIX_MOVE_ATTRS(__VA_ARGS__)                    \
    }

class RuntimeObject : public RTTI {
public:
    RuntimeObject() = default;
    virtual void restore(RuntimeObject *old_obj) noexcept {
    }
    virtual ~RuntimeObject() = default;
};

template<typename T>
class TSlot {
public:
    using impl_type = T;

protected:
    impl_type impl_;

public:
    OC_MAKE_MEMBER_GETTER_SETTER(impl, &)
    TSlot() = default;
    TSlot(T impl) : impl_(std::move(impl)) {}
    [[nodiscard]] operator bool() const noexcept { return impl().get() != nullptr; }
    [[nodiscard]] auto get() const noexcept { return impl_.get(); }
    [[nodiscard]] auto get() noexcept { return impl_.get(); }
    [[nodiscard]] auto operator->() const noexcept { return impl_.get(); }
    [[nodiscard]] auto operator->() noexcept { return impl_.get(); }
    [[nodiscard]] decltype(auto) operator*() const noexcept { return *impl_.get(); }
    [[nodiscard]] decltype(auto) operator*() noexcept { return *impl_.get(); }
};

class IObjectConstructor {
public:
    using Deleter = void(RuntimeObject *);

protected:
    string_view filename_{};

public:
    explicit IObjectConstructor(const char *fn) : filename_(fn) {}
    OC_MAKE_MEMBER_GETTER(filename, )
    template<typename T>
    [[nodiscard]] bool match(T &&t) const noexcept {
        return class_name() == t->class_name();
    }
    template<typename T>
    [[nodiscard]] T construct() const {
        using raw_type = ocarina::ptr_t<T>;
        static_assert(std::derived_from<raw_type, RuntimeObject>);
        if constexpr (ocarina::pointer_category_v<T> == ocarina::PointerCategory::Raw) {
            return construct_raw<raw_type>();
        } else if constexpr (ocarina::pointer_category_v<T> == ocarina::PointerCategory::Shared) {
            return construct_shared<raw_type>();
        } else if constexpr (ocarina::pointer_category_v<T> == ocarina::PointerCategory::Unique) {
            return construct_unique<raw_type>();
        } else {
            return T{construct<typename T::impl_type>()};
        }
    }
    template<typename T = RuntimeObject>
    [[nodiscard]] T *construct_raw() const {
        return dynamic_cast<T *>(construct_impl());
    }
    template<typename T = RuntimeObject>
    [[nodiscard]] SP<T> construct_shared() const noexcept {
        return std::static_pointer_cast<T>(construct_shared_impl());
    }
    template<typename T = RuntimeObject>
    [[nodiscard]] UP<T, Deleter *> construct_unique() const noexcept {
        return ocarina::static_unique_pointer_cast<T>(construct_unique_impl());
    }
    [[nodiscard]] virtual RuntimeObject *construct_impl() const = 0;
    [[nodiscard]] virtual SP<RuntimeObject> construct_shared_impl() const = 0;
    [[nodiscard]] virtual UP<RuntimeObject, Deleter *> construct_unique_impl() const = 0;
    static void destroy(RuntimeObject *obj) {
        delete obj;
    }
    [[nodiscard]] virtual string_view class_name() const = 0;
    virtual ~IObjectConstructor() = default;
};

template<typename T>
requires std::derived_from<T, RuntimeObject>
class ObjectConstructor : public IObjectConstructor {
public:
    explicit ObjectConstructor(const char *fn = nullptr) : IObjectConstructor(fn) {}
    [[nodiscard]] RuntimeObject *construct_impl() const override {
        return new T{};
    }
    [[nodiscard]] SP<RuntimeObject> construct_shared_impl() const override {
        return SP<T>(static_cast<T *>(construct_impl()), destroy);
    }
    [[nodiscard]] UP<RuntimeObject, Deleter *> construct_unique_impl() const override {
        return UP<T, Deleter *>(static_cast<T *>(construct_impl()), destroy);
    }
    [[nodiscard]] string_view class_name() const override {
        return type_string<T>();
    }
};
}// namespace vision::inline hotfix
