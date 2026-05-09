#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "corona/kernel/event/i_event_bus.h"

namespace Corona::Kernel {

/**
 * @brief 事件总线实现类
 *
 * EventBus 是 IEventBus 接口的具体实现，提供线程安全的即时事件分发。
 *
 * 线程安全策略：
 * - 订阅和取消订阅：持锁操作，修改订阅列表
 * - 发布事件：先拷贝处理器列表再释放锁，避免死锁
 * - 异常隔离：单个处理器的异常不影响其他处理器
 */
class EventBus : public IEventBus {
   public:
    EventBus() : next_id_(0) {}

    void unsubscribe(EventId id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        // 遍历所有事件类型，查找并移除指定 ID 的订阅
        for (auto& [type, handlers] : subscriptions_) {
            auto it = std::remove_if(handlers.begin(), handlers.end(),
                                     [id](const Subscription& sub) { return sub.id == id; });
            if (it != handlers.end()) {
                handlers.erase(it, handlers.end());
                break;  // 找到后退出，每个 ID 只对应一个订阅
            }
        }
    }

   protected:
    EventId subscribe_impl(std::type_index type, TypeErasedHandler handler) override {
        std::lock_guard<std::mutex> lock(mutex_);
        EventId id = next_id_++;
        auto& handlers = subscriptions_[type];
        handlers.reserve(handlers.size() + 1);  // 优化内存分配，减少重新分配
        handlers.push_back({id, std::move(handler)});
        return id;
    }

    void publish_impl(std::type_index type, const void* event_ptr) override {
        // 拷贝处理器列表（在锁外调用，避免死锁）
        std::vector<TypeErasedHandler> handlers_copy;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = subscriptions_.find(type);
            if (it != subscriptions_.end()) {
                handlers_copy.reserve(it->second.size());  // 预分配精确大小
                for (const auto& sub : it->second) {
                    handlers_copy.push_back(sub.handler);
                }
            }
        }

        // 在锁外调用处理器，避免死锁和减少锁竞争
        // 捕获异常以防止单个订阅者崩溃整个系统
        for (const auto& handler : handlers_copy) {
            try {
                handler(event_ptr);
            } catch (const std::exception& e) {
                // 捕获异常但继续处理其他处理器
                // 在生产环境中，这里可以记录到日志系统
                // 目前静默处理以保证系统稳定性
                (void)e;  // 抑制未使用变量警告
            } catch (...) {
                // 捕获所有其他异常以确保健壮性
            }
        }
    }

   private:
    /// @brief 订阅信息结构体
    struct Subscription {
        EventId id;                 ///< 订阅 ID
        TypeErasedHandler handler;  ///< 类型擦除的处理器
    };

    std::map<std::type_index, std::vector<Subscription>> subscriptions_;  ///< 订阅列表（按类型索引）
    std::mutex mutex_;                                                    ///< 保护订阅列表的互斥锁
    EventId next_id_;                                                     ///< 下一个订阅 ID
};

/**
 * @brief 创建事件总线实例
 * @return 事件总线的唯一指针
 */
std::unique_ptr<IEventBus> create_event_bus() {
    return std::make_unique<EventBus>();
}

}  // namespace Corona::Kernel
