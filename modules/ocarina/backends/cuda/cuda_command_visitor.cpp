//
// Created by zero on 2022/7/9.
//

#include "cuda_command_visitor.h"
#include "util.h"
#include "cuda_device.h"
#include "cuda_shader.h"
#include "cuda_mesh.h"
#include "core/stl.h"
#include "optix_accel.h"

namespace ocarina {
void CUDACommandVisitor::visit(const BufferUploadCommand *cmd) noexcept {
    OC_ASSERT((cmd->device_handle() == 0) == (cmd->host_ptr() == 0));
    if (cmd->async() && stream_) {
        OC_CU_CHECK(cuMemcpyHtoDAsync(cmd->device_handle() + cmd->device_offset(),
                                      cmd->host_ptr<const void *>(),
                                      cmd->size_in_bytes(),
                                      stream_));
    } else {
        device_->use_context([&] {
            OC_CU_CHECK(cuMemcpyHtoD(cmd->device_handle() + cmd->device_offset(),
                                     cmd->host_ptr<const void *>(),
                                     cmd->size_in_bytes()));
        });
    }
}

void CUDACommandVisitor::visit(const BufferByteSetCommand *cmd) noexcept {
    device_->use_context([&] {
        if (cmd->async() && stream_) {
            OC_CU_CHECK(cuMemsetD8Async(cmd->device_handle(), cmd->value(),
                                        cmd->size_in_bytes(), stream_));
        } else {
            OC_CU_CHECK(cuMemsetD8(cmd->device_handle(), cmd->value(),
                                   cmd->size_in_bytes()));
        }
    });
}

void CUDACommandVisitor::visit(const BufferCopyCommand *cmd) noexcept {
    auto src_buffer = cmd->src() + cmd->src_offset();
    auto dst_buffer = cmd->dst() + cmd->dst_offset();
    OC_ASSERT((cmd->src() == 0) == (cmd->dst() == 0));
    if (cmd->async() && stream_) {
        OC_CU_CHECK(cuMemcpyDtoDAsync(dst_buffer, src_buffer, cmd->size(), stream_));
    } else {
        device_->use_context([&] {
            OC_CU_CHECK(cuMemcpyDtoD(dst_buffer, src_buffer, cmd->size()));
        });
    }
}

void CUDACommandVisitor::visit(const BufferReallocateCommand *cmd) noexcept {
    //    if (cmd->async() && stream_) {
    //        RHIResource *rhi_resource = cmd->rhi_resource();
    //        if (rhi_resource->handle()) {
    //            OC_CU_CHECK(cuMemFreeAsync(rhi_resource->handle(), stream_));
    //        }
    //        if (cmd->new_size() > 0) {
    //            OC_CU_CHECK(cuMemAllocAsync(reinterpret_cast<handle_ty *>(rhi_resource->handle_ptr()),
    //                                        cmd->new_size(), stream_));
    //        }
    //    } else {
    device_->use_context([&] {
        RHIResource *rhi_resource = cmd->rhi_resource();
        if (rhi_resource->handle()) {
            bool exportable = false;
            auto *exportable_resource = dynamic_cast<ExportableResource *>(rhi_resource);
            if (exportable_resource->exported()) {
                exportable = true;
            }
            handle_ty *ptr = reinterpret_cast<handle_ty *>(rhi_resource->handle_ptr());
            device_->memory_free(ptr);
            *ptr = 0;
            //OC_CU_CHECK(cuMemFree(rhi_resource->handle()));
        }
        if (cmd->new_size() > 0) {
            bool exportable = false;
            auto *exportable_resource = dynamic_cast<ExportableResource *>(rhi_resource);
            if (exportable_resource->exported()) {
                exportable = true;
            }
            device_->memory_allocate(reinterpret_cast<handle_ty *>(rhi_resource->handle_ptr()), cmd->new_size(), exportable);
            //OC_CU_CHECK(cuMemAlloc(reinterpret_cast<handle_ty *>(rhi_resource->handle_ptr()),
            //                       cmd->new_size()));
        }
    });
    //    }
}

void CUDACommandVisitor::visit(const BufferDownloadCommand *cmd) noexcept {
    OC_ASSERT((cmd->device_handle() == 0) == (cmd->host_ptr() == 0));
    if (cmd->async() && stream_) {
        OC_CU_CHECK(cuMemcpyDtoHAsync(cmd->host_ptr<void *>(),
                                      cmd->device_handle() + cmd->device_offset(),
                                      cmd->size_in_bytes(),
                                      stream_));
    } else {
        device_->use_context([&] {
            OC_CU_CHECK(cuMemcpyDtoH(cmd->host_ptr<void *>(),
                                     cmd->device_handle() + cmd->device_offset(),
                                     cmd->size_in_bytes()));
        });
    }
}

void CUDACommandVisitor::visit(const SynchronizeCommand *cmd) noexcept {
    OC_CU_CHECK(cuStreamSynchronize(stream_));
}

void CUDACommandVisitor::visit(const ShaderDispatchCommand *cmd) noexcept {
    cmd->entry<CUDAShader *>()->launch(handle_ty(stream_),
                                       const_cast<ShaderDispatchCommand *>(cmd));
}

void CUDACommandVisitor::visit(const ocarina::HostFunctionCommand *cmd) noexcept {
    if (cmd->async()) {
        std::function<void()> *ptr = new_with_allocator<std::function<void()>>(ocarina::move(cmd->function()));
        OC_CU_CHECK(cuLaunchHostFunc(
            stream_, [](void *ptr) {
                auto func = reinterpret_cast<std::function<void()> *>(ptr);
                (*func)();
                delete_with_allocator(func);
            },
            ptr));
    } else {
        cmd->function()();
    }
}

namespace detail {

[[nodiscard]] CUDA_MEMCPY3D memcpy3d_desc(const TextureOpCommand *cmd) noexcept {
    CUDA_MEMCPY3D memcpy_desc{};
    memcpy_desc.srcXInBytes = 0;
    memcpy_desc.srcY = 0;
    memcpy_desc.srcPitch = cmd->width_in_bytes();
    memcpy_desc.dstPitch = cmd->width_in_bytes();
    memcpy_desc.dstXInBytes = 0;
    memcpy_desc.dstY = 0;
    memcpy_desc.WidthInBytes = cmd->width_in_bytes();
    memcpy_desc.Height = cmd->height();
    memcpy_desc.Depth = cmd->depth();
    return memcpy_desc;
}

[[nodiscard]] CUDA_MEMCPY2D memcpy2d_desc(const TextureOpCommand *cmd) noexcept {
    CUDA_MEMCPY2D memcpy_desc{};
    memcpy_desc.srcXInBytes = 0;
    memcpy_desc.srcY = 0;
    memcpy_desc.srcPitch = cmd->width_in_bytes();
    memcpy_desc.dstPitch = cmd->width_in_bytes();
    memcpy_desc.dstXInBytes = 0;
    memcpy_desc.dstY = 0;
    memcpy_desc.WidthInBytes = cmd->width_in_bytes();
    memcpy_desc.Height = cmd->height();
    return memcpy_desc;
}

}// namespace detail

void CUDACommandVisitor::visit(const Texture3DUploadCommand *cmd) noexcept {
    device_->use_context([&] {
        CUDA_MEMCPY3D desc = detail::memcpy3d_desc(cmd);
        desc.srcMemoryType = CU_MEMORYTYPE_HOST;
        desc.srcHost = cmd->host_ptr<const void *>();
        desc.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        desc.dstArray = cmd->device_handle<CUarray>();
        if (cmd->async() && stream_) {
            OC_CU_CHECK(cuMemcpy3DAsync(&desc, stream_));
        } else {
            OC_CU_CHECK(cuMemcpy3D(&desc));
        }
    });
}

void CUDACommandVisitor::visit(const ocarina::Texture2DUploadCommand *cmd) noexcept {
    device_->use_context([&] {
        CUDA_MEMCPY2D desc = detail::memcpy2d_desc(cmd);
        desc.srcMemoryType = CU_MEMORYTYPE_HOST;
        desc.srcHost = cmd->host_ptr<const void *>();
        desc.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        desc.dstArray = cmd->device_handle<CUarray>();
        if (cmd->async() && stream_) {
            OC_CU_CHECK(cuMemcpy2DAsync(&desc, stream_));
        } else {
            OC_CU_CHECK(cuMemcpy2D(&desc));
        }
    });
}

void CUDACommandVisitor::visit(const Texture3DDownloadCommand *cmd) noexcept {
    device_->use_context([&] {
        CUDA_MEMCPY3D desc = detail::memcpy3d_desc(cmd);
        desc.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        desc.dstMemoryType = CU_MEMORYTYPE_HOST;
        desc.srcArray = cmd->device_handle<CUarray>();
        desc.dstHost = reinterpret_cast<void *>(cmd->host_ptr());
        if (cmd->async() && stream_) {
            OC_CU_CHECK(cuMemcpy3DAsync(&desc, stream_));
        } else {
            OC_CU_CHECK(cuMemcpy3D(&desc));
        }
    });
}

void CUDACommandVisitor::visit(const Texture2DDownloadCommand *cmd) noexcept {
    device_->use_context([&] {
        CUDA_MEMCPY2D desc = detail::memcpy2d_desc(cmd);
        desc.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        desc.dstMemoryType = CU_MEMORYTYPE_HOST;
        desc.srcArray = cmd->device_handle<CUarray>();
        desc.dstHost = reinterpret_cast<void *>(cmd->host_ptr());
        if (cmd->async() && stream_) {
            OC_CU_CHECK(cuMemcpy2DAsync(&desc, stream_));
        } else {
            OC_CU_CHECK(cuMemcpy2D(&desc));
        }
    });
}

void CUDACommandVisitor::visit(const Texture3DCopyCommand *cmd) noexcept {
    device_->use_context([&] {
        CUDA_MEMCPY3D copy{};
        uint pitch = pixel_size(cmd->pixel_storage()) * cmd->resolution().x;
        copy.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        copy.srcArray = cmd->src<CUarray>();
        copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        copy.dstArray = cmd->dst<CUarray>();
        copy.WidthInBytes = pitch;
        copy.Height = cmd->resolution().y;
        copy.Depth = cmd->resolution().z;
        if (cmd->async() && stream_) {
            OC_CU_CHECK(cuMemcpy3DAsync(&copy, stream_));
        } else {
            OC_CU_CHECK(cuMemcpy3D(&copy));
        }
    });
}

void CUDACommandVisitor::visit(const Texture2DCopyCommand *cmd) noexcept {
    device_->use_context([&] {
        CUDA_MEMCPY2D copy{};
        uint pitch = pixel_size(cmd->pixel_storage()) * cmd->resolution().x;
        copy.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        copy.srcArray = cmd->src<CUarray>();
        copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        copy.dstArray = cmd->dst<CUarray>();
        copy.WidthInBytes = pitch;
        copy.Height = cmd->resolution().y;
        if (cmd->async() && stream_) {
            OC_CU_CHECK(cuMemcpy2DAsync(&copy, stream_));
        } else {
            OC_CU_CHECK(cuMemcpy2D(&copy));
        }
    });
}

void CUDACommandVisitor::visit(const ocarina::BufferToTexture3DCommand *cmd) noexcept {
    device_->use_context([&] {
        CUDA_MEMCPY3D copy{};
        copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.srcDevice = cmd->src() + cmd->buffer_offset();
        copy.srcPitch = cmd->width_in_bytes();
        copy.srcHeight = cmd->height();
        copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        copy.dstArray = cmd->dst<CUarray>();
        copy.WidthInBytes = cmd->width_in_bytes();
        copy.Height = cmd->height();
        copy.Depth = cmd->depth();
        if (cmd->async() && stream_) {
            OC_CU_CHECK(cuMemcpy3DAsync(&copy, stream_));
        } else {
            OC_CU_CHECK(cuMemcpy3D(&copy));
        }
    });
}

void CUDACommandVisitor::visit(const ocarina::BufferToTexture2DCommand *cmd) noexcept {
    device_->use_context([&] {
        CUDA_MEMCPY2D copy{};
        copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.srcDevice = cmd->src() + cmd->buffer_offset();
        copy.srcPitch = cmd->width_in_bytes();
        copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        copy.dstArray = cmd->dst<CUarray>();
        copy.WidthInBytes = cmd->width_in_bytes();
        copy.Height = cmd->height();
        if (cmd->async() && stream_) {
            OC_CU_CHECK(cuMemcpy2DAsync(&copy, stream_));
        } else {
            OC_CU_CHECK(cuMemcpy2D(&copy));
        }
    });
}

void CUDACommandVisitor::visit(const BLASBuildCommand *cmd) noexcept {
    cmd->mesh<CUDAMesh>()->build_bvh(cmd);
}

void CUDACommandVisitor::visit(const TLASBuildCommand *cmd) noexcept {
    cmd->accel<OptixAccel>()->build_bvh(this);
}

void CUDACommandVisitor::visit(const ocarina::TLASUpdateCommand *cmd) noexcept {
    cmd->accel<OptixAccel>()->update_bvh(this);
}

}// namespace ocarina