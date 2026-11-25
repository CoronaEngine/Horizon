#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"

#include "Horizon.h"
#include <stdexcept>

RasterizerPipeline<>::RasterizerPipeline()
{
    rasterizerPipeline_ = std::make_shared<RasterizerPipelineVulkan>();
}

RasterizerPipeline<>::RasterizerPipeline(std::string vertex, std::string fragment,
                                         uint32_t views, const std::source_location& location)
    : rasterizerPipeline_(std::make_shared<RasterizerPipelineVulkan>(std::move(vertex), std::move(fragment), views, location))
{
}

RasterizerPipeline<>::RasterizerPipeline(const EmbeddedShader::ShaderCodeModule &vertexSpirV, const EmbeddedShader::ShaderCodeModule &fragmentSpirV, uint32_t multiviewCount, const std::source_location &src)
{
    rasterizerPipeline_ = std::make_shared<RasterizerPipelineVulkan>(vertexSpirV, fragmentSpirV, multiviewCount, src);
}

// 辅助函数：从已编译的 ShaderCodeCompiler 初始化 RasterizerPipeline
void RasterizerPipeline<>::initFromCompiler(EmbeddedShader::ShaderCodeCompiler& vertex,
                                            EmbeddedShader::ShaderCodeCompiler& fragment,
                                            uint32_t views, const std::source_location& src)
{
    rasterizerPipeline_ = std::make_shared<RasterizerPipelineVulkan>(vertex, fragment, views, src);
}

RasterizerPipeline<>::RasterizerPipeline(RasterizerPipeline<>&&) noexcept = default;
RasterizerPipeline<>::~RasterizerPipeline() = default;
RasterizerPipeline<>& RasterizerPipeline<>::operator=(RasterizerPipeline<>&&) noexcept = default;

RasterizerPipelineVulkan& RasterizerPipeline<>::backend() const
{
    if (!rasterizerPipeline_) throw std::logic_error("Empty raster pipeline handle");
    return *rasterizerPipeline_;
}

void RasterizerPipeline<>::setDepthImage(HardwareImage &depthImage)
{
    backend().setDepthImage(depthImage);
}

void RasterizerPipeline<>::setDepthEnabled(bool enabled)
{
    backend().setDepthEnabled(enabled);
}

void RasterizerPipeline<>::setRasterState(RasterState state)
{
    backend().setRasterState(state);
}

HardwareImage RasterizerPipeline<>::getDepthImage()
{
    return backend().getDepthImage();
}

void RasterizerPipeline<>::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    backend().setPushConstantDirect(byteOffset, data, size, bindType);
}

void RasterizerPipeline<>::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    backend().setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

void RasterizerPipeline<>::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location)
{
    backend().setResourceDirect(byteOffset, typeSize, image, bindType, location);
}

RasterBatch RasterizerPipeline<>::capture(std::optional<ktm::uvec2> extent, RasterPass pass)
{
    if (variants_) refreshShaderVariant();
    // Auto-bind: read current resource from each EDSL proxy's back-pointer
    for (const auto& entry : autoBindEntries_)
    {
        if (entry.boundValueRef)
        {
            setPushConstantDirect(entry.byteOffset, entry.boundValueRef, entry.boundValueSize, entry.bindType);
        }
        else if (entry.boundResourceRef && *entry.boundResourceRef)
        {
            void* res = *entry.boundResourceRef;
            if (entry.boundResourceIsBuffer)
                setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareBuffer*>(res), entry.bindType);
            else
                setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareImage*>(res), entry.bindType, entry.location);
        }
    }
    auto batch = backend().capture(extent, pass);
    return RasterBatch(std::move(batch), resourceUses_);
}

void RasterBatch::drawIndirect(const HardwareBuffer& vertices, const HardwareBuffer& indices,
                               const HardwareBuffer& packets, uint32_t count, size_t bytes,
                               const ParameterWriter& writeParameters, IndirectFirstInstance firstInstance,
                               std::optional<ScissorRect> scissor)
{
    if (!record_) throw std::logic_error("Raster batch already submitted");
    static_cast<RasterizerPipelineVulkan::Batch&>(*record_).appendIndirect(vertices, indices, packets, count,
                                                                          bytes, writeParameters, firstInstance, scissor);
}

void RasterizerPipeline<>::refreshShaderVariant()
{
    auto vertex = variants_->vertex->getCurrentConditionInfo();
    auto fragment = variants_->fragment->getCurrentConditionInfo();
    if (vertex == vertexConditions_ && fragment == fragmentConditions_) return;
    auto next = backend().variant(variants_->vertex->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, vertex),
                                  variants_->fragment->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, fragment));
    variants_->updateAutoBind(vertex, fragment);
    auto& bindings = variants_->autoBindEntries;
    bindings.insert(bindings.end(), autoBindEntries_.end() - explicitTargetCount_, autoBindEntries_.end());
    autoBindEntries_ = std::move(bindings);
    rasterizerPipeline_ = std::move(next);
    vertexConditions_ = std::move(vertex);
    fragmentConditions_ = std::move(fragment);
}
