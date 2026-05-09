/**
❗❗❗Bug: ABA 问题未解决
*/
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace Corona::Kernel::Utils {

template <typename T>
class LockFreeQueue {
   public:
    LockFreeQueue() {
        auto dummy = std::make_shared<Node>();
        head_.store(dummy, std::memory_order_relaxed);
        tail_.store(std::move(dummy), std::memory_order_relaxed);
    }

    ~LockFreeQueue() {
        T value;
        while (dequeue(value)) {
        }
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;

    template <typename U>
    void enqueue(U&& value) {
        auto new_node = std::make_shared<Node>(std::forward<U>(value));
        std::shared_ptr<Node> tail;

        while (true) {
            tail = tail_.load(std::memory_order_acquire);
            auto next = tail->next.load(std::memory_order_acquire);

            if (tail != tail_.load(std::memory_order_acquire)) {
                continue;
            }

            if (!next) {
                if (tail->next.compare_exchange_weak(next, new_node,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed)) {
                    break;
                }
            } else {
                tail_.compare_exchange_weak(tail, next, std::memory_order_release,
                                            std::memory_order_relaxed);
            }
        }

        tail_.compare_exchange_strong(tail, std::move(new_node), std::memory_order_release,
                                      std::memory_order_relaxed);
    }

    bool dequeue(T& value) {
        std::shared_ptr<Node> head;

        while (true) {
            head = head_.load(std::memory_order_acquire);
            auto tail = tail_.load(std::memory_order_acquire);
            auto next = head->next.load(std::memory_order_acquire);

            if (head != head_.load(std::memory_order_acquire)) {
                continue;
            }

            if (!next) {
                return false;
            }

            if (head == tail) {
                tail_.compare_exchange_weak(tail, next, std::memory_order_release,
                                            std::memory_order_relaxed);
                continue;
            }

            if (head_.compare_exchange_weak(head, next, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                value = std::move(next->value.value());
                next->value.reset();
                break;
            }
        }

        return true;
    }

    [[nodiscard]] bool empty() const {
        auto head = head_.load(std::memory_order_acquire);
        auto next = head->next.load(std::memory_order_acquire);
        return !next;
    }

   private:
    struct Node {
        Node() noexcept = default;

        template <typename U>
        explicit Node(U&& v) : value(std::forward<U>(v)) {}

        std::atomic<std::shared_ptr<Node>> next{nullptr};
        std::optional<T> value;
    };

    std::atomic<std::shared_ptr<Node>> head_{nullptr};
    std::atomic<std::shared_ptr<Node>> tail_{nullptr};
};

// 基于 Dmitry Vyukov 算法的 MPMC 环形缓冲区队列，容量需为 2 的幂。
template <typename T, std::size_t Capacity>
class LockFreeRingBufferQueue {
   public:
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    LockFreeRingBufferQueue() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~LockFreeRingBufferQueue() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            const std::size_t seq = slots_[i].sequence.load(std::memory_order_relaxed);
            if ((seq & index_mask_) != i) {
                T* value_ptr = std::launder(reinterpret_cast<T*>(&slots_[i].storage));
                value_ptr->~T();
                slots_[i].sequence.store(i, std::memory_order_relaxed);
            }
        }
    }

    LockFreeRingBufferQueue(const LockFreeRingBufferQueue&) = delete;
    LockFreeRingBufferQueue& operator=(const LockFreeRingBufferQueue&) = delete;
    LockFreeRingBufferQueue(LockFreeRingBufferQueue&&) = delete;
    LockFreeRingBufferQueue& operator=(LockFreeRingBufferQueue&&) = delete;

    template <typename U>
    bool enqueue(U&& value) {
        Slot* slot = nullptr;
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        while (true) {
            slot = &slots_[pos & index_mask_];
            const std::size_t seq = slot->sequence.load(std::memory_order_acquire);
            const std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) -
                                        static_cast<std::ptrdiff_t>(pos);

            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                                       std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;  // 队列已满
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        T* value_ptr = std::launder(reinterpret_cast<T*>(&slot->storage));
        new (value_ptr) T(std::forward<U>(value));
        slot->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool dequeue(T& value) {
        Slot* slot = nullptr;
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

        while (true) {
            slot = &slots_[pos & index_mask_];
            const std::size_t seq = slot->sequence.load(std::memory_order_acquire);
            const std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) -
                                        static_cast<std::ptrdiff_t>(pos + 1);

            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                                       std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;  // 队列为空
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }

        T* value_ptr = std::launder(reinterpret_cast<T*>(&slot->storage));
        value = std::move(*value_ptr);
        value_ptr->~T();
        slot->sequence.store(pos + Capacity, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const {
        std::size_t pos = dequeue_pos_.load(std::memory_order_acquire);
        const Slot& slot = slots_[pos & index_mask_];
        const std::size_t seq = slot.sequence.load(std::memory_order_acquire);
        const std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) -
                                    static_cast<std::ptrdiff_t>(pos + 1);
        return diff < 0;
    }

    [[nodiscard]] bool full() const {
        std::size_t pos = enqueue_pos_.load(std::memory_order_acquire);
        const Slot& slot = slots_[pos & index_mask_];
        const std::size_t seq = slot.sequence.load(std::memory_order_acquire);
        const std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) -
                                    static_cast<std::ptrdiff_t>(pos);
        return diff < 0;
    }

    [[nodiscard]] constexpr std::size_t capacity() const {
        return Capacity;
    }

   private:
    struct Slot {
        std::atomic<std::size_t> sequence{0};
        std::aligned_storage_t<sizeof(T), alignof(T)> storage;
    };

    static constexpr std::size_t index_mask_ = Capacity - 1;

    Slot slots_[Capacity];
    std::atomic<std::size_t> enqueue_pos_{0};
    std::atomic<std::size_t> dequeue_pos_{0};
};

}  // namespace Corona::Kernel::Utils