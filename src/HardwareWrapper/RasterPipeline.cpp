#include "HardwareWrapperVulkan/PipelineVulkan/RasterPipeline.h"

#include "Horizon.h"
#include <stdexcept>

template<class VS, class FS>
struct RasterPipeline<VS, FS>::Impl
{
    std::shared_ptr<RasterPipelineVulkan> backend;
    std::unique_ptr<EmbeddedShader::RasterPipelineObject> variants;
    EmbeddedShader::ShaderCodeCompiler::ConditionInfo vertexConditions, fragmentConditions;
    std::vector<EmbeddedShader::AutoBindEntry> autoBindEntries;
    size_t explicitTargetCount{}; // The last entries override reflected outputs.
    std::vector<ResourceUse> resourceUses;

    void refreshShaderVariant();
};

template<class VS, class FS>
RasterPipeline<VS, FS>::RasterPipeline()
    : impl_(std::make_unique<Impl>(std::make_shared<RasterPipelineVulkan>())) {}

template<class VS, class FS>
RasterPipeline<VS, FS>::RasterPipeline(const EmbeddedShader::ShaderCodeModule& vertex,
                                       const EmbeddedShader::ShaderCodeModule& fragment,
                                       uint32_t views)
    : impl_(std::make_unique<Impl>(std::make_shared<RasterPipelineVulkan>(vertex, fragment, views))) {}

template<class VS, class FS>
RasterPipeline<VS, FS>::RasterPipeline(EmbeddedShader::RasterPipelineObject shaders, uint32_t views)
    : impl_(std::make_unique<Impl>(std::make_shared<RasterPipelineVulkan>(*shaders.vertex, *shaders.fragment, views)))
{
    impl_->vertexConditions = shaders.vertex->getCurrentConditionInfo();
    impl_->fragmentConditions = shaders.fragment->getCurrentConditionInfo();
    impl_->autoBindEntries = std::move(shaders.autoBindEntries);
    if (!impl_->vertexConditions.empty() || !impl_->fragmentConditions.empty())
        impl_->variants = std::make_unique<EmbeddedShader::RasterPipelineObject>(std::move(shaders));
}

template<class VS, class FS>
RasterPipeline<VS, FS>::~RasterPipeline() = default;

template<class VS, class FS>
RasterPipeline<VS, FS>::RasterPipeline(RasterPipeline&&) noexcept = default;

template<class VS, class FS>
RasterPipeline<VS, FS>& RasterPipeline<VS, FS>::operator=(RasterPipeline&&) noexcept = default;

template<class VS, class FS>
auto RasterPipeline<VS, FS>::state() -> Impl&
{
    if (!impl_ || !impl_->backend) throw std::logic_error("Empty raster pipeline handle");
    return *impl_;
}

template<class VS, class FS>
RasterPipeline<VS, FS>& RasterPipeline<VS, FS>::uses(std::initializer_list<ResourceUse> resources)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->resourceUses.assign(resources);
    return *this;
}

template<class VS, class FS>
RasterPipeline<VS, FS>& RasterPipeline<VS, FS>::bindRenderTarget(uint32_t location, void** resource)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->autoBindEntries.push_back({resource, 0, 0,
        static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs), location});
    ++impl_->explicitTargetCount;
    return *this;
}

template<class VS, class FS>
void RasterPipeline<VS, FS>::setDepthImage(HardwareImage& depthImage)
{
    state().backend->setDepthImage(depthImage);
}

template<class VS, class FS>
void RasterPipeline<VS, FS>::setRasterState(RasterState rasterState)
{
    state().backend->setRasterState(rasterState);
}

template<class VS, class FS>
HardwareImage RasterPipeline<VS, FS>::getDepthImage()
{
    return state().backend->getDepthImage();
}

template<class VS, class FS>
void RasterPipeline<VS, FS>::setPushConstantDirect(uint64_t byteOffset, const void* data, size_t size, int32_t bindType)
{
    state().backend->setPushConstantDirect(byteOffset, data, size, bindType);
}

template<class VS, class FS>
void RasterPipeline<VS, FS>::setResourceDirect(uint64_t byteOffset, uint32_t typeSize,
                                               const HardwareBuffer& buffer, int32_t bindType)
{
    state().backend->setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

template<class VS, class FS>
void RasterPipeline<VS, FS>::setResourceDirect(uint64_t byteOffset, uint32_t typeSize,
                                               const HardwareImage& image, int32_t bindType, uint32_t location)
{
    state().backend->setResourceDirect(byteOffset, typeSize, image, bindType, location);
}

template<class VS, class FS>
HardwareExecutor::Command RasterPipeline<VS, FS>::operator()(
    const HardwareBuffer& vertices, const HardwareBuffer& indices, const HardwareBuffer& packets, uint32_t count,
    const HardwareBuffer& parameters, std::optional<ScissorRect> scissor,
    std::optional<RasterPass> pass, std::optional<ktm::uvec2> extent)
{
    if (!count && !pass) return HardwareExecutor::Command(nullptr);
    auto& pipeline = state();
    if (pipeline.variants) pipeline.refreshShaderVariant();
    auto& backend = *pipeline.backend;
    // Auto-bind: read current resource from each EDSL proxy's back-pointer
    for (const auto& entry : pipeline.autoBindEntries)
    {
        if (entry.boundValueRef)
        {
            backend.setPushConstantDirect(entry.byteOffset, entry.boundValueRef, entry.boundValueSize, entry.bindType);
        }
        else if (entry.boundResourceRef && *entry.boundResourceRef)
        {
            void* res = *entry.boundResourceRef;
            if (entry.boundResourceIsBuffer)
                backend.setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareBuffer*>(res), entry.bindType);
            else
                backend.setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareImage*>(res), entry.bindType, entry.location);
        }
    }
    auto draw = std::make_shared<HardwareExecutor::Command::Impl::Draw>(pipeline.backend, extent, pass, pipeline.resourceUses);
    draw->appendIndirect(vertices, indices, packets, count, parameters, scissor);
    if (!draw->drawCount && !pass) return HardwareExecutor::Command(nullptr);
    return HardwareExecutor::Command(std::move(draw));
}

template<class VS, class FS>
void RasterPipeline<VS, FS>::Impl::refreshShaderVariant()
{
    auto vertex = variants->vertex->getCurrentConditionInfo();
    auto fragment = variants->fragment->getCurrentConditionInfo();
    if (vertex == vertexConditions && fragment == fragmentConditions) return;
    auto next = backend->variant(variants->vertex->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, vertex),
                                 variants->fragment->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, fragment));
    variants->updateAutoBind(vertex, fragment);
    auto& bindings = variants->autoBindEntries;
    bindings.insert(bindings.end(), autoBindEntries.end() - explicitTargetCount, autoBindEntries.end());
    autoBindEntries = std::move(bindings);
    backend = std::move(next);
    vertexConditions = std::move(vertex);
    fragmentConditions = std::move(fragment);
}

template struct RasterPipeline<void, void>;
