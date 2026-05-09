#pragma once
#include <functional>
#include <typeindex>

#include "event_concepts.h"

namespace Corona::Kernel {

using EventId = std::size_t;  ///< 事件订阅 ID 类型

/**
 * @brief 事件处理器约束（C++20 概念）
 *
 * 定义了事件处理器必须满足的要求：
 * - 必须是可调用对象
 * - 参数类型为 const T&
 * - T 必须满足 Event 概念
 *
 * 示例：
 * @code
 * // Lambda 表达式
 * auto handler1 = [](const MyEvent& e) { ... };
 *
 * // 函数指针
 * void handler2(const MyEvent& e) { ... }
 *
 * // 函数对象
 * struct Handler3 {
 *     void operator()(const MyEvent& e) { ... }
 * };
 * @endcode
 */
template <typename Handler, typename T>
concept EventHandler = Event<T> && std::invocable<Handler, const T&>;

/**
 * @brief 事件总线接口（即时消息模式）
 *
 * IEventBus 提供即时的发布-订阅消息机制。
 * 当事件被发布时，所有订阅者的处理函数会被立即同步调用。
 *
 * 特性：
 * - 类型安全：使用 C++20 概念确保类型正确
 * - 即时触发：publish() 会立即调用所有订阅者
 * - 线程安全：可以从多个线程安全地订阅和发布
 * - 异常安全：单个处理器的异常不会影响其他处理器
 *
 * 与 EventStream 的区别：
 * - EventBus：即时同步调用，无队列缓冲
 * - EventStream：异步队列消息，订阅者按需拉取
 *
 * 使用示例：
 * @code
 * struct GameEvent {
 *     std::string message;
 *     int value;
 * };
 *
 * auto bus = KernelContext::instance().event_bus();
 *
 * // 订阅
 * auto id = bus->subscribe<GameEvent>([](const GameEvent& e) {
 *     std::cout << "收到事件: " << e.message << std::endl;
 * });
 *
 * // 发布
 * bus->publish(GameEvent{"测试", 42});
 *
 * // 取消订阅
 * bus->unsubscribe(id);
 * @endcode
 */
class IEventBus {
   public:
    virtual ~IEventBus() = default;

    /**
     * @brief 订阅特定类型的事件
     * @tparam T 事件类型，必须满足 Event 概念
     * @tparam Handler 处理器类型，必须满足 EventHandler<T> 概念
     * @param handler 事件处理函数，接受 const T& 参数
     * @return 订阅 ID，用于后续取消订阅
     *
     * 处理器在订阅时被拷贝或移动到内部存储，之后可以安全地销毁原对象
     */
    template <Event T, EventHandler<T> Handler>
    EventId subscribe(Handler&& handler) {
        return subscribe_impl(std::type_index(typeid(T)),
                              [handler = std::forward<Handler>(handler)](const void* event_ptr) mutable {
                                  handler(*static_cast<const T*>(event_ptr));
                              });
    }

    /**
     * @brief 取消事件订阅
     * @param id 订阅时返回的 EventId
     *
     * 取消订阅后，该处理器不会再收到事件通知
     */
    virtual void unsubscribe(EventId id) = 0;

    /**
     * @brief 发布事件
     * @tparam T 事件类型，必须满足 Event 概念
     * @param event 要发布的事件对象
     *
     * 同步调用所有订阅了该类型事件的处理器。
     * 如果某个处理器抛出异常，不会影响其他处理器的执行。
     */
    template <Event T>
    void publish(const T& event) {
        publish_impl(std::type_index(typeid(T)), &event);
    }

   protected:
    /// @brief 类型擦除的事件处理器类型
    using TypeErasedHandler = std::function<void(const void*)>;

    /**
     * @brief 内部订阅实现（类型擦除）
     * @param type 事件类型的 type_index
     * @param handler 类型擦除的处理器
     * @return 订阅 ID
     */
    virtual EventId subscribe_impl(std::type_index type, TypeErasedHandler handler) = 0;

    /**
     * @brief 内部发布实现（类型擦除）
     * @param type 事件类型的 type_index
     * @param event_ptr 指向事件对象的指针
     */
    virtual void publish_impl(std::type_index type, const void* event_ptr) = 0;
};

}  // namespace Corona::Kernel
