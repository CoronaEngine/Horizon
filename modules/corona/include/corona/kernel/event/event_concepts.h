#pragma once
#include <type_traits>

namespace Corona::Kernel {

/**
 * @brief 事件类型约束（C++20 概念）
 *
 * 定义了作为事件的类型必须满足的要求：
 * - 可拷贝构造：事件在发布时可能需要复制到多个订阅者
 * - 可移动构造：支持高效的事件传递
 *
 * 符合此概念的类型可以用于 IEventBus 和 EventStream
 *
 * 示例：
 * @code
 * struct MyEvent {
 *     int value;
 *     std::string message;
 * };  // 自动满足 Event 概念
 *
 * EventBus::subscribe<MyEvent>([](const MyEvent& e) { ... });
 * @endcode
 */
template <typename T>
concept Event = std::is_copy_constructible_v<T> && std::is_move_constructible_v<T>;

}  // namespace Corona::Kernel
