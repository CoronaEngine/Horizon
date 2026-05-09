#pragma once

#include <atomic>
#include <cstddef>

#include "cache_aligned_allocator.h"

namespace Corona::Kernal::Memory {

/// 无锁空闲链表节点
struct alignas(CacheLineSize) LockFreeNode {
    std::atomic<LockFreeNode*> next{nullptr};
};

/// 无锁空闲链表（LIFO 栈）
///
/// 使用 CAS 操作实现无锁并发访问
/// 适用于高竞争场景下的空闲块管理
class LockFreeStack {
   public:
    LockFreeStack() = default;
    ~LockFreeStack() = default;

    /// 禁用拷贝
    LockFreeStack(const LockFreeStack&) = delete;
    LockFreeStack& operator=(const LockFreeStack&) = delete;

    /// 禁用移动（原子变量不支持移动）
    LockFreeStack(LockFreeStack&&) = delete;
    LockFreeStack& operator=(LockFreeStack&&) = delete;

    /// 压入节点
    /// @param node 要压入的节点
    void push(LockFreeNode* node) noexcept {
        if (!node) {
            return;
        }

        LockFreeNode* old_head = head_.load(std::memory_order_relaxed);
        do {
            node->next.store(old_head, std::memory_order_relaxed);
        } while (!head_.compare_exchange_weak(old_head, node, std::memory_order_release,
                                              std::memory_order_relaxed));
    }

    /// 弹出节点
    /// @return 弹出的节点，如果栈为空返回 nullptr
    [[nodiscard]] LockFreeNode* pop() noexcept {
        LockFreeNode* old_head = head_.load(std::memory_order_acquire);
        while (old_head) {
            LockFreeNode* next = old_head->next.load(std::memory_order_relaxed);
            if (head_.compare_exchange_weak(old_head, next, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                return old_head;
            }
            // CAS 失败，old_head 已被更新，重试
        }
        return nullptr;
    }

    /// 检查是否为空
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == nullptr;
    }

    /// 获取当前头节点（仅用于调试）
    [[nodiscard]] LockFreeNode* peek() const noexcept {
        return head_.load(std::memory_order_acquire);
    }

    /// 清空栈（注意：不会释放节点内存）
    void clear() noexcept { head_.store(nullptr, std::memory_order_release); }

    /// 获取大致的节点数量（可能不精确）
    [[nodiscard]] std::size_t approximate_size() const noexcept {
        std::size_t count = 0;
        LockFreeNode* current = head_.load(std::memory_order_acquire);
        while (current) {
            ++count;
            current = current->next.load(std::memory_order_relaxed);
        }
        return count;
    }

   private:
    std::atomic<LockFreeNode*> head_{nullptr};
};

}  // namespace Corona::Kernal::Memory
