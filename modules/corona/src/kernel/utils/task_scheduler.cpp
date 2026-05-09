#include "corona/kernel/utils/task_scheduler.h"

#include <random>

// 单例实例获取（线程安全的懒汉式）
Corona::Kernal::Utils::TaskScheduler& Corona::Kernal::Utils::TaskScheduler::instance() {
    static TaskScheduler instance;  // C++11 保证静态局部变量的线程安全初始化
    return instance;
}

// 构造函数：初始化线程池和任务队列
Corona::Kernal::Utils::TaskScheduler::TaskScheduler()
    : thread_count_(std::thread::hardware_concurrency()),  // 根据 CPU 核心数创建线程
      task_queues_(thread_count_),                         // 为每个线程创建独立队列
      threads_(thread_count_) {                            // 预留线程容器空间
    // 启动所有工作线程
    for (std::size_t i = 0; i < thread_count_; ++i) {
        threads_.emplace_back(&TaskScheduler::worker_loop, this, i, stop_source_.get_token());
    }
}

// 析构函数：优雅地关闭所有工作线程
Corona::Kernal::Utils::TaskScheduler::~TaskScheduler() {
    stop_source_.request_stop();  // 请求所有线程停止（协作式取消）
    cv_.notify_all();             // 唤醒所有休眠的线程，让它们检查停止标志
    // jthread 的析构函数会自动 join，等待所有线程退出
}

// 提交任务到调度器（轮询分配策略）
void Corona::Kernal::Utils::TaskScheduler::submit(Task task) {
    // 使用原子递增的计数器实现轮询分配，避免竞争
    uint32_t index = submission_index_.fetch_add(1) % thread_count_;
    // 将任务推入对应的队列
    task_queues_[index].push(std::move(task));
    // 唤醒一个休眠的工作线程（如果有）
    cv_.notify_one();
}

// 外部线程尝试执行一个任务（用于主线程参与式等待）
bool Corona::Kernal::Utils::TaskScheduler::try_execute_task() {
    return try_steal_and_execute();  // 直接调用窃取逻辑，不指定排除索引
}

// 工作线程的主循环：不断取任务执行，直到收到停止信号
void Corona::Kernal::Utils::TaskScheduler::worker_loop(std::uint32_t index, std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        // 步骤 1：尝试从自己的队列中取任务（FIFO，从头部取）
        if (auto task = task_queues_[index].try_pop()) {
            (*task)();  // 执行任务
        }
        // 步骤 2：如果自己的队列为空，尝试从其他线程窃取任务
        else if (try_steal_and_execute(index)) {
            continue;  // 成功窃取并执行，继续下一轮循环
        }
        // 步骤 3：如果窃取也失败，进入休眠等待新任务
        else {
            std::unique_lock lock(cv_mutex_);
            // 等待直到：1) 收到停止信号，或 2) 有新任务提交
            cv_.wait(lock, [this, &stop_token, index]() {
                return stop_token.stop_requested() || has_any_task();
            });
        }
    }
}

// 尝试从其他线程窃取任务并执行
bool Corona::Kernal::Utils::TaskScheduler::try_steal_and_execute(std::optional<std::uint32_t> index) {
    // 随机生成起始索引，避免所有线程总是从同一个队列开始窃取
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<std::uint32_t> dis(0, thread_count_ - 1);
    std::uint32_t start_index = dis(gen);

    // 遍历所有队列尝试窃取（从随机位置开始，循环一圈）
    for (std::uint32_t i = 0; i < thread_count_; ++i) {
        std::uint32_t target_index = (start_index + i) % thread_count_;
        // 如果提供了 index，跳过自己的队列（避免重复尝试）
        if (index.has_value() && target_index == index.value()) {
            continue;
        }
        // 尝试从目标队列的尾部窃取任务（LIFO，减少与所有者的竞争）
        if (auto task = task_queues_[target_index].try_steal()) {
            (*task)();  // 立即执行窃取到的任务
            return true;
        }
    }
    return false;  // 所有队列都为空，窃取失败
}

// 检查是否还有任务待执行（简化实现）
bool Corona::Kernal::Utils::TaskScheduler::has_any_task() const {
    return true;  // 简化实现，始终返回 true
    // TODO: 未来可以优化为实际遍历所有队列检查是否为空
    //       但这会引入额外的锁竞争，需要权衡性能
}