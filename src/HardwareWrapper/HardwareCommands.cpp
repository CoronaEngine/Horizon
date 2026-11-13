#include "HardwareCommands_old.h"
#include "Horizon_old.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceCommand.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include <optional>

// ================= CopyCommandImpl 基类 =================
// CopyCommandImpl definition moved to HardwareExecutorVulkan.h to allow shared usage
// struct CopyCommandImpl
// {
//    virtual ~CopyCommandImpl() = default;
//    virtual CommandRecordVulkan *getCommandRecord() = 0;
// };

// ================= Buffer 到 Buffer 拷贝命令实现 =================
struct BufferCopyCommandImpl : CopyCommandImpl
{
    HardwareBuffer srcBuffer;
    HardwareBuffer dstBuffer;
    uint64_t srcOffset{0};
    uint64_t dstOffset{0};
    uint64_t size{0};

    std::optional<CopyBufferCommand> command;

    BufferCopyCommandImpl(const HardwareBuffer &src, const HardwareBuffer &dst)
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
struct BufferToImageCommandImpl : CopyCommandImpl
{
    HardwareBuffer srcBuffer;
    HardwareImage dstImage;
    uint64_t bufferOffset{0};
    uint32_t imageLayer{0};
    uint32_t imageMip{0};

    std::optional<CopyBufferToImageCommand> command;

    BufferToImageCommandImpl(const HardwareBuffer &src, const HardwareImage &dst)
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

// ================= BufferCopyCommand 构造函数 =================
BufferCopyCommand::BufferCopyCommand(const HardwareBuffer &src, const HardwareBuffer &dst,
                                     uint64_t srcOffset, uint64_t dstOffset, uint64_t size)
{
    auto implPtr = std::make_shared<BufferCopyCommandImpl>(src, dst);
    implPtr->srcOffset = srcOffset;
    implPtr->dstOffset = dstOffset;
    implPtr->size = size;
    impl = implPtr;
}

// ================= BufferToImageCommand 构造函数 =================
BufferToImageCommand::BufferToImageCommand(const HardwareBuffer &src, const HardwareImage &dst,
                                           uint64_t bufferOffset, uint32_t imageLayer, uint32_t imageMip)
{
    auto implPtr = std::make_shared<BufferToImageCommandImpl>(src, dst);
    implPtr->bufferOffset = bufferOffset;
    implPtr->imageLayer = imageLayer;
    implPtr->imageMip = imageMip;
    impl = implPtr;
}

// ================= Image 到 Image 拷贝命令实现 =================
struct ImageCopyCommandImpl : CopyCommandImpl
{
    HardwareImage srcImage;
    HardwareImage dstImage;
    uint32_t srcLayer{0};
    uint32_t dstLayer{0};
    uint32_t srcMip{0};
    uint32_t dstMip{0};

    std::optional<CopyImageCommand> command;

    ImageCopyCommandImpl(const HardwareImage &src, const HardwareImage &dst)
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

// ================= ImageCopyCommand 构造函数 =================
ImageCopyCommand::ImageCopyCommand(const HardwareImage &src, const HardwareImage &dst,
                                   uint32_t srcLayer, uint32_t dstLayer,
                                   uint32_t srcMip, uint32_t dstMip)
{
    auto implPtr = std::make_shared<ImageCopyCommandImpl>(src, dst);
    implPtr->srcLayer = srcLayer;
    implPtr->dstLayer = dstLayer;
    implPtr->srcMip = srcMip;
    implPtr->dstMip = dstMip;
    impl = implPtr;
}

// ================= Image 到 Buffer 拷贝命令实现 =================
struct ImageToBufferCommandImpl : CopyCommandImpl
{
    HardwareImage srcImage;
    HardwareBuffer dstBuffer;
    uint32_t imageLayer{0};
    uint32_t imageMip{0};
    uint64_t bufferOffset{0};

    std::optional<CopyImageToBufferCommand> command;

    ImageToBufferCommandImpl(const HardwareImage &src, const HardwareBuffer &dst)
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

// ================= ImageToBufferCommand 构造函数 =================
ImageToBufferCommand::ImageToBufferCommand(const HardwareImage &src, const HardwareBuffer &dst,
                                           uint32_t imageLayer, uint32_t imageMip, uint64_t bufferOffset)
{
    auto implPtr = std::make_shared<ImageToBufferCommandImpl>(src, dst);
    implPtr->imageLayer = imageLayer;
    implPtr->imageMip = imageMip;
    implPtr->bufferOffset = bufferOffset;
    impl = implPtr;
}
