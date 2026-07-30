#pragma once

#include <cstdint>

namespace Corona::Horizon
{
    // 全局帧环。环长 N 就是交换链图像数（DisplayManager 建链时发布），
    // 同一个 N 同时充当三件事：
    //   1. 交换链图像数 / acquire 信号量槽数；
    //   2. 每个管线 UBO 环的长度；
    //   3. 允许在飞的帧数预算。
    //
    // epoch 每完成一次 display 提交自增一次（present 成功或被跳过都算），
    // 自增紧跟在"等待新槽上一次占用者 GPU 完成"之后，所以下一帧 CPU 写
    // ring[slot] 时该槽必然已空闲——这正是 K = N 能成立的原因。
    //
    // 未建链时环长为 1，退化成逐帧串行（等价于改造前的行为）。

    [[nodiscard]] uint32_t frame_ring_size() noexcept;

    // 只增不减：交换链重建拿到更小的 N 时保持旧环长，避免已在飞的帧引用
    // 到被缩掉的槽。
    void publish_frame_ring_size(uint32_t size) noexcept;

    [[nodiscard]] uint64_t frame_ring_epoch() noexcept;

    // 当前帧应当使用的槽位 = epoch % frame_ring_size()。
    [[nodiscard]] uint32_t frame_ring_slot() noexcept;

    // 推进到下一帧，返回新的 epoch。
    uint64_t advance_frame_ring() noexcept;
}
