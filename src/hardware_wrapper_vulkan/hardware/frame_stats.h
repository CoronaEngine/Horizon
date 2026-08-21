#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

// 逐帧命令计数。HORIZON_FRAME_STATS=1 时 present 打印一行并清零。
// 只用于定位性能问题；关闭时全部是 relaxed 自增，代价可忽略。
namespace Horizon::FrameStats
{
    [[nodiscard]] inline bool enabled() noexcept
    {
        static const bool on = [] {
            const char* v = std::getenv("HORIZON_FRAME_STATS");
            return v != nullptr && v[0] != '0';
        }();
        return on;
    }

    struct Counters
    {
        std::atomic<uint64_t> draws { 0 };
        std::atomic<uint64_t> draw_batches { 0 };
        std::atomic<uint64_t> instances { 0 };
        std::atomic<uint64_t> dispatches { 0 };
        std::atomic<uint64_t> render_passes { 0 };
        std::atomic<uint64_t> image_barriers { 0 };
        std::atomic<uint64_t> buffer_barriers { 0 };
        std::atomic<uint64_t> barrier_calls { 0 };
        std::atomic<uint64_t> pipeline_binds { 0 };
        std::atomic<uint64_t> descriptor_binds { 0 };
        std::atomic<uint64_t> copies { 0 };
        std::atomic<uint64_t> push_constants { 0 };
    };

    [[nodiscard]] inline Counters& counters() noexcept
    {
        static Counters instance;
        return instance;
    }

    inline void bump(std::atomic<uint64_t>& counter, uint64_t amount = 1) noexcept
    {
        if (enabled())
            counter.fetch_add(amount, std::memory_order_relaxed);
    }
}
