#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"

#include "Horizon.h"
#include <stdexcept>

using horizon::detail::RasterPipelineState;

RasterPipelineState::RasterPipelineState()
    : rasterizerPipeline_(std::make_shared<RasterizerPipelineVulkan>()) {}

RasterPipelineState::RasterPipelineState(std::string vertex, std::string fragment, uint32_t views,
                                         const std::source_location& location)
    : rasterizerPipeline_(std::make_shared<RasterizerPipelineVulkan>(std::move(vertex), std::move(fragment), views, location)) {}

RasterPipelineState::RasterPipelineState(const EmbeddedShader::ShaderCodeModule& vertex,
                                         const EmbeddedShader::ShaderCodeModule& fragment,
                                         uint32_t views, const std::source_location& location)
    : rasterizerPipeline_(std::make_shared<RasterizerPipelineVulkan>(vertex, fragment, views, location)) {}

RasterPipelineState::RasterPipelineState(EmbeddedShader::RasterizedPipelineObject shaders, uint32_t views,
                                         const std::source_location& location)
    : rasterizerPipeline_(std::make_shared<RasterizerPipelineVulkan>(*shaders.vertex, *shaders.fragment, views, location)),
      vertexConditions_(shaders.vertex->getCurrentConditionInfo()), fragmentConditions_(shaders.fragment->getCurrentConditionInfo()),
      autoBindEntries_(std::move(shaders.autoBindEntries))
{
    if (!vertexConditions_.empty() || !fragmentConditions_.empty())
        variants_ = std::make_unique<EmbeddedShader::RasterizedPipelineObject>(std::move(shaders));
}

RasterizerPipelineVulkan& RasterPipelineState::backend() const
{
    if (!rasterizerPipeline_) throw std::logic_error("Empty raster pipeline handle");
    return *rasterizerPipeline_;
}

void RasterPipelineState::setDepthImage(HardwareImage &depthImage)
{
    backend().setDepthImage(depthImage);
}

void RasterPipelineState::setDepthEnabled(bool enabled)
{
    backend().setDepthEnabled(enabled);
}

void RasterPipelineState::setRasterState(RasterState state)
{
    backend().setRasterState(state);
}

HardwareImage RasterPipelineState::getDepthImage()
{
    return backend().getDepthImage();
}

void RasterPipelineState::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    backend().setPushConstantDirect(byteOffset, data, size, bindType);
}

void RasterPipelineState::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    backend().setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

void RasterPipelineState::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location)
{
    backend().setResourceDirect(byteOffset, typeSize, image, bindType, location);
}

RasterBatch RasterPipelineState::capture(std::optional<ktm::uvec2> extent, RasterPass pass)
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
                               const HardwareBuffer& packets, uint32_t count, const HardwareBuffer& parameters,
                               std::optional<ScissorRect> scissor)
{
    if (!record_) throw std::logic_error("Raster batch already submitted");
    static_cast<RasterizerPipelineVulkan::Batch&>(*record_).appendIndirect(vertices, indices, packets, count,
                                                                          parameters, scissor);
}

void RasterPipelineState::refreshShaderVariant()
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
