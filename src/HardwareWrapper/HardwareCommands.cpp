#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceCommand.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include <optional>

// ================= Buffer 到 Buffer 拷贝命令实现 =================
struct CopyCommand::Impl::BufferToBuffer final : CopyCommand::Impl
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
        uint64_t srcBufferID = srcBuffer.getBufferID();
        uint64_t dstBufferID = dstBuffer.getBufferID();

        if (srcBufferID == 0 || dstBufferID == 0)
        {
            return nullptr;
        }

        command.emplace(*legacyBufferFromId(srcBufferID),
            *legacyBufferFromId(dstBufferID), srcOffset, dstOffset, size);

        return &*command;
    }
};

// ================= Buffer 到 Image 拷贝命令实现 =================
struct CopyCommand::Impl::BufferToImage final : CopyCommand::Impl
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
        if (srcBuffer.getBufferID() == 0 || dstImage.getImageID() == 0)
        {
            return nullptr;
        }

        auto srcHandle = legacyBufferFromId(srcBuffer.getBufferID());
        auto dstHandle = legacyImageFromId(dstImage.getImageID());
        command.emplace(*srcHandle, *dstHandle, imageMip, imageLayer, bufferOffset, 1);

        return &*command;
    }
};

CopyCommand HardwareBuffer::copyTo(const HardwareBuffer& dst, uint64_t srcOffset, uint64_t dstOffset, uint64_t size) const
{
    auto copy = std::make_shared<CopyCommand::Impl::BufferToBuffer>(*this, dst);
    copy->srcOffset = srcOffset;
    copy->dstOffset = dstOffset;
    copy->size = size;
    return CopyCommand(std::move(copy));
}

CopyCommand HardwareBuffer::copyTo(const HardwareImage& dst, uint64_t bufferOffset, uint32_t imageLayer, uint32_t imageMip) const
{
    auto copy = std::make_shared<CopyCommand::Impl::BufferToImage>(*this, dst);
    copy->bufferOffset = bufferOffset;
    copy->imageLayer = imageLayer;
    copy->imageMip = imageMip;
    return CopyCommand(std::move(copy));
}

// ================= Image 到 Image 拷贝命令实现 =================
struct CopyCommand::Impl::ImageToImage final : CopyCommand::Impl
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
        uint64_t srcImageID = srcImage.getImageID();
        uint64_t dstImageID = dstImage.getImageID();

        if (srcImageID == 0 || dstImageID == 0)
        {
            return nullptr;
        }

        if (srcImageID == dstImageID)
            throw std::invalid_argument("A legacy image copy requires distinct images");

        command.emplace(*legacyImageFromId(srcImageID),
            *legacyImageFromId(dstImageID), srcLayer, dstLayer, srcMip, dstMip);

        return &*command;
    }
};

CopyCommand HardwareImage::copyTo(const HardwareImage& dst, uint32_t srcLayer, uint32_t dstLayer,
                                  uint32_t srcMip, uint32_t dstMip) const
{
    auto copy = std::make_shared<CopyCommand::Impl::ImageToImage>(*this, dst);
    copy->srcLayer = srcLayer;
    copy->dstLayer = dstLayer;
    copy->srcMip = srcMip;
    copy->dstMip = dstMip;
    return CopyCommand(std::move(copy));
}

// ================= Image 到 Buffer 拷贝命令实现 =================
struct CopyCommand::Impl::ImageToBuffer final : CopyCommand::Impl
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
        if (srcImage.getImageID() == 0 || dstBuffer.getBufferID() == 0)
        {
            return nullptr;
        }

        auto srcHandle = legacyImageFromId(srcImage.getImageID());
        auto dstHandle = legacyBufferFromId(dstBuffer.getBufferID());
        command.emplace(*srcHandle, *dstHandle, imageMip, imageLayer, bufferOffset, 1);

        return &*command;
    }
};

CopyCommand HardwareImage::copyTo(const HardwareBuffer& dst, uint32_t imageLayer, uint32_t imageMip, uint64_t bufferOffset) const
{
    auto copy = std::make_shared<CopyCommand::Impl::ImageToBuffer>(*this, dst);
    copy->imageLayer = imageLayer;
    copy->imageMip = imageMip;
    copy->bufferOffset = bufferOffset;
    return CopyCommand(std::move(copy));
}
