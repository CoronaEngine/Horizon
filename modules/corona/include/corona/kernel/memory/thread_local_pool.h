#pragma once

#include <utility>

#include "object_pool.h"

namespace Corona::Kernal::Memory {

/// 线程本地对象池
///
/// 每个线程拥有独立的对象池实例，避免线程间竞争
/// 适用于高频创建/销毁对象的多线程场景
template <typename T>
class ThreadLocalPool {
   public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;

    /// 默认池容量
    static constexpr std::size_t kDefaultCapacity = 256;

    /// 获取当前线程的池实例
    static ObjectPool<T>& instance() {
        thread_local ObjectPool<T> pool(kDefaultCapacity, false);  // 线程内无需同步
        return pool;
    }

    /// 创建对象
    /// @param args 构造函数参数
    /// @return 新创建的对象指针
    template <typename... Args>
    [[nodiscard]] static pointer create(Args&&... args) {
        return instance().create(std::forward<Args>(args)...);
    }

    /// 销毁对象
    /// @param obj 要销毁的对象
    static void destroy(pointer obj) noexcept { instance().destroy(obj); }

    /// 获取当前线程池的统计信息
    [[nodiscard]] static PoolStats stats() { return instance().stats(); }

    /// 获取当前线程池的活跃对象数
    [[nodiscard]] static std::size_t size() { return instance().size(); }

    /// 获取当前线程池的容量
    [[nodiscard]] static std::size_t capacity() { return instance().capacity(); }

    /// 获取当前线程池的可用槽位数
    [[nodiscard]] static std::size_t available() { return instance().available(); }

   private:
    // 禁止实例化
    ThreadLocalPool() = delete;
    ~ThreadLocalPool() = delete;
};

/// 线程本地池智能指针删除器
template <typename T>
struct ThreadLocalPoolDeleter {
    void operator()(T* ptr) const noexcept { ThreadLocalPool<T>::destroy(ptr); }
};

/// 使用线程本地池的 unique_ptr 类型别名
template <typename T>
using ThreadLocalUniquePtr = std::unique_ptr<T, ThreadLocalPoolDeleter<T>>;

/// 创建使用线程本地池管理的 unique_ptr
template <typename T, typename... Args>
[[nodiscard]] ThreadLocalUniquePtr<T> make_thread_local_unique(Args&&... args) {
    return ThreadLocalUniquePtr<T>(ThreadLocalPool<T>::create(std::forward<Args>(args)...));
}

}  // namespace Corona::Kernal::Memory
