//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/concepts.h"



namespace ocarina {
struct RHIPipeline;
class Texture3D;

class DescriptorSet : concepts::Noncopyable {
public:
    //DescriptorSet() {}
    virtual ~DescriptorSet() {}
    void set_is_global(bool is_global) { is_global_ = is_global; }
    bool is_global() const { return is_global_; }

    virtual void update_buffer(uint64_t name_id, void *data, uint32_t size) = 0;
    virtual void update_texture(uint64_t name_id, Texture3D *texture) = 0;

private:
    bool is_global_ = false;
};

class DescriptorSetLayout : concepts::Noncopyable {
public:
    DescriptorSetLayout() {}
    virtual ~DescriptorSetLayout() {}

    virtual DescriptorSet* allocate_descriptor_set() = 0;

    const std::string get_name() const { return name_; }
    void set_name(const std::string &name) {
        name_ = name;
    }

    bool is_global_ubo() const { return is_global_ubo_; }
    bool is_global_textures() const { return is_global_textures_; }
    void set_is_global_textures(bool is_global) { is_global_textures_ = is_global; }

private:
    std::string name_;

protected:
    bool is_global_ubo_ = false;
    bool is_global_textures_ = false;
};

class DescriptorSetWriter : concepts::Noncopyable {
public:
    DescriptorSetWriter() {}
    virtual ~DescriptorSetWriter() {}
    //virtual void write_descriptor_set(DescriptorSet &descriptor_set) = 0;
    //// Add more methods to set different types of descriptors
    //virtual void set_uniform_buffer(uint32_t binding, const void *data, size_t size) = 0;
    //virtual void set_storage_buffer(uint32_t binding, const void *data, size_t size) = 0;
    //virtual void set_texture3d(uint32_t binding, const void *texture) = 0;
    //virtual void set_sampler(uint32_t binding, const void *sampler) = 0;
    //virtual void bind_buffer(uint64_t name_id, handle_ty buffer) = 0;
    //virtual void bind_texture(uint64_t name_id, handle_ty texture) = 0;
    virtual void update_buffer(uint64_t name_id, void *data, uint32_t size) = 0;
    virtual void update_push_constants(uint64_t name_id, void *data, uint32_t size, RHIPipeline* pipeline) = 0;
    virtual void update_texture(uint64_t name_id, Texture3D * texture) = 0;

protected:
    //unique_ptr<DescriptorSet> descriptor_set_ = nullptr;
    DescriptorSetLayout *descriptor_set_layout_ = nullptr;
};

}// namespace ocarina