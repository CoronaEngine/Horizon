//
// Created by Zero on 10/07/2022.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include "thread_safety.h"

namespace ocarina {

template<typename T>
class Pool : public concepts::Noncopyable,
             public thread_safety<conditional_mutex_t<true>> {
public:
    static constexpr size_t element_count = 64;
    using element_type = std::remove_cvref_t<T>;
    static constexpr bool trivially = std::is_trivially_destructible_v<element_type>;

private:
    ocarina::vector<T *> blocks_;
    ocarina::vector<T *> available_objects_;

    void _enlarge() {
        T *ptr = ocarina::allocate<T>(element_count);
        blocks_.push_back(ptr);
        available_objects_.reserve(element_count);
        for (int i = 0; i < element_count; ++i) {
            available_objects_.push_back(&(ptr[i]));
        }
    }

public:
    ~Pool() {
        if (blocks_.empty()) {
            return;
        }
        for (auto &ptr : blocks_) {
            ocarina::deallocate(ptr);
        }
    }

    template<typename... Args>
    [[nodiscard]] auto create(Args &&...args) noexcept {
        auto ptr = with_lock([this] {
            if (available_objects_.empty()) {
                _enlarge();
            }
            T *ptr = available_objects_.back();
            available_objects_.pop_back();
            return ptr;
        });
        std::construct_at(ptr, OC_FORWARD(args)...);
        return ptr;
    }

    void recycle(T *ptr) noexcept {
        if (!trivially) {
            ptr->~T();
        }
        with_lock([this, ptr] { available_objects_.emplace_back(ptr); });
    }
};

}// namespace ocarina