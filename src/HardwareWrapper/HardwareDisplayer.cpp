#include "Horizon.h"
#include "HardwareWrapperVulkan/DisplayVulkan/DisplayManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include <stdexcept>

HardwareDisplayer::HardwareDisplayer(void* surface, PresentDevicePreference preference)
    : displaySurfaceID(reinterpret_cast<uintptr_t>(new DisplayManager(preference))), displaySurface(surface)
{
}

HardwareDisplayer::HardwareDisplayer(HardwareDisplayer&& other) noexcept
    : displaySurfaceID(std::exchange(other.displaySurfaceID, 0)),
      displaySurface(std::exchange(other.displaySurface, nullptr))
{
}

HardwareDisplayer::~HardwareDisplayer()
{
    delete reinterpret_cast<DisplayManager*>(displaySurfaceID);
}

HardwareDisplayer& HardwareDisplayer::operator=(HardwareDisplayer&& other) noexcept
{
    if (this == &other)
        return *this;

    delete reinterpret_cast<DisplayManager*>(displaySurfaceID);
    displaySurfaceID = std::exchange(other.displaySurfaceID, 0);
    displaySurface = std::exchange(other.displaySurface, nullptr);
    return *this;
}

HardwareDisplayer &HardwareDisplayer::wait(const HardwareExecutor &executor)
{
    const auto executorId = executor.getExecutorID();
    if (executorId && displaySurfaceID)
    {
        auto* display = reinterpret_cast<DisplayManager*>(displaySurfaceID);
        display->waitExecutor(*reinterpret_cast<HardwareExecutorVulkan*>(executorId));
    }
    return *this;
}

HardwareDisplayer &HardwareDisplayer::operator<<(const HardwareImage &image)
{
    if (displaySurfaceID && displaySurface)
        reinterpret_cast<DisplayManager*>(displaySurfaceID)->displayFrame(displaySurface, image);
    return *this;
}

bool HardwareDisplayer::present(HardwareExecutor& executor, const HardwareImage& image)
{
    auto* pending = reinterpret_cast<HardwareExecutorVulkan*>(executor.getExecutorID());
    if (!pending) return false;
    const bool shown = displaySurfaceID && displaySurface &&
        reinterpret_cast<DisplayManager*>(displaySurfaceID)->displayFrame(displaySurface, image, pending);
    if (!pending->commandList.empty()) pending->commit(); // Minimized, out of date or no surface.
    return shown;
}

bool HardwareDisplayer::present(std::span<const uint32_t> pixels, uint32_t width, uint32_t height, ImageFormat format)
{
    if (format != ImageFormat::RGBA8_UNORM && format != ImageFormat::RGBA8_SRGB)
        throw std::invalid_argument("Pixel presentation requires RGBA8_UNORM or RGBA8_SRGB");
    const uint64_t count = uint64_t(width) * height;
    if (count != pixels.size())
        throw std::invalid_argument("Pixel count does not match the presentation extent");
    if (count > UINT32_MAX / sizeof(uint32_t))
        throw std::length_error("Pixel presentation exceeds the supported upload size");
    if (!count || !displaySurfaceID) return false;

    auto& cache = reinterpret_cast<DisplayManager*>(displaySurfaceID)->pixelUpload;
    if (!cache) cache.emplace();
    auto& upload = *cache;
    // The CPU can overwrite staging after the producer has finished reading it.
    // The same producer carries the display's dependency on the previous image read.
    upload.executor.waitForDeferredResources();
    if (width != upload.width || height != upload.height || format != upload.format)
    {
        HardwareBuffer staging(static_cast<uint32_t>(pixels.size_bytes()), BufferUsage::StorageBuffer);
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
