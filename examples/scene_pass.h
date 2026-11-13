#pragma once
#include "Horizon_old.h"
#include <span>

namespace example {
inline HardwareImageCreateInfo imageInfo(uint32_t width, uint32_t height, ImageFormat format, ImageUsage usage) {
    HardwareImageCreateInfo info(width, height, format);
    info.usage = usage;
    info.linearFiltering = true;
    return info;
}
inline HardwareImageCreateInfo cubeInfo(uint32_t size, ImageFormat format) {
    HardwareImageCreateInfo info(size, size, format);
    info.arrayLayers = 6;
    info.cube = true;
    info.linearFiltering = true;
    return info;
}
inline HardwareBuffer staging(std::span<const std::byte> data) {
    return HardwareBuffer(static_cast<uint32_t>(data.size()), BufferUsage::StorageBuffer, data.data());
}
struct PassState {
    RasterState raster{true, true, RasterCompare::LessEqual, RasterBlend::Alpha};
    RasterPass attachments{};
};

inline EmbeddedShader::CompilerOption shaderOptions() {
    EmbeddedShader::CompilerOption options;
    options.enableMatrixColumnMajor = true;
    return options;
}

struct NoDrawParameters {};

template<class Params, class VS = void, class FS = void>
class ScenePass : public RasterizerPipeline<VS, FS> {
public:
    template<class... Args>
    ScenePass(uint32_t width, uint32_t height, PassState state, Args&&... args)
        : RasterizerPipeline<VS, FS>(std::forward<Args>(args)...),
          width_(width), height_(height), attachments_(state.attachments) {
        this->setRasterState(state.raster);
    }
    Params parameters{};
    void draw(const RasterGeometry& geometry, std::span<const RasterDraw> ranges) {
        if (ranges.empty()) return;
        if constexpr (std::is_void_v<VS>) {
            this->syncShaderConditions();
            // Host EDSL values and texture proxies may change between materials.
            batches_.push_back(this->beginBatch(width_, height_, nextLoad()));
        } else if (batches_.empty()) {
            batches_.push_back(this->beginBatch(width_, height_, attachments_));
        }
        for (const auto& range : ranges) {
            if constexpr (std::is_same_v<Params, NoDrawParameters>)
                batches_.back().draw(geometry, range);
            else
                batches_.back().draw(geometry, range, parameters);
        }
    }
    void draw(const RasterGeometry& geometry) {
        const RasterDraw all{};
        draw(geometry, {&all, 1});
    }
    friend HardwareExecutor& operator<<(HardwareExecutor& executor, ScenePass& pass) {
        if (pass.batches_.empty())
            executor << pass.beginBatch(pass.width_, pass.height_, pass.attachments_);
        for (auto& batch : pass.batches_) executor << std::move(batch);
        pass.batches_.clear();
        return executor;
    }
private:
    RasterPass nextLoad() const {
        return batches_.empty() ? attachments_ : RasterPass{true, true};
    }
    uint32_t width_, height_;
    RasterPass attachments_;
    std::vector<RasterBatch> batches_;
};
}
