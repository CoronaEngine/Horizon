//
// Created by Zero on 2023/8/23.
//

#pragma once

#include "core/stl.h"
#include "math/basic_types.h"
#include "core/image/image.h"
#include "core/header.h"
#include "core/concurrency/thread_safety.h"

namespace ocarina {

class OC_RHI_API MemoryStats : public thread_safety<> {
    struct BufferData {
        string name;
        size_t size;
    };
    struct TexData {
        string name;
        uint3 res;
        PixelStorage storage;
        [[nodiscard]] size_t size() const noexcept {
            return res.x * res.y * res.z * pixel_size(storage);
        }
    };

    OC_MAKE_INSTANCE_CONSTRUCTOR(MemoryStats, s_stats);

private:
    size_t buffer_size_{};
    size_t tex_size_{};
    map<handle_ty, BufferData> buffer_map_;
    map<handle_ty, TexData> tex_map_;

private:
    [[nodiscard]] size_t buffer_size(handle_ty handle) const noexcept {
        OC_ASSERT(buffer_map_.contains(handle));
        return buffer_map_.at(handle).size;
    }

    [[nodiscard]] uint3 tex_res(handle_ty handle) const noexcept {
        OC_ASSERT(tex_map_.contains(handle));
        return tex_map_.at(handle).res;
    }

    [[nodiscard]] size_t tex_size(handle_ty handle) const noexcept {
        OC_ASSERT(tex_map_.contains(handle));
        TexData data = tex_map_.at(handle);
        return data.size();
    }

public:
    OC_MAKE_INSTANCE_FUNC_DECL(MemoryStats)
    OC_MAKE_MEMBER_GETTER(buffer_size, )
    OC_MAKE_MEMBER_GETTER(tex_size, )
    void on_buffer_allocate(handle_ty handle, size_t size, string name = "");
    void on_buffer_free(handle_ty handle);
    void foreach_buffer_info(const std::function<void(BufferData)> &func) const noexcept;
    void on_tex_allocate(handle_ty handle, uint3 res, PixelStorage storage, string name = "");
    void on_tex_free(handle_ty handle);
    void foreach_tex_info(const std::function<void(TexData)> &func) const noexcept;
    [[nodiscard]] string total_buffer_info() const noexcept;
    [[nodiscard]] string buffer_detail_info() const noexcept;
    [[nodiscard]] string buffer_info() const noexcept;
    [[nodiscard]] string total_tex_info() const noexcept;
    [[nodiscard]] string tex_detail_info() const noexcept;
    [[nodiscard]] string tex_info() const noexcept;
};

}// namespace ocarina