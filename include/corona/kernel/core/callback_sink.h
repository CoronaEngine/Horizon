#pragma once

#include "quill/sinks/Sink.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Corona::Kernel {

/**
 * @brief 日志条目结构体
 *
 * 每条 LogEntry 是一个值类型，被拷贝进队列后与 Quill 内部的缓冲区完全解耦。
 */
struct LogEntry {
    std::string level;    ///< 日志级别: "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"
    std::string message;  ///< 已格式化的完整日志文本
    uint64_t timestamp;   ///< 时间戳（纳秒，epoch）
};

/**
 * @brief 自定义 Quill Sink，将格式化后的日志条目推入线程安全队列。
 *
 * 消费者（如 Python 线程）通过 drain() 主动拉取，彻底避免 GIL 死锁风险。
 *
 * 设计要点：
 * - write_log() 中只做 string 拷贝 + mutex 锁队列，开销极小（~100ns 级）
 * - kMaxQueueSize 防止消费者长时间不拉取导致内存膨胀
 * - 使用 std::vector 而非 std::queue，方便 drain() 时一次性 swap 清空
 *
 * 线程安全保证：
 * - write_log() 由 Quill Backend 线程调用
 * - drain() / empty() / size() 由消费者线程调用
 * - 所有操作通过 queue_mutex_ 互斥保护
 */
class CallbackSink : public quill::Sink {
   public:
    /// 队列最大容量，超出后丢弃最旧条目
    static constexpr size_t kMaxQueueSize = 10000;

    // ========== Quill Sink 接口 (由 Quill Backend 线程调用) ==========

    /**
     * @brief 接收格式化后的日志并推入队列
     *
     * 由 Quill Backend 线程调用，不会触碰 Python GIL。
     */
    void write_log(quill::MacroMetadata const* log_metadata, uint64_t log_timestamp,
                   std::string_view thread_id, std::string_view thread_name,
                   std::string const& process_id, std::string_view logger_name,
                   quill::LogLevel log_level, std::string_view log_level_description,
                   std::string_view log_level_short_code,
                   std::vector<std::pair<std::string, std::string>> const* named_args,
                   std::string_view log_message, std::string_view log_statement) override {
        LogEntry entry;
        entry.level = std::string(log_level_description);
        entry.message = std::string(log_statement);  // 完整格式化后的日志行
        entry.timestamp = log_timestamp;

        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.size() >= kMaxQueueSize) {
            queue_.erase(queue_.begin());  // 丢弃最旧条目
        }
        queue_.push_back(std::move(entry));
    }

    /**
     * @brief Flush 操作 (no-op)
     *
     * 队列不需要 flush，消费者通过 drain() 主动拉取。
     */
    void flush_sink() override { /* no-op: 队列不需要 flush */ }

    // ========== 公共 API (由消费者线程调用) ==========

    /**
     * @brief 拉取并清空所有待处理的日志条目
     * @return 自上次 drain 以来的所有 LogEntry
     */
    std::vector<LogEntry> drain() {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::vector<LogEntry> result;
        result.swap(queue_);
        return result;
    }

    /**
     * @brief 查询队列是否为空
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return queue_.empty();
    }

    /**
     * @brief 获取当前队列大小
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return queue_.size();
    }

   private:
    mutable std::mutex queue_mutex_;
    std::vector<LogEntry> queue_;
};

}  // namespace Corona::Kernel
