// Ocarina Slang device-side resource types.
// Replaces cuda_device_resource.h — mirrors host BufferDesc<>/TextureDesc/BindlessArrayDesc layouts.

// ── Buffer: physical-storage-buffer reference (VkDeviceAddress) ──────────────

[[vk::buffer_reference, vk::buffer_reference_align(4)]]
struct OCRawBuffer { uint8_t data[]; };

struct OCBuffer<T> {
    OCRawBuffer handle;  // VkDeviceAddress stored as buffer_reference
    uint        offset;
    uint64_t    size;
};

// ── Texture descriptor: holds global descriptor-array indices ─────────────────

struct OCTextureDesc {
    uint tex_index;      // index into g_bindless_textures[]
    uint surf_index;     // index into g_bindless_storage_images[]
    uint pixel_storage;
};

// ── Bindless-array descriptor: device addresses of per-type slot SSBOs ────────

struct OCBindlessArrayDesc {
    uint64_t buffer_slot;
    uint64_t tex3d_slot;
    uint64_t tex2d_slot;
};
