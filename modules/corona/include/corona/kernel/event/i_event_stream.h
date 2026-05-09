#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "event_concepts.h"

namespace Corona::Kernel {

// ========================================
// 事件流配置
// ========================================

/**
 * @brief 背压策略枚举
 *
 * 定义当订阅者队列已满时的处理策略
 */
enum class BackpressurePolicy {
    Block,       ///< 阻塞发布者，直到队列有空闲空间
    DropOldest,  ///< 丢弃最旧的事件，添加新事件
    DropNewest   ///< 丢弃新事件，保留队列中的旧事件
};

/**
 * @brief 事件流订阅选项
 *
 * 配置订阅者的队列大小和背压策略
 */
struct EventStreamOptions {
    std::size_t max_queue_size = 256;                       ///< 最大队列大小
    BackpressurePolicy policy = BackpressurePolicy::Block;  ///< 背压策略
};

// ========================================
// 事件订阅句柄（RAII）
// ========================================

/**
 * @brief 事件订阅句柄类
 *
 * EventSubscription 是一个 RAII 包装器，管理对 EventStream 的订阅。
 * 当订阅对象销毁时，自动取消订阅。
 *
 * 特性：
 * - RAII：自动管理订阅生命周期
 * - 移动语义：支持移动，禁止拷贝
 * - 非阻塞拉取：try_pop() 立即返回
 * - 阻塞等待：wait() 和 wait_for() 等待新事件
 *
 * 使用示例：
 * @code
 * auto stream = event_stream->get_stream<MyEvent>();
 * auto sub = stream->subscribe();
 *
 * // 非阻塞拉取
 * if (auto event = sub.try_pop()) {
 *     // 处理事件
 * }
 *
 * // 阻塞等待（带超时）
 * if (auto event = sub.wait_for(std::chrono::milliseconds(100))) {
 *     // 处理事件
 * }
 *
 * // 无限等待
 * if (auto event = sub.wait()) {
 *     // 处理事件
 * }
 * @endcode
 */
template <Event T>
class EventSubscription {
   public:
    EventSubscription() = default;
    EventSubscription(EventSubscription&&) noexcept = default;
    EventSubscription& operator=(EventSubscription&&) noexcept = default;
    ~EventSubscription();

    // 禁止拷贝
    EventSubscription(const EventSubscription&) = delete;
    EventSubscription& operator=(const EventSubscription&) = delete;

    /**
     * @brief 检查订阅是否有效
     * @return 订阅有效返回 true，已关闭或无效返回 false
     */
    bool is_valid() const;

    /**
     * @brief 尝试弹出一个事件（非阻塞）
     * @return 如果队列中有事件则返回事件，否则返回 nullopt
     */
    std::optional<T> try_pop();

    /**
     * @brief 等待事件（带超时）
     * @param timeout 超时时间
     * @return 在超时前收到事件返回事件，否则返回 nullopt
     */
    std::optional<T> wait_for(std::chrono::milliseconds timeout);

    /**
     * @brief 无限等待事件
     * @return 收到事件返回事件，订阅关闭返回 nullopt
     */
    std::optional<T> wait();

    /**
     * @brief 关闭订阅
     *
     * 关闭后不会再接收新事件，等待中的线程会被唤醒
     */
    void close();

   private:
    template <Event U>
    friend class EventStream;

    struct State;

    explicit EventSubscription(std::shared_ptr<State> state);

    void release();

    std::shared_ptr<State> state_;
};

// ========================================
// 事件流（队列消息模式）
// ========================================

/**
 * @brief 事件流类
 *
 * EventStream 提供基于队列的异步消息传递机制。
 * 与 IEventBus 不同，事件流将事件存储在队列中，订阅者按需拉取。
 *
 * 特性：
 * - 异步解耦：发布者和订阅者时间解耦
 * - 独立队列：每个订阅者有独立的队列
 * - 背压控制：支持多种队列满时的处理策略
 * - 线程安全：支持多生产者多消费者模式
 *
 * 与 IEventBus 的对比：
 * - EventBus：即时同步调用，无缓冲
 * - EventStream：异步队列消息，订阅者控制消费速度
 *
 * 使用场景：
 * - 生产者和消费者速度不匹配
 * - 需要削峰填谷的消息处理
 * - 跨线程异步通信
 *
 * 使用示例：
 * @code
 * auto stream = std::make_shared<EventStream<NetworkEvent>>();
 *
 * // 订阅者线程
 * auto sub = stream->subscribe(EventStreamOptions{
 *     .max_queue_size = 100,
 *     .policy = BackpressurePolicy::DropOldest
 * });
 *
 * // 消费线程
 * while (running) {
 *     if (auto event = sub.wait_for(std::chrono::seconds(1))) {
 *         process(*event);
 *     }
 * }
 *
 * // 生产者线程
 * stream->publish(NetworkEvent{...});
 * @endcode
 */
template <Event T>
class EventStream {
   public:
    using value_type = T;

    EventStream() = default;
    ~EventStream() = default;

    // 禁止拷贝和移动
    EventStream(const EventStream&) = delete;
    EventStream& operator=(const EventStream&) = delete;
    EventStream(EventStream&&) = delete;
    EventStream& operator=(EventStream&&) = delete;

    /**
     * @brief 订阅事件流
     * @param options 订阅选项（队列大小、背压策略）
     * @return 订阅句柄，管理订阅生命周期
     */
    EventSubscription<T> subscribe(EventStreamOptions options = {});

    /**
     * @brief 发布事件（拷贝）
     * @param event 要发布的事件
     */
    void publish(const T& event);

    /**
     * @brief 发布事件（移动）
     * @param event 要发布的事件
     */
    void publish(T&& event);

    /**
     * @brief 获取活跃订阅者数量
     * @return 当前订阅者数量
     */
    std::size_t subscriber_count() const;

   private:
    friend class EventSubscription<T>;

    /// @brief 订阅者状态（每个订阅者一个）
    struct SubscriberState {
        std::size_t id;              ///< 订阅者 ID
        EventStreamOptions options;  ///< 订阅选项
        std::deque<T> queue;         ///< 事件队列
        std::mutex mutex;            ///< 队列互斥锁
        std::condition_variable cv;  ///< 条件变量（用于阻塞等待）
        bool closed = false;         ///< 关闭标志
    };

    void publish_impl(const T& event);
    void unsubscribe(std::size_t id);

    mutable std::mutex subscribers_mutex_;                                           ///< 订阅者列表互斥锁
    std::unordered_map<std::size_t, std::shared_ptr<SubscriberState>> subscribers_;  ///< 订阅者列表
    std::size_t next_id_ = 1;                                                        ///< 下一个订阅者 ID
};

// ========================================
// 事件总线流接口
// ========================================

/**
 * @brief 事件总线流接口
 *
 * IEventBusStream 是所有 EventStream 的中央注册表。
 * 通过此接口可以获取或创建特定类型的事件流。
 *
 * 使用示例：
 * @code
 * auto bus_stream = KernelContext::instance().event_stream();
 *
 * // 获取或创建事件流（线程安全）
 * auto stream = bus_stream->get_stream<NetworkEvent>();
 *
 * // 订阅和发布
 * auto sub = stream->subscribe();
 * stream->publish(NetworkEvent{...});
 * @endcode
 */
class IEventBusStream {
   public:
    virtual ~IEventBusStream() = default;

    /**
     * @brief 获取或创建指定类型的事件流
     * @tparam T 事件类型
     * @return 事件流的共享指针
     *
     * 线程安全：如果多个线程同时调用，只会创建一个实例
     */
    template <Event T>
    std::shared_ptr<EventStream<T>> get_stream();

   protected:
    virtual std::shared_ptr<void> get_stream_impl(std::type_index type) = 0;
    virtual std::shared_ptr<void> get_or_create_stream_impl(std::type_index type,
                                                            std::shared_ptr<void> new_stream) = 0;
};

// ========================================
// EventSubscription 实现
// ========================================

/// @brief EventSubscription 内部状态
template <Event T>
struct EventSubscription<T>::State {
    std::shared_ptr<typename EventStream<T>::SubscriberState> subscriber;  ///< 订阅者状态
    std::size_t stream_id;                                                 ///< 流 ID（用于取消订阅）
};

template <Event T>
EventSubscription<T>::EventSubscription(std::shared_ptr<State> state)
    : state_(std::move(state)) {}

template <Event T>
EventSubscription<T>::~EventSubscription() {
    release();
}

template <Event T>
bool EventSubscription<T>::is_valid() const {
    return state_ && state_->subscriber && !state_->subscriber->closed;
}

template <Event T>
void EventSubscription<T>::close() {
    release();
}

template <Event T>
void EventSubscription<T>::release() {
    if (!state_ || !state_->subscriber) {
        return;
    }

    auto sub = state_->subscriber;
    state_.reset();

    std::lock_guard<std::mutex> lock(sub->mutex);
    sub->closed = true;
    sub->queue.clear();
    sub->cv.notify_all();  // 唤醒所有等待的线程
}

template <Event T>
std::optional<T> EventSubscription<T>::try_pop() {
    if (!state_ || !state_->subscriber) {
        return std::nullopt;
    }

    auto sub = state_->subscriber;
    std::lock_guard<std::mutex> lock(sub->mutex);

    if (sub->queue.empty() || sub->closed) {
        return std::nullopt;
    }

    T event = std::move(sub->queue.front());
    sub->queue.pop_front();
    sub->cv.notify_all();  // 通知可能阻塞的发布者
    return event;
}

template <Event T>
std::optional<T> EventSubscription<T>::wait_for(std::chrono::milliseconds timeout) {
    if (!state_ || !state_->subscriber) {
        return std::nullopt;
    }

    auto sub = state_->subscriber;
    std::unique_lock<std::mutex> lock(sub->mutex);

    // 等待事件或超时
    if (!sub->cv.wait_for(lock, timeout, [&] { return sub->closed || !sub->queue.empty(); })) {
        return std::nullopt;  // 超时
    }

    if (sub->closed || sub->queue.empty()) {
        return std::nullopt;
    }

    T event = std::move(sub->queue.front());
    sub->queue.pop_front();
    sub->cv.notify_all();
    return event;
}

template <Event T>
std::optional<T> EventSubscription<T>::wait() {
    if (!state_ || !state_->subscriber) {
        return std::nullopt;
    }

    auto sub = state_->subscriber;
    std::unique_lock<std::mutex> lock(sub->mutex);

    // 无限等待事件
    sub->cv.wait(lock, [&] { return sub->closed || !sub->queue.empty(); });

    if (sub->closed || sub->queue.empty()) {
        return std::nullopt;
    }

    T event = std::move(sub->queue.front());
    sub->queue.pop_front();
    sub->cv.notify_all();
    return event;
}

// ========================================
// EventStream 实现
// ========================================

template <Event T>
EventSubscription<T> EventStream<T>::subscribe(EventStreamOptions options) {
    auto sub = std::make_shared<SubscriberState>();
    sub->id = next_id_++;
    sub->options = options;

    // 确保队列大小至少为 1
    if (sub->options.max_queue_size == 0) {
        sub->options.max_queue_size = 1;
    }

    {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_.emplace(sub->id, sub);
    }

    auto state = std::make_shared<typename EventSubscription<T>::State>();
    state->subscriber = std::move(sub);

    return EventSubscription<T>(std::move(state));
}

template <Event T>
void EventStream<T>::publish(const T& event) {
    publish_impl(event);
}

template <Event T>
void EventStream<T>::publish(T&& event) {
    publish_impl(event);
}

template <Event T>
void EventStream<T>::publish_impl(const T& event) {
    // 快照订阅者列表，避免在发布时持有锁（遵循 event_bus.cpp 的模式）
    std::vector<std::shared_ptr<SubscriberState>> snapshot;
    {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        snapshot.reserve(subscribers_.size());
        for (const auto& [id, sub] : subscribers_) {
            snapshot.push_back(sub);
        }
    }

    // 向每个订阅者发布事件
    for (auto& sub : snapshot) {
        std::unique_lock<std::mutex> lock(sub->mutex);

        if (sub->closed) {
            continue;
        }

        const auto max_size = sub->options.max_queue_size;

        switch (sub->options.policy) {
            case BackpressurePolicy::Block: {
                // 阻塞等待队列有空间
                sub->cv.wait(lock, [&] { return sub->closed || sub->queue.size() < max_size; });

                if (sub->closed) {
                    continue;
                }

                sub->queue.push_back(event);
                break;
            }

            case BackpressurePolicy::DropOldest: {
                // 队列满时移除最旧的事件
                if (sub->queue.size() >= max_size && !sub->queue.empty()) {
                    sub->queue.pop_front();
                }
                sub->queue.push_back(event);
                break;
            }

            case BackpressurePolicy::DropNewest: {
                // 队列满时丢弃新事件
                if (sub->queue.size() < max_size) {
                    sub->queue.push_back(event);
                }
                break;
            }
        }

        lock.unlock();
        sub->cv.notify_all();  // 通知等待的消费者
    }
}

template <Event T>
void EventStream<T>::unsubscribe(std::size_t id) {
    std::shared_ptr<SubscriberState> removed;

    {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        auto it = subscribers_.find(id);
        if (it != subscribers_.end()) {
            removed = it->second;
            subscribers_.erase(it);
        }
    }

    if (removed) {
        std::lock_guard<std::mutex> lock(removed->mutex);
        removed->closed = true;
        removed->queue.clear();
        removed->cv.notify_all();
    }
}

template <Event T>
std::size_t EventStream<T>::subscriber_count() const {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    return subscribers_.size();
}

// ========================================
// IEventBusStream 实现
// ========================================

template <Event T>
std::shared_ptr<EventStream<T>> IEventBusStream::get_stream() {
    auto type = std::type_index(typeid(T));

    // 快速路径：检查流是否已存在（大多数实现中是无锁读取）
    auto stream = get_stream_impl(type);
    if (stream) {
        return std::static_pointer_cast<EventStream<T>>(stream);
    }

    // 慢速路径：需要创建流
    // 在锁外创建以最小化持锁时间
    auto new_stream = std::make_shared<EventStream<T>>();

    // 原子地检查并插入（实现中会持有锁）
    // 如果其他线程先创建了，返回已存在的
    auto registered = get_or_create_stream_impl(type, new_stream);

    return std::static_pointer_cast<EventStream<T>>(registered);
}

// ========================================
// 工厂函数
// ========================================

/**
 * @brief 创建独立的事件流
 * @tparam T 事件类型
 * @param max_queue_size 最大队列大小（未使用，保留用于未来扩展）
 * @return 事件流的共享指针
 */
template <Event T>
std::shared_ptr<EventStream<T>> create_event_stream(std::size_t max_queue_size = 256) {
    return std::make_shared<EventStream<T>>();
}

/**
 * @brief 创建事件总线流实例
 * @return 事件总线流的唯一指针
 *
 * 事件总线流是所有 EventStream 的中央注册表
 */
std::unique_ptr<IEventBusStream> create_event_bus_stream();

}  // namespace Corona::Kernel
