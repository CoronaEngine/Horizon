#include "HardwareCommands.h"
#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceCommand.h"
#include "HardwareWrapperVulkan/ResourcePool.h"

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

    std::unique_ptr<CopyBufferCommand> command;

    BufferCopyCommandImpl(const HardwareBuffer &src, const HardwareBuffer &dst)
        : srcBuffer(src), dstBuffer(dst)
    {
    }

    CommandRecordVulkan *getCommandRecord() override
    {
        uint64_t srcBufferID = srcBuffer.getBufferID();
        uint64_t dstBufferID = dstBuffer.getBufferID();

        if (srcBufferID == 0 || dstBufferID == 0)
        {
            return nullptr;
        }

        // 按 ID 顺序获取锁以避免死锁
        if (srcBufferID < dstBufferID)
        {
            auto srcHandle = globalBufferStorages.acquire_write(srcBufferID);
            auto dstHandle = globalBufferStorages.acquire_write(dstBufferID);
            command = std::make_unique<CopyBufferCommand>(*srcHandle, *dstHandle);
        }
        else
        {
            auto dstHandle = globalBufferStorages.acquire_write(dstBufferID);
            auto srcHandle = globalBufferStorages.acquire_write(srcBufferID);
            command = std::make_unique<CopyBufferCommand>(*srcHandle, *dstHandle);
        }

        return command.get();
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

    std::unique_ptr<CopyBufferToImageCommand> command;

    BufferToImageCommandImpl(const HardwareBuffer &src, const HardwareImage &dst)
        : srcBuffer(src), dstImage(dst)
    {
    }

    CommandRecordVulkan *getCommandRecord() override
    {
        if (srcBuffer.getBufferID() == 0 || dstImage.getImageID() == 0)
        {
            return nullptr;
        }

        auto srcHandle = globalBufferStorages.acquire_write(srcBuffer.getBufferID());
        auto dstHandle = globalImageStorages.acquire_write(dstImage.getImageID());
        command = std::make_unique<CopyBufferToImageCommand>(*srcHandle, *dstHandle, imageMip);

        return command.get();
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

    std::unique_ptr<CopyImageCommand> command;

    ImageCopyCommandImpl(const HardwareImage &src, const HardwareImage &dst)
        : srcImage(src), dstImage(dst)
    {
    }

    CommandRecordVulkan *getCommandRecord() override
    {
        uint64_t srcImageID = srcImage.getImageID();
        uint64_t dstImageID = dstImage.getImageID();

        if (srcImageID == 0 || dstImageID == 0)
        {
            return nullptr;
        }

        // 按 ID 顺序获取锁以避免死锁
        if (srcImageID < dstImageID)
        {
            auto srcHandle = globalImageStorages.acquire_write(srcImageID);
            auto dstHandle = globalImageStorages.acquire_write(dstImageID);
            command = std::make_unique<CopyImageCommand>(*srcHandle, *dstHandle, srcLayer, dstLayer, srcMip, dstMip);
        }
        else
        {
            auto dstHandle = globalImageStorages.acquire_write(dstImageID);
            auto srcHandle = globalImageStorages.acquire_write(srcImageID);
            command = std::make_unique<CopyImageCommand>(*srcHandle, *dstHandle, srcLayer, dstLayer, srcMip, dstMip);
        }

        return command.get();
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

    std::unique_ptr<CopyImageToBufferCommand> command;

    ImageToBufferCommandImpl(const HardwareImage &src, const HardwareBuffer &dst)
        : srcImage(src), dstBuffer(dst)
    {
    }

    CommandRecordVulkan *getCommandRecord() override
    {
        if (srcImage.getImageID() == 0 || dstBuffer.getBufferID() == 0)
        {
            return nullptr;
        }

        auto srcHandle = globalImageStorages.acquire_write(srcImage.getImageID());
        auto dstHandle = globalBufferStorages.acquire_write(dstBuffer.getBufferID());
        command = std::make_unique<CopyImageToBufferCommand>(*srcHandle, *dstHandle);

        return command.get();
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
