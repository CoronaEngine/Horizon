#include "Horizon.h"
#include "HardwareWrapperVulkan/DisplayVulkan/DisplayManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include <stdexcept>

struct HardwareDisplayer::Impl
{
    Impl(void* surface, PresentDevicePreference preference) : manager(preference), surface(surface) {}

    DisplayManager manager;
    void* surface;
};

HardwareDisplayer::HardwareDisplayer(void* surface, PresentDevicePreference preference)
    : impl_(std::make_unique<Impl>(surface, preference))
{
}

HardwareDisplayer::HardwareDisplayer(HardwareDisplayer&&) noexcept = default;
HardwareDisplayer::~HardwareDisplayer() = default;
HardwareDisplayer& HardwareDisplayer::operator=(HardwareDisplayer&&) noexcept = default;

HardwareDisplayer &HardwareDisplayer::wait(const HardwareExecutor &executor)
{
    if (impl_ && executor.impl_)
        impl_->manager.waitExecutor(executor.impl_->backend);
    return *this;
}

HardwareDisplayer &HardwareDisplayer::operator<<(const HardwareImage &image)
{
    if (impl_ && impl_->surface)
        impl_->manager.displayFrame(impl_->surface, image);
    return *this;
}

bool HardwareDisplayer::present(HardwareExecutor& executor, const HardwareImage& image)
{
    if (!executor.impl_) return false;
    auto& pending = executor.impl_->backend;
    const bool shown = impl_ && impl_->surface && impl_->manager.displayFrame(impl_->surface, image, &pending);
    if (!pending.commandList.empty()) pending.commit(); // Minimized, out of date or no surface.
    return shown;
}

bool HardwareDisplayer::present(std::span<const uint32_t> pixels, uint32_t width, uint32_t height, HardwareImage::Format format)
{
    if (format != HardwareImage::Format::RGBA8_UNORM && format != HardwareImage::Format::RGBA8_SRGB)
        throw std::invalid_argument("Pixel presentation requires RGBA8_UNORM or RGBA8_SRGB");
    const uint64_t count = uint64_t(width) * height;
    if (count != pixels.size())
        throw std::invalid_argument("Pixel count does not match the presentation extent");
    if (count > UINT32_MAX / sizeof(uint32_t))
        throw std::length_error("Pixel presentation exceeds the supported upload size");
    if (!count || !impl_) return false;

    auto& cache = impl_->manager.pixelUpload;
    if (!cache) cache.emplace();
    auto& upload = *cache;
    // The CPU can overwrite staging after the producer has finished reading it.
    // The same producer carries the display's dependency on the previous image read.
    upload.executor.wait();
    if (width != upload.width || height != upload.height || format != upload.format)
    {
        HardwareBuffer staging(static_cast<uint32_t>(pixels.size_bytes()), HardwareBuffer::Usage::StorageBuffer);
        HardwareImage image(width, height, format);
        upload.staging = std::move(staging);
        upload.image = std::move(image);
        upload.width = width;
        upload.height = height;
        upload.format = format;
    }
    if (!upload.staging.copyFromData(pixels.data(), pixels.size_bytes()))
        throw std::runtime_error("Could not upload presentation pixels");
    upload.executor << upload.staging.copyTo(upload.image);
    return present(upload.executor, upload.image);
}
