#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include "corona/kernel/core/i_logger.h"
#include "corona/pal/cfw_platform.h"
#include "stack_trace.h"

// 超时锁配置（用于死锁检测）
#ifndef CFW_LOCK_TIMEOUT_MS
#ifdef NDEBUG
#define CFW_LOCK_TIMEOUT_MS 2000  // 生产环境 2 秒
#else
#define CFW_LOCK_TIMEOUT_MS 200  // 调试环境 0.2 秒
#endif
#endif

// #define CFW_ENABLE_LOCK_TIMEOUT 1
// 锁超时功能开关
#ifndef CFW_ENABLE_LOCK_TIMEOUT
#define CFW_ENABLE_LOCK_TIMEOUT 0  // 默认关闭，设为 1 则使用超时锁
#endif

// #define CFW_ENABLE_LOCK_LOGGING 1
#ifndef CFW_ENABLE_LOCK_LOGGING
#define CFW_ENABLE_LOCK_LOGGING 0  // 默认关闭，设为 1 则启用锁日志记录
#endif

namespace Corona::Kernel::Utils {

/**
 * @brief 线程安全的固定容量静态缓冲区
 *
 * StaticBuffer 提供了一个固定容量的对象池数据结构。
 * 每个槽位独立加锁以实现细粒度并发控制。
 *
 * @tparam T 存储的元素类型
 * @tparam Capacity 缓冲区容量，必须是 2 的幂次且 >= 2
 *
 * @note 此结构体仅作为 Storage 的内部数据容器使用，不提供分配/释放等高级操作
 * @note 包含三个核心成员：buffer（数据数组）、occupied（占用标志）、mutexes（槽位锁）
 */
template <typename T, std::size_t Capacity>
struct StaticBuffer {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    StaticBuffer() {
        free_indices.reserve(Capacity);
        for (std::size_t i = 0; i < Capacity; ++i) {
            occupied[i].store(false, std::memory_order_relaxed);
            free_indices.push_back(i);
        }
    }

    StaticBuffer(const StaticBuffer&) = delete;
    StaticBuffer& operator=(const StaticBuffer&) = delete;
    StaticBuffer(StaticBuffer&&) = delete;
    StaticBuffer& operator=(StaticBuffer&&) = delete;

    ~StaticBuffer() = default;

    std::array<T, Capacity> buffer{};                                 ///< 数据存储数组
    std::array<std::atomic<bool>, Capacity> occupied{};               ///< 槽位占用标志，原子操作确保可见性
    mutable std::array<std::shared_timed_mutex, Capacity> mutexes{};  ///< 每个槽位的独立共享锁（支持超时）

    std::vector<std::size_t> free_indices;
    std::mutex alloc_mutex;
    std::atomic<std::size_t> active_count{0};
    std::atomic<bool> is_full{false};
};

/**
 * @brief 动态扩容的线程安全对象池
 *
 * Storage 基于 StaticBuffer 实现，通过指针句柄策略提供动态扩容的对象池。
 * 当所有现有 buffer 满时，自动创建新的 StaticBuffer 并将新槽位加入空闲队列。
 *
 * @tparam T 存储的元素类型
 * @tparam BufferCapacity 每个底层 StaticBuffer 的容量，必须是 2 的幂次且 >= 2
 * @tparam InitialBuffers 初始 buffer 数量，必须 >= 1
 *
 * @note 句柄类型：
 * - ObjectId 是 std::uintptr_t 类型，存储槽位的实际内存地址
 * - 通过 get_parent_buffer() 根据地址范围反查所属的 StaticBuffer
 *
 * @note 线程安全性：
 * - allocate：多线程安全，扩容时使用独占锁保护 buffer 列表
 * - deallocate：多线程安全，使用独占锁标记槽位空闲后回收句柄
 * - acquire_read/acquire_write：多线程安全，分别使用共享锁/独占锁访问槽位
 * - 迭代器：多线程安全，采用 try_lock 避免死锁，锁定失败的槽位会加入跳过队列稍后重试
 *
 * @note 使用示例：
 * @code
 * Storage<GameEntity, 64> entities;
 *
 * // 分配和访问
 * auto id = entities.allocate();
 * {
 *     auto writer = entities.acquire_write(id);
 *     writer->x = 100.0f;
 * }
 *
 * // Range-based for 遍历（只读）
 * for (const auto& entity : entities) {
 *     std::cout << entity.name << std::endl;
 * }
 *
 * // Range-based for 遍历（可写）
 * for (auto& entity : entities) {
 *     entity.x += 10.0f;
 * }
 *
 * entities.deallocate(id);
 * @endcode
 */
template <typename T, std::size_t BufferCapacity = 128, std::size_t InitialBuffers = 2>
class Storage {
    static_assert(InitialBuffers >= 1, "InitialBuffers must be at least 1");
    static_assert(BufferCapacity >= 2, "BufferCapacity must be at least 2");
    static_assert((BufferCapacity & (BufferCapacity - 1)) == 0, "BufferCapacity must be a power of two");

   public:
    using ObjectId = std::uintptr_t;  ///< 对象 ID 类型，存储槽位的实际内存地址

    /**
     * @brief 只读访问句柄 (RAII)
     */
    class ReadHandle final {
       public:
        ReadHandle() = default;
        ReadHandle(const T* ptr, std::shared_lock<std::shared_timed_mutex>&& lock)
            : ptr_(ptr), lock_(std::move(lock)) {
#if CFW_ENABLE_LOCK_LOGGING
            CFW_LOG_TRACE("ReadHandle acquire lock for ptr addr: {}", reinterpret_cast<std::uintptr_t>(ptr_));
#endif
        }

        ~ReadHandle() {
#if CFW_ENABLE_LOCK_LOGGING
            CFW_LOG_TRACE("ReadHandle release lock for ptr addr: {}", reinterpret_cast<std::uintptr_t>(ptr_));
#endif
        }

        ReadHandle(ReadHandle&&) = default;
        ReadHandle& operator=(ReadHandle&&) = default;
        ReadHandle(const ReadHandle&) = delete;
        ReadHandle& operator=(const ReadHandle&) = delete;

        [[nodiscard]] bool valid() const { return ptr_ != nullptr && lock_.owns_lock(); }
        explicit operator bool() const { return valid(); }

        const T* get() const { return ptr_; }
        const T* operator->() const { return ptr_; }
        const T& operator*() const { return *ptr_; }

       private:
        const T* ptr_ = nullptr;
        std::shared_lock<std::shared_timed_mutex> lock_;
    };

    /**
     * @brief 读写访问句柄 (RAII)
     */
    class WriteHandle final {
       public:
        WriteHandle() = default;
        WriteHandle(T* ptr, std::unique_lock<std::shared_timed_mutex>&& lock)
            : ptr_(ptr), lock_(std::move(lock)) {
#if CFW_ENABLE_LOCK_LOGGING
            CFW_LOG_TRACE("WriteHandle acquire lock for ptr addr: {}", reinterpret_cast<std::uintptr_t>(ptr_));
#endif
        }

        ~WriteHandle() {
#if CFW_ENABLE_LOCK_LOGGING
            CFW_LOG_TRACE("WriteHandle release lock for ptr addr: {}", reinterpret_cast<std::uintptr_t>(ptr_));
#endif
        }

        WriteHandle(WriteHandle&&) = default;
        WriteHandle& operator=(WriteHandle&&) = default;
        WriteHandle(const WriteHandle&) = delete;
        WriteHandle& operator=(const WriteHandle&) = delete;

        [[nodiscard]] bool valid() const { return ptr_ != nullptr && lock_.owns_lock(); }
        explicit operator bool() const { return valid(); }

        T* get() const { return ptr_; }
        T* operator->() const { return ptr_; }
        T& operator*() const { return *ptr_; }

       private:
        T* ptr_ = nullptr;
        std::unique_lock<std::shared_timed_mutex> lock_;
    };

   public:
    /**
     * @brief 构造函数，创建初始 buffer 并预分配所有槽位 ID
     *
     * @note 创建 InitialBuffers 个 StaticBuffer，每个包含 BufferCapacity 个槽位
     * @note 将所有槽位的地址作为 ID 加入空闲队列
     */
    Storage() {
        for (std::size_t i = 0; i < InitialBuffers; ++i) {
            buffers_.emplace_back();
        }
    }

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&&) = delete;
    Storage& operator=(Storage&&) = delete;

    ~Storage() = default;

    /**
     * @brief 获取对象池的总容量
     *
     * @return 当前所有 buffer 的槽位总数（buffer 数量 × 每个 buffer 的容量）
     *
     * @note 返回值为瞬时快照，多线程环境下可能在返回后立即变化
     */
    CFW_FORCE_INLINE
    std::uint64_t capacity() const {
        return buffer_count_.load(std::memory_order_relaxed) * BufferCapacity;
    }

    /**
     * @brief 获取当前已占用的槽位数量
     *
     * @return 当前活跃对象的数量
     *
     * @note 返回值为瞬时快照，多线程环境下可能在返回后立即变化
     */
    CFW_FORCE_INLINE
    std::size_t count() const {
        return occupied_count_.load(std::memory_order_relaxed);
    }

    /**
     * @brief 检查对象池是否为空
     *
     * @return 若当前没有活跃对象则返回 true，否则返回 false
     *
     * @note 基于 count() 的瞬时快照
     */
    CFW_FORCE_INLINE
    bool empty() const {
        return count() == 0;
    }

    /**
     * @brief 检查指定 ID 是否存在于对象池中
     *
     * @param id 对象 ID（由 allocate() 返回）
     * @return 若 ID 有效且对应槽位已占用则返回 true，否则返回 false
     *
     * @note 检查流程：
     *       1. 通过 get_parent_buffer() 查找槽位所属的 StaticBuffer
     *       2. 若找不到对应的 buffer，返回 false
     *       3. 计算槽位在 buffer 内的索引，检查 occupied 原子标志
     *
     * @note 线程安全，使用原子操作检查槽位占用状态
     * @note 返回值为瞬时快照，多线程环境下可能在返回后立即变化
     */
    CFW_FORCE_INLINE
    bool contains(ObjectId id) const {
        auto [_, parent_buffer] = const_cast<Storage*>(this)->get_parent_buffer(id);
        if (!parent_buffer) {
            return false;
        }
        std::size_t index = (id - reinterpret_cast<ObjectId>(&(parent_buffer->buffer[0]))) / sizeof(T);
        return parent_buffer->occupied[index].load(std::memory_order_acquire);
    }

    /**
     * @brief 根据对象 ID 计算全局唯一的序列号
     *
     * @param id 对象 ID（内存地址）
     * @return 成功返回非负序列号（0 ~ capacity-1），失败返回 -1
     *
     * @note 序列号计算公式：buffer_index * BufferCapacity + slot_index
     * @note 该序列号可用于数组索引等场景，但需注意扩容会导致序列号范围增加
     */
    CFW_FORCE_INLINE
    std::int64_t seq_id(std::uintptr_t id) const {
        auto [index, buffer] = const_cast<Storage*>(this)->get_parent_buffer(id);
        if (index >= 0 && buffer) {
            return index * BufferCapacity +
                   (id - reinterpret_cast<std::uintptr_t>(&(buffer->buffer[0]))) / sizeof(T);
        }
        CFW_LOG_FLUSH();
        throw std::runtime_error("Parent buffer not found during seq_id calculation");
        return -1;
    }

    /**
     * @brief 根据只读句柄获取对象的全局唯一序列号
     *
     * @param handle 只读访问句柄
     * @return 若句柄有效则返回非负序列号（0 ~ capacity-1），否则返回 -1
     *
     * @note 该方法是 seq_id(ObjectId) 的便捷重载，自动从 handle 中提取对象指针并转换为 ID
     */
    CFW_FORCE_INLINE
    std::int64_t seq_id(const ReadHandle& handle) const {
        if (handle.valid()) {
            return seq_id(reinterpret_cast<std::uintptr_t>(handle.get()));
        }
        return -1;
    }

    /**
     * @brief 根据读写句柄获取对象的全局唯一序列号
     *
     * @param handle 读写访问句柄
     * @return 若句柄有效则返回非负序列号（0 ~ capacity-1），否则返回 -1
     *
     * @note 该方法是 seq_id(ObjectId) 的便捷重载，自动从 handle 中提取对象指针并转换为 ID
     */
    CFW_FORCE_INLINE
    std::int64_t seq_id(const WriteHandle& handle) const {
        if (handle.valid()) {
            return seq_id(reinterpret_cast<std::uintptr_t>(handle.get()));
        }
        return -1;
    }

    /**
     * @brief 分配一个槽位并初始化
     *
     * @return 成功返回槽位 ID（内存地址），失败返回 0
     *
     * @note 分配流程：
     *       1. 从空闲队列获取槽位 ID
     *       2. 若队列为空，则扩容（创建新 buffer 并入队所有新槽位）
     *       3. 通过 get_parent_buffer() 查找槽位所属的 StaticBuffer
     *       4. 计算槽位在 buffer 内的索引，加锁后初始化并标记为已占用
     *
     * @note 线程安全，扩容时使用独占锁保护 buffer 列表
     * @throws std::runtime_error 若无法找到槽位所属的 buffer（理论上不应发生）
     */
    [[nodiscard]]
    ObjectId allocate() {
        // 1. Try to allocate from existing buffers (prefer head)
        {
            std::shared_lock lock(list_mutex_);
            for (auto& buffer : buffers_) {
                if (!buffer.is_full.load(std::memory_order_acquire)) {
                    if (ObjectId id = allocate_from_buffer(buffer)) {
                        return id;
                    }
                }
            }
        }

        // 2. All buffers full, expand
        std::unique_lock lock(list_mutex_);
        // Double check
        for (auto& buffer : buffers_) {
            if (!buffer.is_full.load(std::memory_order_acquire)) {
                if (ObjectId id = allocate_from_buffer(buffer)) {
                    return id;
                }
            }
        }

        buffers_.emplace_back();
        buffer_count_.fetch_add(1, std::memory_order_relaxed);
        CFW_LOG_TRACE("Storage<{},{},{}> expanded: new capacity = BufferCount:{} * BufferCapacity:{} = {}",
                      typeid(T).name(),
                      BufferCapacity,
                      InitialBuffers,
                      buffer_count_.load(std::memory_order_relaxed),
                      BufferCapacity,
                      capacity());

        return allocate_from_buffer(buffers_.back());
    }

    /**
     * @brief 释放指定槽位
     *
     * @param id 槽位 ID（由 allocate() 返回）
     *
     * @note 释放流程：
     *       1. 通过 get_parent_buffer() 查找槽位所属的 StaticBuffer
     *       2. 计算槽位在 buffer 内的索引
     *       3. 加独占锁后标记槽位为未占用
     *       4. 将 ID 重新加入空闲队列以供复用
     *
     * @note 线程安全，使用独占锁保护槽位状态
     * @throws std::runtime_error 若无法找到槽位所属的 buffer（可能是无效 ID）
     */
    void deallocate(ObjectId id) {
        T* ptr = reinterpret_cast<T*>(id);
        auto [_, parent_buffer] = get_parent_buffer(id);

        if (parent_buffer) {
            std::size_t index = (id - reinterpret_cast<std::uint64_t>(&(parent_buffer->buffer[0]))) / sizeof(T);

            {
                std::unique_lock slot_lock(parent_buffer->mutexes[index]);
                if (parent_buffer->occupied[index].load(std::memory_order_acquire) == false) {
                    CFW_LOG_CRITICAL("Double free detected for object ID: {}", id);
                    CFW_LOG_FLUSH();
                    throw std::runtime_error("Double free detected");
                }
                parent_buffer->occupied[index].store(false, std::memory_order_release);
            }

            occupied_count_.fetch_sub(1, std::memory_order_relaxed);

            {
                std::unique_lock alloc_lock(parent_buffer->alloc_mutex);
                parent_buffer->free_indices.push_back(index);
                parent_buffer->is_full.store(false, std::memory_order_release);
            }

            std::size_t active = parent_buffer->active_count.fetch_sub(1, std::memory_order_relaxed) - 1;

            if (active == 0) {
                std::unique_lock list_lock(list_mutex_, std::try_to_lock);
                if (list_lock.owns_lock()) {
                    int empty_cnt = 0;
                    for (auto it = buffers_.rbegin(); it != buffers_.rend(); ++it) {
                        if (it->active_count.load() == 0) {
                            empty_cnt++;
                        } else {
                            break;
                        }
                    }

                    while (empty_cnt > 2 && buffers_.size() > InitialBuffers) {
                        buffers_.pop_back();
                        buffer_count_.fetch_sub(1, std::memory_order_relaxed);
                        empty_cnt--;
                        CFW_LOG_TRACE("Storage<{},{},{}> shrunk: new capacity = BufferCount:{} * BufferCapacity:{} = {}",
                                      typeid(T).name(),
                                      BufferCapacity,
                                      InitialBuffers,
                                      buffer_count_.load(std::memory_order_relaxed),
                                      BufferCapacity,
                                      capacity());
                    }
                }
            }
            return;
        }
        CFW_LOG_FLUSH();
        throw std::runtime_error("Parent buffer not found during deallocation");
    }

    /**
     * @brief 获取只读访问句柄
     *
     * @param id 对象 ID
     * @return ReadHandle RAII 句柄，若 ID 无效或未占用则 valid() 为 false
     *
     * @note 访问流程：
     *       1. 通过 get_parent_buffer() 查找槽位所属的 StaticBuffer
     *       2. 计算槽位在 buffer 内的索引
     *       3. 加共享锁后检查占用标志，若已占用则返回持有锁的 Handle
     *
     * @note 线程安全，使用共享锁允许多个读者并发访问
     * @note 使用超时锁检测死锁，超时时间由 CFW_LOCK_TIMEOUT_MS 宏定义
     * @throws std::runtime_error 若锁获取超时（可能存在死锁）
     */
    [[nodiscard]]
    ReadHandle acquire_read(ObjectId id) {
        T* ptr = reinterpret_cast<T*>(id);
        auto [_, parent_buffer] = get_parent_buffer(id);

        if (parent_buffer) {
            std::size_t index = (id - reinterpret_cast<ObjectId>(&(parent_buffer->buffer[0]))) / sizeof(T);
            std::shared_lock<std::shared_timed_mutex> slot_lock(parent_buffer->mutexes[index], std::defer_lock);

#if CFW_ENABLE_LOCK_TIMEOUT
            // 使用超时锁检测死锁
            constexpr auto timeout = std::chrono::milliseconds(CFW_LOCK_TIMEOUT_MS);
            if (!slot_lock.try_lock_for(timeout)) {
                std::string stack_info = capture_stack_trace(2, 15);
                std::string error_msg =
                    "Lock timeout during acquire_read for object " + std::to_string(id) +
                    " after " + std::to_string(CFW_LOCK_TIMEOUT_MS) + "ms - possible deadlock detected\n" +
                    stack_info;
                CFW_LOG_CRITICAL("{}", error_msg);
                CFW_LOG_FLUSH();
                throw std::runtime_error(error_msg);
            }
#else
            slot_lock.lock();
#endif

            if (parent_buffer->occupied[index].load(std::memory_order_acquire)) {
                return ReadHandle(ptr, std::move(slot_lock));
            }
            CFW_LOG_FLUSH();
            throw std::runtime_error("Attempt to acquire read handle for unoccupied object ID: " + std::to_string(id));
        }
        CFW_LOG_FLUSH();
        throw std::runtime_error("Parent buffer not found during read acquisition");
    }

    /**
     * @brief 尝试获取只读访问句柄（不抛异常版本）
     *
     * @param id 对象 ID
     * @return ReadHandle RAII 句柄，若 ID 无效或未占用则 valid() 为 false
     *
     * @note 与 acquire_read() 不同，此方法在 ID 无效或未占用时不抛异常，
     *       而是返回无效的 Handle（valid() 返回 false）
     *
     * @note 使用示例：
     * @code
     * if (auto handle = storage.try_acquire_read(id)) {
     *     // ID 存在，可以安全访问
     *     std::cout << handle->name << std::endl;
     * }
     * // 无需先调用 contains() 检查
     * @endcode
     *
     * @note 线程安全，使用共享锁允许多个读者并发访问
     * @note 使用超时锁检测死锁，超时时间由 CFW_LOCK_TIMEOUT_MS 宏定义
     * @throws std::runtime_error 若锁获取超时（可能存在死锁）
     */
    [[nodiscard]]
    ReadHandle try_acquire_read(ObjectId id) {
        T* ptr = reinterpret_cast<T*>(id);
        auto [_, parent_buffer] = get_parent_buffer(id);

        if (!parent_buffer) {
            return ReadHandle();  // 返回无效句柄
        }

        std::size_t index = (id - reinterpret_cast<ObjectId>(&(parent_buffer->buffer[0]))) / sizeof(T);
        std::shared_lock<std::shared_timed_mutex> slot_lock(parent_buffer->mutexes[index], std::defer_lock);

#if CFW_ENABLE_LOCK_TIMEOUT
        // 使用超时锁检测死锁
        constexpr auto timeout = std::chrono::milliseconds(CFW_LOCK_TIMEOUT_MS);
        if (!slot_lock.try_lock_for(timeout)) {
            std::string stack_info = capture_stack_trace(2, 15);
            std::string error_msg =
                "Lock timeout during try_acquire_read for object " + std::to_string(id) +
                " after " + std::to_string(CFW_LOCK_TIMEOUT_MS) + "ms - possible deadlock detected\n" +
                stack_info;
            CFW_LOG_CRITICAL("{}", error_msg);
            CFW_LOG_FLUSH();
            throw std::runtime_error(error_msg);
        }
#else
        slot_lock.lock();
#endif

        if (parent_buffer->occupied[index].load(std::memory_order_acquire)) {
            return ReadHandle(ptr, std::move(slot_lock));
        }
        return ReadHandle();  // 槽位未占用，返回无效句柄
    }

    /**
     * @brief 获取读写访问句柄
     *
     * @param id 对象 ID
     * @return WriteHandle RAII 句柄，若 ID 无效或未占用则 valid() 为 false
     *
     * @note 访问流程：
     *       1. 通过 get_parent_buffer() 查找槽位所属的 StaticBuffer
     *       2. 计算槽位在 buffer 内的索引
     *       3. 加独占锁后检查占用标志，若已占用则返回持有锁的 Handle
     *
     * @note 线程安全，使用独占锁确保独占写入
     * @note 使用超时锁检测死锁，超时时间由 CFW_LOCK_TIMEOUT_MS 宏定义
     * @throws std::runtime_error 若锁获取超时（可能存在死锁）
     */
    [[nodiscard]]
    WriteHandle acquire_write(ObjectId id) {
        T* ptr = reinterpret_cast<T*>(id);
        auto [_, parent_buffer] = get_parent_buffer(id);

        if (parent_buffer) {
            std::size_t index = (id - reinterpret_cast<ObjectId>(&(parent_buffer->buffer[0]))) / sizeof(T);
            std::unique_lock<std::shared_timed_mutex> slot_lock(parent_buffer->mutexes[index], std::defer_lock);

#if CFW_ENABLE_LOCK_TIMEOUT
            // 使用超时锁检测死锁
            constexpr auto timeout = std::chrono::milliseconds(CFW_LOCK_TIMEOUT_MS);
            if (!slot_lock.try_lock_for(timeout)) {
                std::string stack_info = capture_stack_trace(2, 15);
                std::string error_msg =
                    "Lock timeout during acquire_write for object " + std::to_string(id) +
                    " after " + std::to_string(CFW_LOCK_TIMEOUT_MS) + "ms - possible deadlock detected\n" +
                    stack_info;
                CFW_LOG_CRITICAL("{}", error_msg);
                CFW_LOG_FLUSH();
                throw std::runtime_error(error_msg);
            }
#else
            slot_lock.lock();
#endif

            if (parent_buffer->occupied[index].load(std::memory_order_acquire)) {
                return WriteHandle(ptr, std::move(slot_lock));
            }
            CFW_LOG_FLUSH();
            throw std::runtime_error("Attempt to acquire write handle for unoccupied object ID: " + std::to_string(id));
        }
        CFW_LOG_FLUSH();
        throw std::runtime_error("Parent buffer not found during write acquisition");
    }

    /**
     * @brief 尝试获取读写访问句柄（不抛异常版本）
     *
     * @param id 对象 ID
     * @return WriteHandle RAII 句柄，若 ID 无效或未占用则 valid() 为 false
     *
     * @note 与 acquire_write() 不同，此方法在 ID 无效或未占用时不抛异常，
     *       而是返回无效的 Handle（valid() 返回 false）
     *
     * @note 使用示例：
     * @code
     * if (auto handle = storage.try_acquire_write(id)) {
     *     // ID 存在，可以安全修改
     *     handle->x += 10.0f;
     * }
     * // 无需先调用 contains() 检查
     * @endcode
     *
     * @note 线程安全，使用独占锁确保独占写入
     * @note 使用超时锁检测死锁，超时时间由 CFW_LOCK_TIMEOUT_MS 宏定义
     * @throws std::runtime_error 若锁获取超时（可能存在死锁）
     */
    [[nodiscard]]
    WriteHandle try_acquire_write(ObjectId id) {
        T* ptr = reinterpret_cast<T*>(id);
        auto [_, parent_buffer] = get_parent_buffer(id);

        if (!parent_buffer) {
            return WriteHandle();  // 返回无效句柄
        }

        std::size_t index = (id - reinterpret_cast<ObjectId>(&(parent_buffer->buffer[0]))) / sizeof(T);
        std::unique_lock<std::shared_timed_mutex> slot_lock(parent_buffer->mutexes[index], std::defer_lock);

#if CFW_ENABLE_LOCK_TIMEOUT
        // 使用超时锁检测死锁
        constexpr auto timeout = std::chrono::milliseconds(CFW_LOCK_TIMEOUT_MS);
        if (!slot_lock.try_lock_for(timeout)) {
            std::string stack_info = capture_stack_trace(2, 15);
            std::string error_msg =
                "Lock timeout during try_acquire_write for object " + std::to_string(id) +
                " after " + std::to_string(CFW_LOCK_TIMEOUT_MS) + "ms - possible deadlock detected\n" +
                stack_info;
            CFW_LOG_CRITICAL("{}", error_msg);
            CFW_LOG_FLUSH();
            throw std::runtime_error(error_msg);
        }
#else
        slot_lock.lock();
#endif

        if (parent_buffer->occupied[index].load(std::memory_order_acquire)) {
            return WriteHandle(ptr, std::move(slot_lock));
        }
        return WriteHandle();  // 槽位未占用，返回无效句柄
    }

    /**
     * @brief 线程安全的迭代器 (Input Iterator)
     *
     * @note 使用基于索引的方式而非迭代器，以避免并发扩容/缩容导致的迭代器失效问题
     */
    class Iterator {
       public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        Iterator() = default;
        Iterator(Storage* storage, bool is_end) : storage_(storage), is_end_(is_end) {
            if (!is_end_ && storage_) {
                advance();
            }
        }

        Iterator(Iterator&&) = default;
        Iterator& operator=(Iterator&&) = default;

        T& operator*() { return *handle_; }
        T* operator->() { return handle_.get(); }

        Iterator& operator++() {
            handle_ = WriteHandle();
            advance();
            return *this;
        }

        bool operator!=(const Iterator& other) const { return is_end_ != other.is_end_; }
        bool operator==(const Iterator& other) const { return is_end_ == other.is_end_; }

       private:
        /**
         * @brief 根据索引获取对应的 buffer 指针
         * @param index buffer 索引
         * @return buffer 指针，如果索引超出范围则返回 nullptr
         * @note 必须在持有 list_mutex_ 共享锁时调用
         */
        StaticBuffer<T, BufferCapacity>* get_buffer_at(std::size_t index) {
            auto it = storage_->buffers_.begin();
            for (std::size_t i = 0; i < index && it != storage_->buffers_.end(); ++i, ++it) {
            }
            return (it != storage_->buffers_.end()) ? &(*it) : nullptr;
        }

        void advance() {
            while (true) {
                if (!retry_mode_) {
                    // 检查是否需要移动到下一个 buffer
                    if (slot_index_ >= BufferCapacity) {
                        ++buffer_index_;
                        slot_index_ = 0;
                    }

                    // 获取当前 buffer
                    StaticBuffer<T, BufferCapacity>* buffer = nullptr;
                    {
                        std::shared_lock lock(storage_->list_mutex_);
                        buffer = get_buffer_at(buffer_index_);
                        if (!buffer) {
                            // 已经遍历完所有 buffer，进入重试模式
                            retry_mode_ = true;
                            continue;
                        }
                    }

                    // 检查当前槽位是否被占用
                    if (buffer->occupied[slot_index_].load(std::memory_order_acquire)) {
                        std::unique_lock slot_lock(buffer->mutexes[slot_index_], std::try_to_lock);
                        if (slot_lock.owns_lock()) {
                            // 再次检查占用状态（double-check）
                            if (buffer->occupied[slot_index_].load(std::memory_order_acquire)) {
                                handle_ = WriteHandle(&buffer->buffer[slot_index_], std::move(slot_lock));
                                ++slot_index_;
                                return;
                            }
                        } else {
                            // 锁定失败，加入跳过队列稍后重试
                            skipped_items_.push({buffer, slot_index_});
                        }
                    }
                    ++slot_index_;
                } else {
                    // 重试模式：处理之前跳过的槽位
                    if (skipped_items_.empty()) {
                        is_end_ = true;
                        return;
                    }
                    auto [buffer, index] = skipped_items_.front();
                    skipped_items_.pop();

                    // 阻塞等待获取锁
                    std::unique_lock slot_lock(buffer->mutexes[index]);
                    if (buffer->occupied[index].load(std::memory_order_acquire)) {
                        handle_ = WriteHandle(&buffer->buffer[index], std::move(slot_lock));
                        return;
                    }
                    // 如果槽位已被释放，继续处理下一个跳过的项
                }
            }
        }

        Storage* storage_ = nullptr;
        std::size_t buffer_index_ = 0;  ///< 当前 buffer 索引（替代迭代器）
        std::size_t slot_index_ = 0;
        std::queue<std::pair<StaticBuffer<T, BufferCapacity>*, std::size_t>> skipped_items_;
        WriteHandle handle_;
        bool is_end_ = true;
        bool retry_mode_ = false;
    };

    /**
     * @brief 线程安全的只读迭代器 (Input Iterator)
     *
     * @note 使用基于索引的方式而非迭代器，以避免并发扩容/缩容导致的迭代器失效问题
     */
    class ConstIterator {
       public:
        using iterator_category = std::input_iterator_tag;
        using value_type = const T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        ConstIterator() = default;
        ConstIterator(const Storage* storage, bool is_end) : storage_(storage), is_end_(is_end) {
            if (!is_end_ && storage_) {
                advance();
            }
        }

        ConstIterator(ConstIterator&&) = default;
        ConstIterator& operator=(ConstIterator&&) = default;

        const T& operator*() const { return *handle_; }
        const T* operator->() const { return handle_.get(); }

        ConstIterator& operator++() {
            handle_ = ReadHandle();
            advance();
            return *this;
        }

        bool operator!=(const ConstIterator& other) const { return is_end_ != other.is_end_; }
        bool operator==(const ConstIterator& other) const { return is_end_ == other.is_end_; }

       private:
        /**
         * @brief 根据索引获取对应的 buffer 指针
         * @param index buffer 索引
         * @return buffer 指针，如果索引超出范围则返回 nullptr
         * @note 必须在持有 list_mutex_ 共享锁时调用
         */
        const StaticBuffer<T, BufferCapacity>* get_buffer_at(std::size_t index) const {
            auto it = storage_->buffers_.begin();
            for (std::size_t i = 0; i < index && it != storage_->buffers_.end(); ++i, ++it) {
            }
            return (it != storage_->buffers_.end()) ? &(*it) : nullptr;
        }

        void advance() {
            while (true) {
                if (!retry_mode_) {
                    // 检查是否需要移动到下一个 buffer
                    if (slot_index_ >= BufferCapacity) {
                        ++buffer_index_;
                        slot_index_ = 0;
                    }

                    // 获取当前 buffer
                    const StaticBuffer<T, BufferCapacity>* buffer = nullptr;
                    {
                        std::shared_lock lock(storage_->list_mutex_);
                        buffer = get_buffer_at(buffer_index_);
                        if (!buffer) {
                            // 已经遍历完所有 buffer，进入重试模式
                            retry_mode_ = true;
                            continue;
                        }
                    }

                    // 检查当前槽位是否被占用
                    if (buffer->occupied[slot_index_].load(std::memory_order_acquire)) {
                        std::shared_lock slot_lock(buffer->mutexes[slot_index_], std::try_to_lock);
                        if (slot_lock.owns_lock()) {
                            // 再次检查占用状态（double-check）
                            if (buffer->occupied[slot_index_].load(std::memory_order_acquire)) {
                                handle_ = ReadHandle(&buffer->buffer[slot_index_], std::move(slot_lock));
                                ++slot_index_;
                                return;
                            }
                        } else {
                            // 锁定失败，加入跳过队列稍后重试
                            skipped_items_.push({buffer, slot_index_});
                        }
                    }
                    ++slot_index_;
                } else {
                    // 重试模式：处理之前跳过的槽位
                    if (skipped_items_.empty()) {
                        is_end_ = true;
                        return;
                    }
                    auto [buffer, index] = skipped_items_.front();
                    skipped_items_.pop();

                    // 阻塞等待获取锁
                    std::shared_lock slot_lock(buffer->mutexes[index]);
                    if (buffer->occupied[index].load(std::memory_order_acquire)) {
                        handle_ = ReadHandle(&buffer->buffer[index], std::move(slot_lock));
                        return;
                    }
                    // 如果槽位已被释放，继续处理下一个跳过的项
                }
            }
        }

        const Storage* storage_ = nullptr;
        std::size_t buffer_index_ = 0;  ///< 当前 buffer 索引（替代迭代器）
        std::size_t slot_index_ = 0;
        mutable std::queue<std::pair<const StaticBuffer<T, BufferCapacity>*, std::size_t>> skipped_items_;
        mutable ReadHandle handle_;
        bool is_end_ = true;
        bool retry_mode_ = false;
    };

    Iterator begin() { return Iterator(this, false); }
    Iterator end() { return Iterator(this, true); }
    ConstIterator begin() const { return ConstIterator(this, false); }
    ConstIterator end() const { return ConstIterator(this, true); }
    ConstIterator cbegin() const { return ConstIterator(this, false); }
    ConstIterator cend() const { return ConstIterator(this, true); }

   private:
    /**
     * @brief 根据槽位 ID 查找其所属的 StaticBuffer 及其索引
     *
     * @param id 槽位 ID（内存地址）
     * @return std::pair<std::int64_t, StaticBuffer*>
     *         - first: buffer 在列表中的索引，未找到返回 -1
     *         - second: 指向 StaticBuffer 的指针，未找到返回 nullptr
     *
     * @note 查找逻辑：
     *       1. 加 buffer 列表共享锁
     *       2. 遍历所有 buffer，检查 id 是否在该 buffer 的地址范围内
     *       3. 地址范围：[&buffer[0], &buffer[BufferCapacity-1]]
     *
     * @note 线程安全，使用共享锁保护 buffer 列表
     */
    [[nodiscard]]
    std::pair<std::int64_t, StaticBuffer<T, BufferCapacity>*> get_parent_buffer(ObjectId id) {
        std::int64_t index = 0;
        std::shared_lock lock(list_mutex_);
        for (auto& buffer : buffers_) {
            if (id >= reinterpret_cast<ObjectId>(&(buffer.buffer[0])) &&
                id <= reinterpret_cast<ObjectId>(&(buffer.buffer[BufferCapacity - 1]))) {
                return {index, &buffer};
            }
            ++index;
        }
        return {-1, nullptr};
    }

    [[nodiscard]]
    ObjectId allocate_from_buffer(StaticBuffer<T, BufferCapacity>& buffer) {
        std::unique_lock alloc_lock(buffer.alloc_mutex);
        if (buffer.free_indices.empty()) return 0;

        std::size_t idx = buffer.free_indices.back();
        buffer.free_indices.pop_back();
        if (buffer.free_indices.empty()) {
            buffer.is_full.store(true, std::memory_order_release);
        }
        buffer.active_count.fetch_add(1, std::memory_order_relaxed);
        alloc_lock.unlock();

        T* ptr = &buffer.buffer[idx];
        ObjectId id = reinterpret_cast<ObjectId>(ptr);

        std::unique_lock slot_lock(buffer.mutexes[idx]);
        try {
            *ptr = T{};
        } catch (...) {
            std::unique_lock rb_lock(buffer.alloc_mutex);
            buffer.free_indices.push_back(idx);
            buffer.is_full.store(false, std::memory_order_release);
            buffer.active_count.fetch_sub(1, std::memory_order_relaxed);
            CFW_LOG_FLUSH();
            throw;
        }
        buffer.occupied[idx].store(true, std::memory_order_release);
        occupied_count_.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

   private:
    std::atomic<std::size_t> occupied_count_{0};             ///< 已占用槽位计数
    mutable std::shared_mutex list_mutex_;                   ///< 保护 buffers_ 列表的锁（mutable 允许 const 方法加锁）
    std::list<StaticBuffer<T, BufferCapacity>> buffers_;     ///< 底层 buffer 列表
    std::atomic<std::size_t> buffer_count_{InitialBuffers};  ///< 当前 buffer 数量
};

}  // namespace Corona::Kernel::Utils