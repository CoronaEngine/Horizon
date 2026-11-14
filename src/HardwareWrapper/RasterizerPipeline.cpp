#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"

#include "Horizon_old.h"
#include <stdexcept>

RasterizerPipelineBase::RasterizerPipelineBase()
{
    rasterizerPipeline_ = std::make_shared<RasterizerPipelineVulkan>();
}

RasterizerPipelineBase::RasterizerPipelineBase(std::string vs, std::string fs, uint32_t multiviewCount, EmbeddedShader::ShaderLanguage vlang, EmbeddedShader::ShaderLanguage flang, const std::source_location &src)
{
    rasterizerPipeline_ = std::make_shared<RasterizerPipelineVulkan>(vs, fs, multiviewCount, vlang, flang, src);
}

RasterizerPipelineBase::RasterizerPipelineBase(const EmbeddedShader::ShaderCodeModule &vertexSpirV, const EmbeddedShader::ShaderCodeModule &fragmentSpirV, uint32_t multiviewCount, const std::source_location &src)
{
    rasterizerPipeline_ = std::make_shared<RasterizerPipelineVulkan>(vertexSpirV, fragmentSpirV, multiviewCount, src);
}

// 辅助函数：从已编译的 ShaderCodeCompiler 初始化 RasterizerPipeline
void rasterizerPipelineInitFromCompiler(std::shared_ptr<RasterizerPipelineVulkan>& pipeline,
                                         EmbeddedShader::ShaderCodeCompiler &vertexCompiler,
                                         EmbeddedShader::ShaderCodeCompiler &fragmentCompiler,
                                         uint32_t multiviewCount,
                                         const std::source_location &src)
{
    pipeline = std::make_shared<RasterizerPipelineVulkan>(vertexCompiler, fragmentCompiler, multiviewCount, src);
}

RasterizerPipelineBase::RasterizerPipelineBase(RasterizerPipelineBase&&) noexcept = default;
RasterizerPipelineBase::~RasterizerPipelineBase() = default;
RasterizerPipelineBase& RasterizerPipelineBase::operator=(RasterizerPipelineBase&&) noexcept = default;

RasterizerPipelineVulkan& RasterizerPipelineBase::backend() const
{
    if (!rasterizerPipeline_) throw std::logic_error("Empty raster pipeline handle");
    return *rasterizerPipeline_;
}

void RasterizerPipelineBase::setDepthImage(HardwareImage &depthImage)
{
    backend().setDepthImage(depthImage);
}

void RasterizerPipelineBase::setDepthEnabled(bool enabled)
{
    backend().setDepthEnabled(enabled);
}

void RasterizerPipelineBase::setRasterState(RasterState state)
{
    backend().setRasterState(state);
}

HardwareImage RasterizerPipelineBase::getDepthImage()
{
    return backend().getDepthImage();
}

void RasterizerPipelineBase::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    backend().setPushConstantDirect(byteOffset, data, size, bindType);
}

void RasterizerPipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    backend().setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

void RasterizerPipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location)
{
    backend().setResourceDirect(byteOffset, typeSize, image, bindType, location);
}

RasterBatch RasterizerPipelineBase::beginBatch(uint16_t width, uint16_t height, RasterPass pass)
{
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
    auto batch = backend().capture(width, height, pass);
    return RasterBatch(std::move(batch), resourceUses_);
}

void RasterBatch::append(const RasterGeometry& geometry, const RasterDraw& range, const void* data, size_t size)
{
    if (!record_) throw std::logic_error("Raster batch already submitted");
    static_cast<RasterizerPipelineVulkan::Batch&>(*record_).append(geometry, range, data, size);
}

void RasterBatch::drawIndirect(const RasterGeometry& geometry, const HardwareBuffer& packets, uint32_t count,
                               IndirectFirstInstance firstInstance)
{
    appendIndirect(geometry, packets, count, nullptr, 0, firstInstance);
}

void RasterBatch::appendIndirect(const RasterGeometry& geometry, const HardwareBuffer& packets, uint32_t count,
                                 const void* data, size_t size, IndirectFirstInstance firstInstance)
{
    if (!record_) throw std::logic_error("Raster batch already submitted");
    static_cast<RasterizerPipelineVulkan::Batch&>(*record_).appendIndirect(geometry, packets, count, data, size, firstInstance);
}

bool RasterizerPipelineBase::syncShaderConditions()
{
    if (!variants_) return false;
    auto vertex = variants_->vertex->getCurrentConditionInfo();
    auto fragment = variants_->fragment->getCurrentConditionInfo();
    if (vertex == vertexConditions_ && fragment == fragmentConditions_) return false;
    auto next = backend().variant(*variants_->vertex, *variants_->fragment);
    variants_->updateAutoBind(vertex, fragment);
    autoBindEntries_ = variants_->autoBindEntries;
    rasterizerPipeline_ = std::move(next);
    vertexConditions_ = std::move(vertex);
    fragmentConditions_ = std::move(fragment);
    return true;
}
