#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceCommand.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include <optional>

// ================= Buffer 到 Buffer 拷贝命令实现 =================
struct HardwareExecutor::Command::Impl::BufferToBuffer final : HardwareExecutor::Command::Impl
{
    HardwareBuffer srcBuffer;
    HardwareBuffer dstBuffer;
    uint64_t srcOffset{0};
    uint64_t dstOffset{0};
    uint64_t size{0};

    std::optional<CopyBufferCommand> command;

    BufferToBuffer(const HardwareBuffer &src, const HardwareBuffer &dst)
        : srcBuffer(src), dstBuffer(dst)
    {
    }

    CommandRecordVulkan *getCommandRecord() override
    {
        if (command) return &*command;
        auto* src = ResourceManager::get(srcBuffer);
        auto* dst = ResourceManager::get(dstBuffer);
        if (!src || !dst) return nullptr;
        command.emplace(*src, *dst, srcOffset, dstOffset, size);

        return &*command;
    }
};

// ================= Buffer 到 Image 拷贝命令实现 =================
struct HardwareExecutor::Command::Impl::BufferToImage final : HardwareExecutor::Command::Impl
{
    HardwareBuffer srcBuffer;
    HardwareImage dstImage;
    uint64_t bufferOffset{0};
    uint32_t imageLayer{0};
    uint32_t imageMip{0};

    std::optional<CopyBufferToImageCommand> command;

    BufferToImage(const HardwareBuffer &src, const HardwareImage &dst)
        : srcBuffer(src), dstImage(dst)
    {
    }

    CommandRecordVulkan *getCommandRecord() override
    {
        if (command) return &*command;
        auto* src = ResourceManager::get(srcBuffer);
        auto* dst = ResourceManager::get(dstImage);
        if (!src || !dst) return nullptr;
        command.emplace(*src, *dst, imageMip, imageLayer, bufferOffset, 1);

        return &*command;
    }
};

HardwareExecutor::Command HardwareBuffer::copyTo(const HardwareBuffer& dst, uint64_t srcOffset,
                                                 uint64_t dstOffset, uint64_t size) const
{
    auto copy = std::make_shared<HardwareExecutor::Command::Impl::BufferToBuffer>(*this, dst);
    copy->srcOffset = srcOffset;
    copy->dstOffset = dstOffset;
    copy->size = size;
    return HardwareExecutor::Command(std::move(copy));
}

HardwareExecutor::Command HardwareBuffer::copyTo(const HardwareImage& dst, uint64_t bufferOffset,
                                                 uint32_t imageLayer, uint32_t imageMip) const
{
    auto copy = std::make_shared<HardwareExecutor::Command::Impl::BufferToImage>(*this, dst);
    copy->bufferOffset = bufferOffset;
    copy->imageLayer = imageLayer;
    copy->imageMip = imageMip;
    return HardwareExecutor::Command(std::move(copy));
}

// ================= Image 到 Image 拷贝命令实现 =================
struct HardwareExecutor::Command::Impl::ImageToImage final : HardwareExecutor::Command::Impl
{
    HardwareImage srcImage;
    HardwareImage dstImage;
    uint32_t srcLayer{0};
    uint32_t dstLayer{0};
    uint32_t srcMip{0};
    uint32_t dstMip{0};

    std::optional<CopyImageCommand> command;

    ImageToImage(const HardwareImage &src, const HardwareImage &dst)
        : srcImage(src), dstImage(dst)
    {
    }

    CommandRecordVulkan *getCommandRecord() override
    {
        if (command) return &*command;
        auto* src = ResourceManager::get(srcImage);
        auto* dst = ResourceManager::get(dstImage);
        if (!src || !dst) return nullptr;
        if (src == dst)
            throw std::invalid_argument("A legacy image copy requires distinct images");

        command.emplace(*src, *dst, srcLayer, dstLayer, srcMip, dstMip);

        return &*command;
    }
};

HardwareExecutor::Command HardwareImage::copyTo(const HardwareImage& dst, uint32_t srcLayer, uint32_t dstLayer,
                                                uint32_t srcMip, uint32_t dstMip) const
{
    auto copy = std::make_shared<HardwareExecutor::Command::Impl::ImageToImage>(*this, dst);
    copy->srcLayer = srcLayer;
    copy->dstLayer = dstLayer;
    copy->srcMip = srcMip;
    copy->dstMip = dstMip;
    return HardwareExecutor::Command(std::move(copy));
}

// ================= Image 到 Buffer 拷贝命令实现 =================
struct HardwareExecutor::Command::Impl::ImageToBuffer final : HardwareExecutor::Command::Impl
{
    HardwareImage srcImage;
    HardwareBuffer dstBuffer;
    uint32_t imageLayer{0};
    uint32_t imageMip{0};
    uint64_t bufferOffset{0};

    std::optional<CopyImageToBufferCommand> command;

    ImageToBuffer(const HardwareImage &src, const HardwareBuffer &dst)
        : srcImage(src), dstBuffer(dst)
    {
    }

    CommandRecordVulkan *getCommandRecord() override
    {
        if (command) return &*command;
        auto* src = ResourceManager::get(srcImage);
        auto* dst = ResourceManager::get(dstBuffer);
        if (!src || !dst) return nullptr;
        command.emplace(*src, *dst, imageMip, imageLayer, bufferOffset, 1);

        return &*command;
    }
};

HardwareExecutor::Command HardwareImage::copyTo(const HardwareBuffer& dst, uint32_t imageLayer,
                                                uint32_t imageMip, uint64_t bufferOffset) const
{
    auto copy = std::make_shared<HardwareExecutor::Command::Impl::ImageToBuffer>(*this, dst);
    copy->imageLayer = imageLayer;
    copy->imageMip = imageMip;
    copy->bufferOffset = bufferOffset;
    return HardwareExecutor::Command(std::move(copy));
}
