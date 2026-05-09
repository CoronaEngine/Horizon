#include "corona/kernel/system/system_base.h"

#include <chrono>
#include <thread>

namespace Corona::Kernel {

SystemBase::SystemBase()
    : state_(SystemState::idle),
      should_run_(false),
      is_paused_(false),
      context_(nullptr),
      target_fps_(60),
      frame_number_(0),
      last_delta_time_(0.0f),
      total_frames_(0),
      total_frame_time_(0.0),
      max_frame_time_(0.0f),
      stats_window_size_(60) {}

SystemBase::~SystemBase() {
    if (thread_.joinable()) {
        stop();
    }
}

// ========================================
// ISystem 接口实现
// ========================================

SystemState SystemBase::get_state() const {
    return state_.load(std::memory_order_acquire);
}

int SystemBase::get_target_fps() const {
    return target_fps_;
}

void SystemBase::set_target_fps(int fps) {
    target_fps_ = fps;
}

void SystemBase::start() {
    std::lock_guard<std::mutex> lock(control_mutex_);

    // 只能从 idle 或 stopped 状态启动
    if (state_ != SystemState::idle && state_ != SystemState::stopped) {
        return;
    }

    should_run_.store(true, std::memory_order_release);
    is_paused_.store(false, std::memory_order_release);
    state_.store(SystemState::running, std::memory_order_release);

    // 创建工作线程
    thread_ = std::thread(&SystemBase::thread_loop, this);
}

void SystemBase::pause() {
    std::lock_guard<std::mutex> lock(control_mutex_);

    if (state_ != SystemState::running) {
        return;
    }

    is_paused_.store(true, std::memory_order_release);
    state_.store(SystemState::paused, std::memory_order_release);
}

void SystemBase::resume() {
    std::lock_guard<std::mutex> lock(control_mutex_);

    if (state_ != SystemState::paused) {
        return;
    }

    {
        std::lock_guard<std::mutex> pause_lock(pause_mutex_);
        is_paused_.store(false, std::memory_order_release);
        state_.store(SystemState::running, std::memory_order_release);
    }
    pause_cv_.notify_one();  // 唤醒暂停的线程
}

void SystemBase::stop() {
    std::lock_guard<std::mutex> lock(control_mutex_);

    if (state_ == SystemState::stopped || state_ == SystemState::idle) {
        return;
    }

    state_.store(SystemState::stopping, std::memory_order_release);
    should_run_.store(false, std::memory_order_release);

    // 唤醒可能在暂停或等待的线程
    {
        std::lock_guard<std::mutex> pause_lock(pause_mutex_);
        is_paused_.store(false, std::memory_order_release);
    }
    pause_cv_.notify_one();

    // 等待线程结束
    if (thread_.joinable()) {
        thread_.join();
    }

    state_.store(SystemState::stopped, std::memory_order_release);
}

// ========================================
// 性能统计接口实现
// ========================================

float SystemBase::get_actual_fps() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (total_frames_ == 0) return 0.0f;
    return static_cast<float>(total_frames_) / total_frame_time_;
}

float SystemBase::get_average_frame_time() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (total_frames_ == 0) return 0.0f;
    return static_cast<float>((total_frame_time_ / total_frames_) * 1000.0);  // 转换为毫秒
}

float SystemBase::get_max_frame_time() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return max_frame_time_;
}

std::uint64_t SystemBase::get_total_frames() const {
    return total_frames_.load(std::memory_order_relaxed);
}

void SystemBase::reset_stats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    total_frames_.store(0, std::memory_order_relaxed);
    total_frame_time_ = 0.0;
    max_frame_time_ = 0.0f;
}

// ========================================
// Protected 方法
// ========================================

ISystemContext* SystemBase::context() {
    return context_;
}

const ISystemContext* SystemBase::context() const {
    return context_;
}

uint64_t SystemBase::frame_number() const {
    return frame_number_;
}

float SystemBase::delta_time() const {
    return last_delta_time_;
}

void SystemBase::thread_loop() {
    this->on_thread_started();

    auto last_time = std::chrono::high_resolution_clock::now();

    // 计算帧时间间隔
    std::chrono::microseconds frame_duration(0);
    if (target_fps_ > 0) {
        frame_duration = std::chrono::microseconds(1000000 / target_fps_);
    }

    while (should_run_.load(std::memory_order_acquire)) {
        auto frame_start = std::chrono::high_resolution_clock::now();

        // 检查暂停状态
        if (is_paused_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(pause_mutex_);
            pause_cv_.wait(lock, [this] {
                return !is_paused_.load(std::memory_order_acquire) ||
                       !should_run_.load(std::memory_order_acquire);
            });
            // 恢复后重置时间，避免大的 delta_time 跳变
            last_time = std::chrono::high_resolution_clock::now();
            continue;
        }

        // 计算 delta_time
        auto current_time = std::chrono::high_resolution_clock::now();
        last_delta_time_ = std::chrono::duration<float>(current_time - last_time).count();
        last_time = current_time;

        // 调用子类的更新逻辑
        update();

        ++frame_number_;

        // 收集性能统计
        auto frame_end = std::chrono::high_resolution_clock::now();
        float frame_time = std::chrono::duration<float>(frame_end - frame_start).count();
        float frame_time_ms = frame_time * 1000.0f;

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            total_frames_.fetch_add(1, std::memory_order_relaxed);
            total_frame_time_ += frame_time;
            if (frame_time_ms > max_frame_time_) {
                max_frame_time_ = frame_time_ms;
            }
        }

        // 帧率限制（如果目标 FPS > 0）
        if (target_fps_ > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start);

            if (elapsed < frame_duration) {
                std::this_thread::sleep_for(frame_duration - elapsed);
            }
        }
    }

    this->on_thread_stopped();
}

void SystemBase::on_thread_started() {
    // 默认实现为空，派生类可覆盖
}

void SystemBase::on_thread_stopped() {
    // 默认实现为空，派生类可覆盖
}

// ========================================
// Private 方法
// ========================================

void SystemBase::set_context(ISystemContext* context) {
    context_ = context;
}

}  // namespace Corona::Kernel
