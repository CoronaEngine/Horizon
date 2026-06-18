//
// Created by Zero on 2024/7/29.
//

#pragma once

#include "macro.h"
#include "dsl/types/polymorphic.h"
#include "core/util/hash.h"
#include "object.h"
#include "build_system.h"
#include "file_tool.h"
#include "hotfix/support.h"

namespace vision::inline hotfix {
class HotfixSystem;

class Observer {
protected:
    bool valid_{true};
    void invalidation() noexcept { valid_ = false; }
    friend class HotfixSystem;

public:
    virtual void update_runtime_object(const IObjectConstructor *constructor) noexcept = 0;
    virtual void notified(const vector<const IObjectConstructor *> &constructors) noexcept {
        for (const auto &item : constructors) {
            update_runtime_object(item);
        }
    }
    Observer();
    ~Observer();
};

struct StaticMemoryBlock {
    void *ptr{};
    size_t size{};
};

class HotfixSystem {
public:
    using StaticMap = std::map<string, StaticMemoryBlock>;

private:
    HotfixSystem() { versions_.push_back({}); }
    ~HotfixSystem();
    static HotfixSystem *s_mgr;

private:
    ocarina::vector<Observer *> observers_;
    ocarina::vector<SP<const Observer>> defer_delete_;
    FileTool file_tool_;
    BuildSystem build_system_{};
    queue<std::function<void()>> callbacks_;
    vector<Version> versions_;
    int cur_ver_{0};
    StaticMap static_map_;

public:
    HotfixSystem(const HotfixSystem &) = delete;
    HotfixSystem(HotfixSystem &&) = delete;
    HotfixSystem operator=(const HotfixSystem &) = delete;
    HotfixSystem operator=(HotfixSystem &&) = delete;
    void register_observer(Observer *observer) noexcept;
    void deregister_observer(Observer *observer) noexcept;
    void defer_delete(SP<const Observer> observer) noexcept;
    OC_MAKE_MEMBER_GETTER(file_tool, &)
    template<typename... Args>
    void register_target(Args &&...args) {
        file_tool_.add_inspected(OC_FORWARD(args)...);
    }
    void remove_inspected(const fs::path &path) noexcept {
        file_tool_.remove_inspected(path);
    }
    void execute_callback();

    template<typename T>
    void register_static_var(const string &key, T &val) noexcept {
        size_t size = sizeof(T);
        StaticMemoryBlock new_block{addressof(val), sizeof(T)};
        if (static_map_.contains(key)) {
            StaticMemoryBlock old_block = static_map_.at(key);
            if (old_block.ptr != new_block.ptr) {
                oc_memcpy(new_block.ptr, old_block.ptr, new_block.size);
            }
            static_map_.at(key) = new_block;
        } else {
            static_map_.insert(make_pair(key, new_block));
        }
    }
    void unregister_static_var(const string &key) noexcept {
        if (static_map_.contains(key)) {
            static_map_.erase(key);
        }
    }
    [[nodiscard]] bool is_working() const noexcept { return build_system_.is_working(); }
    void enqueue_function(std::function<void()> fn) noexcept;
    void on_build_finish(bool success, const Target &target) noexcept;
    void on_build_all_finish(const vector<pair<bool, Target>> &modules) noexcept;
    void load_module(Target target) noexcept;
    void update_objects(const vector<const IObjectConstructor *> &constructors) noexcept;
    bool check_and_build() noexcept;
    void next_version() noexcept;
    void previous_version() noexcept;
    [[nodiscard]] bool has_previous() const noexcept {
        return cur_ver_ > 0 && !versions_.empty();
    }
    [[nodiscard]] bool has_next() const noexcept {
        return cur_ver_ < versions_.size() - 1 && !versions_.empty();
    }
    template<typename Tuple>
    static void replace_objects(const IObjectConstructor *constructor, Tuple tuple) noexcept {
        traverse_tuple(tuple, [&]<typename T>(SP<T> *ptr) {
            if (constructor->match(ptr->get())) {
                SP<T> new_obj = constructor->construct_shared<T>();
                new_obj->restore(ptr->get());
                if constexpr (std::derived_from<T, Observer>) {
                    instance().defer_delete(*ptr);
                }
                *ptr = new_obj;
            }
        });
    }
    void on_version_change() noexcept;
    static HotfixSystem &instance() noexcept;
    static void destroy_instance() noexcept;
};

template<typename T>
requires is_ptr_v<T>
class HotfixSlot : public TSlot<T>, public Observer {
public:
    using ptr_type = ptr_t<T>;
    using raw_type = std::remove_pointer_t<ptr_type>;
    static_assert(std::derived_from<raw_type, RuntimeObject>);

public:
    HotfixSlot() : Observer() {}
    HotfixSlot(T arg) : TSlot<T>(std::move(arg)), Observer() {}

    [[nodiscard]] virtual bool auto_process(T &new_obj, T &old_obj) noexcept {
        return true;
    }

    void update_runtime_object(const vision::IObjectConstructor *constructor) noexcept override {
        if (constructor->match(TSlot<T>::impl_.get())) {
            T new_obj = constructor->construct_shared<raw_type>();
            if constexpr (std::derived_from<T, Observer>) {
                HotfixSystem::instance().defer_delete(TSlot<T>::impl_);
            }
            if (auto_process(new_obj, TSlot<T>::impl_)) {
                new_obj->restore(TSlot<T>::impl_.get());
                TSlot<T>::impl_ = std::move(new_obj);
            }
        }
    }
};

template<typename T>
class PolymorphicObserver : public Polymorphic<T>, public Observer {
public:
    using Super = Polymorphic<T>;
    using Polymorphic<T>::Polymorphic;
    void update_runtime_object(const vision::IObjectConstructor *constructor) noexcept override {
        for (uint i = 0; i < Polymorphic<T>::size(); ++i) {
            auto element = Super::at(i);
            if (!constructor->match(element.get())) {
                continue;
            }
            auto new_element = constructor->construct<T>();
            new_element->restore(element.get());
            Super::replace(i, new_element);
        }
    }
};

}// namespace vision::inline hotfix
