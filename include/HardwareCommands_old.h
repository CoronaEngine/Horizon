#pragma once

#include <cstdint>
#include <memory>

// Forward declarations
struct HardwareBuffer;
struct HardwareImage;
struct CopyCommandImpl;

// ================= 拷贝命令基类 =================
struct CopyCommand
{
    std::shared_ptr<CopyCommandImpl> impl;

    CopyCommand() = default;
    explicit CopyCommand(std::shared_ptr<CopyCommandImpl> implPtr) : impl(std::move(implPtr))
    {
    }

    explicit operator bool() const
    {
        return impl != nullptr;
    }
};

// ================= Buffer 到 Buffer 拷贝命令 =================
struct BufferCopyCommand : CopyCommand
{
    BufferCopyCommand() = default;
    BufferCopyCommand(const HardwareBuffer &src, const HardwareBuffer &dst,
                      uint64_t srcOffset = 0, uint64_t dstOffset = 0, uint64_t size = 0);
};

// ================= Buffer 到 Image 拷贝命令 =================
struct BufferToImageCommand : CopyCommand
{
    BufferToImageCommand() = default;
    BufferToImageCommand(const HardwareBuffer &src, const HardwareImage &dst,
                         uint64_t bufferOffset = 0, uint32_t imageLayer = 0, uint32_t imageMip = 0);
};

// ================= Image 到 Image 拷贝命令 =================
struct ImageCopyCommand : CopyCommand
{
    ImageCopyCommand() = default;
    ImageCopyCommand(const HardwareImage &src, const HardwareImage &dst,
                     uint32_t srcLayer = 0, uint32_t dstLayer = 0,
                     uint32_t srcMip = 0, uint32_t dstMip = 0);
};

// ================= Image 到 Buffer 拷贝命令 =================
struct ImageToBufferCommand : CopyCommand
{
    ImageToBufferCommand() = default;
    ImageToBufferCommand(const HardwareImage &src, const HardwareBuffer &dst,
                         uint32_t imageLayer = 0, uint32_t imageMip = 0, uint64_t bufferOffset = 0);
};