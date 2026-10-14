#include "Horizon_old.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/ResourcePool.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#ifndef HORIZON_LEGACY_API
#error "This test must be linked to the legacy Horizon target."
#endif

namespace
{
void expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void test_cpu()
{
    HardwareBuffer empty;
    HardwareImage image;
    HardwareDisplayer displayer;
    HardwareDisplayer displayCopy(displayer);
    expect(displayCopy.getDisplayerID() == displayer.getDisplayerID(), "displayer copy shares ownership");
    expect(!empty && !image, "default resources must be empty");
    auto command = empty.copyTo(empty);
    expect(command.impl->getCommandRecord() == nullptr, "empty copy must not record GPU work");

    const std::array<uint32_t, 4> values{11, 22, 33, 44};
    HardwarePushConstant original(values);
    expect(original.getSize() == sizeof(values), "typed push constant size");
    expect(std::memcmp(original.getData(), values.data(), sizeof(values)) == 0, "typed push constant bytes");
    {
        HardwarePushConstant alias(original);
        alias = original; // Two wrappers for the same slot must not deadlock.
        expect(alias.getPushConstantID() == original.getPushConstantID(), "copy shares ownership");
        HardwarePushConstant moved(std::move(alias));
        expect(alias.getSize() == 0 && alias.getData() == nullptr, "moved-from push constant is empty");
        expect(moved.getSize() == sizeof(values), "move retains data");
        moved = uint32_t{99};
        expect(moved.getSize() == sizeof(uint32_t), "typed assignment replaces storage");
    }
    expect(std::memcmp(original.getData(), values.data(), sizeof(values)) == 0, "shared source survives replacement");
    // Reuse storage slots repeatedly to catch stale refcounts and double frees.
    for (unsigned i = 0; i < 300; ++i)
    {
        HardwarePushConstant value(i);
        HardwarePushConstant copy;
        copy = value;
        expect(std::memcmp(copy.getData(), &i, sizeof(i)) == 0, "reused push constant slot");
    }
}

bool has_vulkan_14_device()
{
    if (volkInitialize() != VK_SUCCESS || volkGetInstanceVersion() < VK_API_VERSION_1_4)
        return false;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_4;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    coronaHardwareCheck(vkCreateInstance(&create, nullptr, &instance));
    volkLoadInstance(instance);
    uint32_t count = 0;
    coronaHardwareCheck(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count);
    coronaHardwareCheck(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    bool supported = false;
    for (auto device : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        supported |= properties.apiVersion >= VK_API_VERSION_1_4;
    }
    vkDestroyInstance(instance, nullptr);
    return supported;
}

void test_gpu()
{
    std::cerr << "GPU: initializing buffers\n";
    const std::array<uint32_t, 8> input{10, 20, 30, 40, 50, 60, 70, 80};
    const std::array<uint32_t, 8> zeros{};
    HardwareBuffer source(input, BufferUsage::StorageBuffer);
    HardwareBuffer destination(zeros, BufferUsage::StorageBuffer);
    HardwareExecutor executor;
    std::cerr << "GPU: buffer copy/readback\n";
    executor << source.copyTo(destination);
    executor.commit().waitForDeferredResources();
    std::array<uint32_t, 8> output{};
    expect(destination.copyToData(output.data(), sizeof(output)), "buffer readback");
    expect(output == input, "GPU buffer round trip");

    // Temporary source wrapper must remain alive until its queued copy completes.
    std::cerr << "GPU: deferred resource lifetime\n";
    executor << HardwareBuffer(input, BufferUsage::StorageBuffer).copyTo(destination);
    executor.commit().waitForDeferredResources();
    expect(destination.copyToData(output.data(), sizeof(output)) && output == input,
           "deferred copy retains temporary resources");

    expect(destination.copyFromData(zeros.data(), sizeof(zeros)), "clear destination");
    executor << source.copyTo(destination, sizeof(uint32_t), 3 * sizeof(uint32_t), 2 * sizeof(uint32_t));
    executor.commit().waitForDeferredResources();
    expect(destination.copyToData(output.data(), sizeof(output)), "partial copy readback");
    expect(output == std::array<uint32_t, 8>{0, 0, 0, 20, 30, 0, 0, 0}, "copy preserves byte offsets and size");

    executor << destination.copyTo(destination, 3 * sizeof(uint32_t), 0, 2 * sizeof(uint32_t));
    executor.commit().waitForDeferredResources();
    expect(destination.copyToData(output.data(), sizeof(output)), "self copy readback");
    expect(output == std::array<uint32_t, 8>{20, 30, 0, 20, 30, 0, 0, 0}, "disjoint self copy must not deadlock");
    bool rejected = false;
    try { executor << source.copyTo(destination, 0, sizeof(output), sizeof(uint32_t)); }
    catch (const std::out_of_range&) { rejected = true; }
    expect(rejected, "out of bounds copy must be rejected before recording");

    std::cerr << "GPU: image mip/layer round trip\n";
    HardwareImageCreateInfo imageInfo(4, 4, ImageFormat::RGBA8_UINT);
    imageInfo.arrayLayers = 2;
    imageInfo.mipLevels = 2;
    HardwareImage image(imageInfo);
    HardwareImage imageCopy(imageInfo);
    {
        HardwareImage owner(imageInfo);
        auto view = owner[1];
        owner = HardwareImage{};
        expect(static_cast<bool>(view), "image view retains its allocation after owner destruction");
    }
    expect(destination.copyFromData(zeros.data(), sizeof(zeros)), "clear image destination");
    executor << source.copyTo(image, sizeof(uint32_t), 1, 1);
    executor << image.copyTo(imageCopy, 1, 0, 1, 1);
    executor << imageCopy.copyTo(destination, 0, 1, 2 * sizeof(uint32_t));
    executor.commit().waitForDeferredResources();
    expect(destination.copyToData(output.data(), sizeof(output)), "image readback");
    expect(output == std::array<uint32_t, 8>{0, 0, 20, 30, 40, 50, 0, 0},
           "image copies preserve layers, mips and buffer offsets");

    EmbeddedShader::CompilerOption options;
    std::cerr << "GPU: Slang compilation/compute dispatch\n";
    options.compileGLSL = false;
    options.compileHLSL = false;
    EmbeddedShader::ShaderCodeCompiler compiler(
        "[shader(\"compute\")] [numthreads(1,1,1)] void main() {}",
        EmbeddedShader::ShaderStage::ComputeShader, EmbeddedShader::ShaderLanguage::Slang, options);
    auto module = compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV);
    expect(!std::get<std::vector<uint32_t>>(module.shaderCode).empty(), "current Slang emits SPIR-V");
    ComputePipelineBase pipeline(module);
    executor << pipeline(1, 1, 1);
    executor.commit();

    std::cerr << "GPU: EDSL compute\n";
    ComputePipelineBase edsl([] {}, ktm::uvec3(1), options);
    executor << edsl(1, 1, 1);
    executor.commit();

    EmbeddedShader::SlangModuleCompileArgs moduleArgs;
    std::cerr << "GPU: generated Slang module\n";
    moduleArgs.moduleName = "legacy_test";
    moduleArgs.sourceLanguage = EmbeddedShader::ShaderLanguage::Slang;
    moduleArgs.shaderCode = "[shader(\"compute\")] [numthreads(1,1,1)] void main() {}";
    auto slangModule = EmbeddedShader::ShaderLanguageConverter::slangModuleCompiler(moduleArgs);
    ComputePipelineBase generated(compileLegacyShaderModule(EmbeddedShader::ShaderStage::ComputeShader, slangModule));
    executor << generated(1, 1, 1);
    executor.commit();
    coronaHardwareCheck(vkDeviceWaitIdle(getHardwareContext().getMainDevice()->deviceManager.getLogicalDevice()));
}
}

int main(int argc, char** argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    // Report native assertions to CTest instead of opening an unattended dialog.
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--gpu")
        {
            if (!has_vulkan_14_device())
            {
                std::cout << "SKIP: no Vulkan 1.4 loader/device\n";
                return 77;
            }
            test_gpu();
        }
        else
            test_cpu();
        std::cout << "Legacy API checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
