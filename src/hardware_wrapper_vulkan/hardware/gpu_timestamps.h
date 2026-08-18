#pragma once

#include <string>

#include <volk.h>

// GPU 侧逐 pass 计时。HORIZON_GPU_TIMES=1 时，每个 rendering scope / dispatch 前后
// 各写一个 timestamp，帧末回读并按标签累计，跑分结束时打印。
//
// 仅诊断用：打开后帧时间本身会略有变化，不能与关闭时的数字直接比较。
namespace Corona::Horizon::GpuTimes
{
    [[nodiscard]] bool enabled() noexcept;

    // 逐 draw 计时的独立开关（HORIZON_GPU_TIMES_DRAWS=1）。draw 多的例子会
    // 打爆查询预算，所以不跟 enabled() 绑在一起。
    [[nodiscard]] bool draws_enabled() noexcept;

    // 由 encoder 在拿到命令缓冲之后调用：建池（首次）并 reset 本槽的查询。
    // 同一帧可能有多个命令缓冲，reset 只在本槽第一个缓冲上发一次。
    void begin_command_buffer(VkDevice device, VkCommandBuffer command_buffer);
    void begin_scope(VkCommandBuffer command_buffer, const std::string& label);
    void end_scope(VkCommandBuffer command_buffer);

    // 由 present 在 gate 等待之后调用：此时 ring 上最旧那帧的查询结果必定可读。
    void collect_and_rotate();

    // 跑分结束时打印累计的 per-label 均值。
    void report();
}
