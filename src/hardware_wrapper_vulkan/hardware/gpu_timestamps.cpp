#include "hardware_wrapper_vulkan/hardware/gpu_timestamps.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Corona::Horizon::GpuTimes
{
    namespace
    {
        // 每帧最多 256 个 scope（512 个 timestamp）。超出后本帧多余的 scope 不计时，
        // 但 begin/end 配对仍然保持平衡（见 invalid_query 哨兵）。
        constexpr uint32_t max_queries = 512;
        constexpr uint32_t ring_size = 4;
        constexpr uint32_t invalid_query = ~0u;

        struct ScopeRecord
        {
            std::string label;
            uint32_t begin_query { invalid_query };
        };

        struct FrameSlot
        {
            VkQueryPool pool { VK_NULL_HANDLE };
            std::vector<ScopeRecord> scopes;
            uint32_t next_query { 0 };
            bool needs_reset { true };
        };

        struct Accumulator
        {
            double total_ms { 0.0 };
            uint64_t occurrences { 0 };
        };

        struct State
        {
            std::mutex mutex;
            VkDevice device { VK_NULL_HANDLE };
            double ns_per_tick { 1.0 };
            std::array<FrameSlot, ring_size> slots {};
            uint32_t write_slot { 0 };
            std::vector<uint32_t> open_scopes;
            std::map<std::string, Accumulator> totals;
            uint64_t collected_frames { 0 };
        };

        State& state()
        {
            static State instance;
            return instance;
        }
    }

    bool enabled() noexcept
    {
        static const bool on = [] {
            const char* value = std::getenv("HORIZON_GPU_TIMES");
            return value != nullptr && value[0] != '\0' && value[0] != '0';
        }();
        return on;
    }

    bool draws_enabled() noexcept
    {
        static const bool on = [] {
            const char* value = std::getenv("HORIZON_GPU_TIMES_DRAWS");
            return enabled() && value != nullptr && value[0] != '\0' && value[0] != '0';
        }();
        return on;
    }

    namespace
    {
        // 分级开关，用来二分定位崩溃：1=只建池+reset，2=加 begin/end timestamp，
        // 3=加回读。默认 3（全开）。
        [[nodiscard]] int stage() noexcept
        {
            static const int level = [] {
                if (const char* value = std::getenv("HORIZON_GPU_TIMES_STAGE"))
                {
                    const int parsed = std::atoi(value);
                    if (parsed > 0)
                    {
                        return parsed;
                    }
                }
                return 3;
            }();
            return level;
        }
    }

    void begin_command_buffer(const VkDevice device, const VkCommandBuffer command_buffer)
    {
        if (!enabled() || device == VK_NULL_HANDLE || command_buffer == VK_NULL_HANDLE)
        {
            return;
        }

        State& s = state();
        const std::lock_guard lock { s.mutex };

        if (s.device == VK_NULL_HANDLE)
        {
            s.device = device;
            // timestampPeriod 不从 physical device 取：这里拿不到可靠的句柄，而
            // HORIZON_GPU_TIMES_PERIOD 足够覆盖（NVIDIA 就是 1.0 ns/tick）。
            if (const char* period = std::getenv("HORIZON_GPU_TIMES_PERIOD"))
            {
                const double parsed = std::atof(period);
                if (parsed > 0.0)
                {
                    s.ns_per_tick = parsed;
                }
            }
        }

        if (s.device != device)
        {
            return;
        }

        FrameSlot& slot = s.slots[s.write_slot];
        if (slot.pool == VK_NULL_HANDLE)
        {
            VkQueryPoolCreateInfo info {};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = max_queries;
            if (vkCreateQueryPool(device, &info, nullptr, &slot.pool) != VK_SUCCESS)
            {
                slot.pool = VK_NULL_HANDLE;
                return;
            }
        }

        // 本槽的第一个命令缓冲负责整池 reset：timestamp 写入前必须 reset，
        // 而 vkCmdResetQueryPool 只能在命令缓冲里发，所以挂在这里。
        if (slot.needs_reset)
        {
            vkCmdResetQueryPool(command_buffer, slot.pool, 0, max_queries);
            slot.needs_reset = false;
            slot.next_query = 0;
            slot.scopes.clear();
            s.open_scopes.clear();
        }
    }

    void begin_scope(const VkCommandBuffer command_buffer, const std::string& label)
    {
        if (!enabled() || command_buffer == VK_NULL_HANDLE || stage() < 2)
        {
            return;
        }

        State& s = state();
        const std::lock_guard lock { s.mutex };

        FrameSlot& slot = s.slots[s.write_slot];

        // 池没建好或查询用尽时压入哨兵：end_scope 一定会 pop，配对不能错位。
        if (slot.pool == VK_NULL_HANDLE || slot.needs_reset || slot.next_query + 2 > max_queries)
        {
            s.open_scopes.push_back(invalid_query);
            return;
        }

        const uint32_t begin_query = slot.next_query;
        slot.next_query += 2;

        // 序号在这里生成而不是调用点：一帧可能有多个命令缓冲，调用点的计数器会
        // 每个缓冲从 0 重来，两个不同的 pass 会撞成同一个标签被平均掉。
        slot.scopes.push_back(ScopeRecord { "#" + std::to_string(slot.scopes.size()) + " " + label, begin_query });
        s.open_scopes.push_back(static_cast<uint32_t>(slot.scopes.size() - 1));

        vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.pool, begin_query);
    }

    void end_scope(const VkCommandBuffer command_buffer)
    {
        if (!enabled() || command_buffer == VK_NULL_HANDLE || stage() < 2)
        {
            return;
        }

        State& s = state();
        const std::lock_guard lock { s.mutex };

        if (s.open_scopes.empty())
        {
            return;
        }

        const uint32_t scope_index = s.open_scopes.back();
        s.open_scopes.pop_back();
        if (scope_index == invalid_query)
        {
            return;
        }

        FrameSlot& slot = s.slots[s.write_slot];
        if (slot.pool == VK_NULL_HANDLE || scope_index >= slot.scopes.size())
        {
            return;
        }

        vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, slot.pool,
                            slot.scopes[scope_index].begin_query + 1);
    }

    void collect_and_rotate()
    {
        if (!enabled())
        {
            return;
        }

        State& s = state();
        const std::lock_guard lock { s.mutex };

        if (s.device == VK_NULL_HANDLE)
        {
            return;
        }

        // 读 ring 上最旧的一槽：调用点在 gate 等待之后，这一帧的 GPU 工作已完成。
        const uint32_t read_slot = (s.write_slot + 1) % ring_size;
        FrameSlot& slot = s.slots[read_slot];

        // needs_reset 为真说明这一槽这轮没被写过，池里全是未初始化查询，读会挂住。
        if (stage() >= 3 && slot.pool != VK_NULL_HANDLE && !slot.needs_reset && slot.next_query > 0)
        {
            std::vector<uint64_t> results(slot.next_query, 0);
            const VkResult status =
                vkGetQueryPoolResults(s.device, slot.pool, 0, slot.next_query,
                                      results.size() * sizeof(uint64_t), results.data(), sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            if (status == VK_SUCCESS)
            {
                ++s.collected_frames;
                for (const ScopeRecord& scope : slot.scopes)
                {
                    const uint64_t begin = results[scope.begin_query];
                    const uint64_t end = results[scope.begin_query + 1];
                    if (end <= begin)
                    {
                        continue;
                    }
                    Accumulator& accumulator = s.totals[scope.label];
                    accumulator.total_ms += static_cast<double>(end - begin) * s.ns_per_tick / 1.0e6;
                    ++accumulator.occurrences;
                }
            }
        }

        slot.needs_reset = true;
        slot.next_query = 0;
        slot.scopes.clear();

        s.write_slot = read_slot;
        s.open_scopes.clear();
    }

    void report()
    {
        if (!enabled())
        {
            return;
        }

        State& s = state();
        const std::lock_guard lock { s.mutex };

        if (s.totals.empty() || s.collected_frames == 0)
        {
            std::printf("[gputimes] no samples\n");
            std::fflush(stdout);
            return;
        }

        const double frames = static_cast<double>(s.collected_frames);

        // 按"每帧成本"排序：一个 pass 每帧被调 8 次、单次 0.5ms，比每帧 1 次
        // 3ms 的 pass 更贵，用单次均值看会看反。
        struct Row
        {
            std::string label;
            double per_frame_ms { 0.0 };
            double per_call_ms { 0.0 };
            double calls_per_frame { 0.0 };
        };

        std::vector<Row> ranked;
        ranked.reserve(s.totals.size());
        for (const auto& [label, accumulator] : s.totals)
        {
            if (accumulator.occurrences == 0)
            {
                continue;
            }
            ranked.push_back(Row {
                label,
                accumulator.total_ms / frames,
                accumulator.total_ms / static_cast<double>(accumulator.occurrences),
                static_cast<double>(accumulator.occurrences) / frames,
            });
        }
        std::ranges::sort(ranked, [](const Row& a, const Row& b) { return a.per_frame_ms > b.per_frame_ms; });

        double sum = 0.0;
        for (const Row& row : ranked)
        {
            sum += row.per_frame_ms;
        }

        std::printf("[gputimes] frames=%llu gpu_total=%.3fms/frame scopes=%zu\n",
                    static_cast<unsigned long long>(s.collected_frames), sum, ranked.size());
        for (const Row& row : ranked)
        {
            std::printf("[gputimes]   %-34s %8.3fms/frame  %7.3fms/call  x%.1f\n",
                        row.label.c_str(), row.per_frame_ms, row.per_call_ms, row.calls_per_frame);
        }
        std::fflush(stdout);
    }
}
