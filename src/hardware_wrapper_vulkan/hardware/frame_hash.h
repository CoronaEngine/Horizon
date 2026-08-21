#pragma once

#include <volk.h>

#include <cstdint>

// 逐帧输出哈希，用于证明优化前后渲染结果逐像素一致。
// 通过 HORIZON_FRAME_HASH=N 启用，N 为要抓取的帧序号（1 起）。
namespace Corona::Horizon::FrameHash
{
    [[nodiscard]] bool enabled() noexcept;

    // 注册可安全使用的设备句柄。须在 capture 之前调用一次。
    void set_device(VkDevice device, VkPhysicalDevice physical_device);

    [[nodiscard]] bool wants_capture() noexcept;

    // 在命令缓冲里追加一次 image→buffer 拷贝。image 必须已在 TRANSFER_SRC_OPTIMAL。
    void capture(VkCommandBuffer command_buffer,
                 VkImage image,
                 uint32_t width,
                 uint32_t height,
                 VkFormat format);

    void end_frame();

    void report();

    // HORIZON_FRAME_HASH_DUMP=<path> 时把最后一次抓取的原始像素写出，供逐像素比对。
}
