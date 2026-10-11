//
// Created by zero on 2022/7/18.
//

#include "cuda_shader.h"
#include "core/util/switch_profile.h"
#include "util.h"
#include "cuda_device.h"
#include <optix_stack_size.h>
#include <optix_stubs.h>
#include "cuda_command_visitor.h"
#include <mutex>

namespace ocarina {

namespace {

[[nodiscard]] bool valid_dim(uint3 dim) noexcept {
    return dim.x != 0u && dim.y != 0u && dim.z != 0u;
}

[[nodiscard]] uint ceil_div(uint num, uint den) noexcept {
    return num / den + uint(num % den != 0u);
}

[[nodiscard]] uint3 ceil_div(uint3 num, uint3 den) noexcept {
    return make_uint3(ceil_div(num.x, den.x),
                      ceil_div(num.y, den.y),
                      ceil_div(num.z, den.z));
}

[[nodiscard]] uint min_u(uint lhs, uint rhs) noexcept {
    return lhs < rhs ? lhs : rhs;
}

[[nodiscard]] uint max_u(uint lhs, uint rhs) noexcept {
    return lhs > rhs ? lhs : rhs;
}

[[nodiscard]] uint volume(uint3 dim) noexcept {
    return dim.x * dim.y * dim.z;
}

[[nodiscard]] uint3 min_dim(uint3 lhs, uint3 rhs) noexcept {
    return make_uint3(min_u(lhs.x, rhs.x), min_u(lhs.y, rhs.y), min_u(lhs.z, rhs.z));
}

[[nodiscard]] int score_candidate(uint3 candidate, uint3 dispatch_dim, uint max_threads) noexcept {
    uint threads = volume(candidate);
    if (threads == 0u || threads > max_threads) {
        return std::numeric_limits<int>::min();
    }
    uint3 covered_dim = min_dim(candidate, dispatch_dim);
    uint active_threads = volume(covered_dim);
    uint wasted_threads = threads - active_threads;
    return static_cast<int>(active_threads * 4096u + threads * 4u - wasted_threads);
}

[[nodiscard]] uint3 choose_block_shape(uint3 dispatch_dim, uint max_threads) noexcept {
    // First match the dispatch topology (1D/2D/3D), then pick the densest block
    // shape that still fits the occupancy-guided thread budget.
    max_threads = max_u(1u, min_u(max_threads, 1024u));

    if (dispatch_dim.z > 1u) {
        static constexpr std::array<uint3, 7> candidates{
            make_uint3(8u, 8u, 4u),
            make_uint3(8u, 4u, 4u),
            make_uint3(4u, 4u, 4u),
            make_uint3(8u, 4u, 2u),
            make_uint3(4u, 4u, 2u),
            make_uint3(2u, 2u, 2u),
            make_uint3(1u, 1u, 1u)};
        uint3 best = make_uint3(1u);
        int best_score = std::numeric_limits<int>::min();
        for (uint3 candidate : candidates) {
            int score = score_candidate(candidate, dispatch_dim, max_threads);
            if (score > best_score) {
                best_score = score;
                best = candidate;
            }
        }
        return best;
    }

    if (dispatch_dim.y > 1u) {
        static constexpr std::array<uint3, 14> candidates{
            make_uint3(32u, 32u, 1u),
            make_uint3(32u, 16u, 1u),
            make_uint3(16u, 32u, 1u),
            make_uint3(16u, 16u, 1u),
            make_uint3(32u, 8u, 1u),
            make_uint3(8u, 32u, 1u),
            make_uint3(16u, 8u, 1u),
            make_uint3(8u, 16u, 1u),
            make_uint3(8u, 8u, 1u),
            make_uint3(8u, 4u, 1u),
            make_uint3(4u, 8u, 1u),
            make_uint3(4u, 4u, 1u),
            make_uint3(2u, 2u, 1u),
            make_uint3(1u, 1u, 1u)};
        uint3 best = make_uint3(1u);
        int best_score = std::numeric_limits<int>::min();
        for (uint3 candidate : candidates) {
            int score = score_candidate(candidate, dispatch_dim, max_threads);
            if (score > best_score) {
                best_score = score;
                best = candidate;
            }
        }
        return best;
    }

    static constexpr std::array<uint, 11> candidates{1024u, 512u, 256u, 128u, 64u, 32u, 16u, 8u, 4u, 2u, 1u};
    for (uint candidate : candidates) {
        if (candidate <= max_threads) {
            return make_uint3(min_u(dispatch_dim.x, candidate), 1u, 1u);
        }
    }
    return make_uint3(1u);
}

}// namespace

CUDAShader::CUDAShader(Device::Impl *device,
                       const Function &func)
    : device_(dynamic_cast<CUDADevice *>(device)),
      function_(func) {}

class CUDASimpleShader : public CUDAShader {
private:
    CUmodule module_{};
    CUfunction func_handle_{};
    uint preferred_block_threads_{0u};

private:
    void ensure_preferred_block_threads() noexcept {
        if (preferred_block_threads_ != 0u) {
            return;
        }
        device_->use_context([&] {
            int min_grid_size = 0;
            int auto_block_size = 0;
            OC_CU_CHECK(cuOccupancyMaxPotentialBlockSize(&min_grid_size, &auto_block_size,
                                                         func_handle_, 0, 0, 0));
            preferred_block_threads_ = max_u(1u, static_cast<uint>(auto_block_size));
        });
    }

    [[nodiscard]] uint3 auto_block_dim(uint3 dispatch_dim) noexcept {
        ensure_preferred_block_threads();
        return choose_block_shape(dispatch_dim, preferred_block_threads_);
    }

public:
    CUDASimpleShader(Device::Impl *device,
                     const ocarina::string &ptx,
                     const Function &f) : CUDAShader(device, f) {
        CUresult load_result;
        {
            switch_profile::Scope profile{"cuModuleLoadData", "cuda_module"};
            load_result = cuModuleLoadData(&module_, ptx.c_str());
        }
        OC_CU_CHECK(load_result);
        CUresult function_result;
        {
            switch_profile::Scope profile{"cuModuleGetFunction", "cuda_module"};
            function_result = cuModuleGetFunction(&func_handle_, module_, function_.func_name().c_str());
        }
        OC_CU_CHECK(function_result);
    }
    ~CUDASimpleShader() override {
        OC_CU_CHECK(cuModuleUnload(module_));
    }
    void launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept override {
        uint3 dispatch_dim = cmd->dispatch_dim();
        if (!valid_dim(dispatch_dim)) return;
        // User-provided launch dimensions win. Otherwise we derive a block shape
        // from the occupancy budget and compute the matching grid dimensions.
        uint3 block_dim = valid_dim(function_.block_dim()) ? function_.block_dim() : auto_block_dim(dispatch_dim);
        uint3 grid_dim = valid_dim(function_.grid_dim()) ? function_.grid_dim() : ceil_div(dispatch_dim, block_dim);
        if (!valid_dim(block_dim)) {
            block_dim = make_uint3(1u);
        }
        if (!valid_dim(grid_dim)) {
            grid_dim = ceil_div(dispatch_dim, block_dim);
        }
        OC_CU_CHECK(cuLaunchKernel(func_handle_, grid_dim.x, grid_dim.y, grid_dim.z,
                                   block_dim.x, block_dim.y, block_dim.z,
                                   0, reinterpret_cast<CUstream>(stream), cmd->args().data(), nullptr));
    }
    void compute_fit_size() noexcept override {
        ensure_preferred_block_threads();
        // Keep the legacy API surface, but only cache the preferred thread budget.
        // The final block shape still depends on the dispatch dimensionality.
        function_.set_grid_dim(0u);
        function_.set_block_dim(preferred_block_threads_);
    }
};

struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) SBTRecord {
    std::byte data[OPTIX_SBT_RECORD_HEADER_SIZE];
};

struct ProgramName {
    const char *raygen{};
    const char *closesthit_closest{};
    const char *closesthit_occlusion{};
};

struct ProgramGroupTable {
    OptixProgramGroup raygen_group{nullptr};
    OptixProgramGroup miss_closest_group{nullptr};
    OptixProgramGroup hit_closest_group{nullptr};
    OptixProgramGroup hit_any_group{nullptr};

    ProgramGroupTable() = default;

    static constexpr auto size() {
        return sizeof(ProgramGroupTable) / sizeof(OptixProgramGroup);
    }

    void clear() const {
        OC_OPTIX_CHECK(optixProgramGroupDestroy(raygen_group));
        OC_OPTIX_CHECK(optixProgramGroupDestroy(hit_closest_group));
        OC_OPTIX_CHECK(optixProgramGroupDestroy(hit_any_group));
        OC_OPTIX_CHECK(optixProgramGroupDestroy(miss_closest_group));
    }
};

class OptixShader : public CUDAShader {
private:
    OptixModule optix_module_{};
    OptixPipeline optix_pipeline_{};
    OptixPipelineCompileOptions pipeline_compile_options_ = {};
    ProgramGroupTable program_group_table_;
    Buffer<SBTRecord> sbt_records_{};
    OptixShaderBindingTable sbt_{};
    std::mutex params_mutex_;
    struct StreamLaunchState {
        Buffer<std::byte> params;
        OptixPipeline pipeline{};
        CUevent completed{};
    };
    ocarina::map<handle_ty, StreamLaunchState> stream_params_;
    std::array<OptixPayloadType, 1> payload_types_{};

public:
    void init_module(const string_view &ptx_code) {
        switch_profile::Scope profile{"OptixShader::init_module", "optix_module"};
        OptixModuleCompileOptions module_compile_options = {};
        static constexpr std::array<uint, 1> ray_trace_payload_semantics{
            OPTIX_PAYLOAD_SEMANTICS_TRACE_CALLER_WRITE | OPTIX_PAYLOAD_SEMANTICS_CH_READ,
        };

        static constexpr std::array<uint, 2> ray_query_payload_semantics{
            OPTIX_PAYLOAD_SEMANTICS_TRACE_CALLER_WRITE | OPTIX_PAYLOAD_SEMANTICS_IS_READ | OPTIX_PAYLOAD_SEMANTICS_AH_READ,
            OPTIX_PAYLOAD_SEMANTICS_TRACE_CALLER_WRITE | OPTIX_PAYLOAD_SEMANTICS_IS_READ | OPTIX_PAYLOAD_SEMANTICS_AH_READ,
        };

        payload_types_[0].numPayloadValues = ray_trace_payload_semantics.size();
        payload_types_[0].payloadSemantics = ray_trace_payload_semantics.data();
        // payload_types[1].numPayloadValues = ray_query_payload_semantics.size();
        // payload_types[1].payloadSemantics = ray_query_payload_semantics.data();

        // TODO: REVIEW THIS
        module_compile_options.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
        //#ifndef NDEBUG
        //        module_compile_options.optLevel = OPTIX_COMPILE_OPTIMIZATION_LEVEL_0;
//        module_compile_options.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_NONE;
        //#else
        module_compile_options.optLevel = OPTIX_COMPILE_OPTIMIZATION_LEVEL_3;
        // module_compile_options.numPayloadTypes = payload_types_.size();
        // module_compile_options.payloadTypes = payload_types_.data();

        //        module_compile_options.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_NONE;
        //#endif
        pipeline_compile_options_.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
        pipeline_compile_options_.usesMotionBlur = false;
        //todo WTF?
        pipeline_compile_options_.numPayloadValues = 32;
        pipeline_compile_options_.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
        //        pipeline_compile_options_.numAttributeValues = 2;

        //#ifndef NDEBUG
        //        pipeline_compile_options_.exceptionFlags = (OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW |
        //                                                    OPTIX_EXCEPTION_FLAG_TRACE_DEPTH |
        //                                                    OPTIX_EXCEPTION_FLAG_DEBUG);
        //#else
        pipeline_compile_options_.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
        //#endif
        pipeline_compile_options_.pipelineLaunchParamsVariableName = "params";
        char log[2048];
        size_t log_size = sizeof(log);
        OptixResult module_result;
        {
            switch_profile::Scope api_profile{"optixModuleCreate", "optix_module"};
            module_result = optixModuleCreate(
                device_->optix_device_context(),
                &module_compile_options,
                &pipeline_compile_options_,
                ptx_code.data(), ptx_code.size(),
                log, &log_size, &optix_module_);
        }
        OC_OPTIX_CHECK_WITH_LOG(module_result, log);
    }

    [[nodiscard]] OptixPipeline build_pipeline(OptixDeviceContext optix_device_context) noexcept {
        switch_profile::Scope profile{"OptixShader::build_pipeline", "optix_pipeline"};
        constexpr int max_trace_depth = 1;
        OptixPipeline pipeline{};

        OptixPipelineLinkOptions pipeline_link_options = {};
        pipeline_link_options.maxTraceDepth = max_trace_depth;
//        pipeline_link_options.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_NONE;
        char log[2048];
        size_t sizeof_log = sizeof(log);

        OptixResult pipeline_result;
        {
            switch_profile::Scope api_profile{"optixPipelineCreate", "optix_pipeline"};
            pipeline_result = optixPipelineCreate(
                optix_device_context,
                &pipeline_compile_options_,
                &pipeline_link_options,
                (OptixProgramGroup *)&program_group_table_,
                program_group_table_.size(),
                log, &sizeof_log,
                &pipeline);
        }
        OC_OPTIX_CHECK_WITH_LOG(pipeline_result, log);

        // Set shaders stack sizes.
        OptixStackSizes stack_sizes = {};
        OC_OPTIX_CHECK(optixUtilAccumulateStackSizes(program_group_table_.raygen_group, &stack_sizes, pipeline));
        OC_OPTIX_CHECK(optixUtilAccumulateStackSizes(program_group_table_.miss_closest_group, &stack_sizes, pipeline));
        OC_OPTIX_CHECK(optixUtilAccumulateStackSizes(program_group_table_.hit_closest_group, &stack_sizes, pipeline));
        OC_OPTIX_CHECK(optixUtilAccumulateStackSizes(program_group_table_.hit_any_group, &stack_sizes, pipeline));

        uint32_t direct_callable_stack_size_from_traversal;
        uint32_t direct_callable_stack_size_from_state;
        uint32_t continuation_stack_size;
        OC_OPTIX_CHECK(optixUtilComputeStackSizes(&stack_sizes,
                                                  max_trace_depth,
                                                  0,// maxCCDepth
                                                  0,// maxDCDEpth
                                                  &direct_callable_stack_size_from_traversal,
                                                  &direct_callable_stack_size_from_state,
                                                  &continuation_stack_size));
        OC_OPTIX_CHECK(optixPipelineSetStackSize(pipeline,
                                                 direct_callable_stack_size_from_traversal,
                                                 direct_callable_stack_size_from_state,
                                                 continuation_stack_size,
                                                 2// maxTraversableDepth
                                                 ));
        return pipeline;
    }

    void build_sbt(ProgramGroupTable program_group_table) {
        switch_profile::Scope profile{"OptixShader::build_sbt", "optix_sbt"};
        SBTRecord sbt[ProgramGroupTable::size()] = {};
        sbt_records_ = Buffer<SBTRecord>(device_, ProgramGroupTable::size(), "OptixShader::sbt_records_");
        OC_OPTIX_CHECK(optixSbtRecordPackHeader(program_group_table.raygen_group, &sbt[0]));
        OC_OPTIX_CHECK(optixSbtRecordPackHeader(program_group_table.hit_closest_group, &sbt[1]));
        OC_OPTIX_CHECK(optixSbtRecordPackHeader(program_group_table.hit_any_group, &sbt[2]));
        OC_OPTIX_CHECK(optixSbtRecordPackHeader(program_group_table.miss_closest_group, &sbt[3]));
        sbt_records_.upload_immediately(sbt);

        sbt_.raygenRecord = sbt_records_.ptr<CUdeviceptr>();
        sbt_.hitgroupRecordBase = sbt_records_.address<CUdeviceptr>(1);
        sbt_.hitgroupRecordStrideInBytes = sizeof(SBTRecord);
        sbt_.hitgroupRecordCount = 2;
        sbt_.missRecordBase = sbt_records_.address<CUdeviceptr>(3);
        sbt_.missRecordStrideInBytes = sizeof(SBTRecord);
        sbt_.missRecordCount = 1;
    }

    ProgramGroupTable create_program_groups(OptixDeviceContext optix_device_context,
                                            const ProgramName &program_name) {
        switch_profile::Scope profile{"OptixShader::create_program_groups", "optix_program_group"};
        OptixProgramGroupOptions program_group_options = {};
//        program_group_options.payloadType = payload_types_.data();
        char log[2048];
        size_t sizeof_log = sizeof(log);
        ProgramGroupTable program_group_table;
        {
            OptixProgramGroupDesc raygen_prog_group_desc = {};
            raygen_prog_group_desc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
            raygen_prog_group_desc.raygen.module = optix_module_;
            raygen_prog_group_desc.raygen.entryFunctionName = program_name.raygen;
            OptixResult raygen_result;
            {
                switch_profile::Scope api_profile{"optixProgramGroupCreate.raygen", "optix_program_group"};
                raygen_result = optixProgramGroupCreate(
                    optix_device_context, &raygen_prog_group_desc,
                    1,// num program groups
                    &program_group_options, log, &sizeof_log,
                    &(program_group_table.raygen_group));
            }
            OC_OPTIX_CHECK_WITH_LOG(raygen_result, log);
        }
        {
            OptixProgramGroupDesc hit_prog_group_desc = {};
            hit_prog_group_desc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
            hit_prog_group_desc.hitgroup.moduleCH = optix_module_;
            hit_prog_group_desc.hitgroup.entryFunctionNameCH = program_name.closesthit_closest;
            sizeof_log = sizeof(log);

            OptixResult closest_result;
            {
                switch_profile::Scope api_profile{"optixProgramGroupCreate.closesthit", "optix_program_group"};
                closest_result = optixProgramGroupCreate(
                    optix_device_context, &hit_prog_group_desc,
                    1,// num program groups
                    &program_group_options, log, &sizeof_log,
                    &(program_group_table.hit_closest_group));
            }
            OC_OPTIX_CHECK_WITH_LOG(closest_result, log);

            memset(&hit_prog_group_desc, 0, sizeof(OptixProgramGroupDesc));
            hit_prog_group_desc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
            hit_prog_group_desc.hitgroup.moduleCH = optix_module_;
            hit_prog_group_desc.hitgroup.entryFunctionNameCH = program_name.closesthit_occlusion;
            sizeof_log = sizeof(log);

            OptixResult anyhit_result;
            {
                switch_profile::Scope api_profile{"optixProgramGroupCreate.anyhit", "optix_program_group"};
                anyhit_result = optixProgramGroupCreate(
                    optix_device_context, &hit_prog_group_desc,
                    1,// num program groups
                    &program_group_options, log, &sizeof_log,
                    &(program_group_table.hit_any_group));
            }
            OC_OPTIX_CHECK_WITH_LOG(anyhit_result, log);
        }
        {
            OptixProgramGroupDesc miss_prog_group_desc = {};
            miss_prog_group_desc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
            sizeof_log = sizeof(log);
            OptixResult miss_result;
            {
                switch_profile::Scope api_profile{"optixProgramGroupCreate.miss", "optix_program_group"};
                miss_result = optixProgramGroupCreate(
                    optix_device_context, &miss_prog_group_desc,
                    1,// num program groups
                    &program_group_options, log, &sizeof_log,
                    &(program_group_table.miss_closest_group));
            }
            OC_OPTIX_CHECK_WITH_LOG(miss_result, log);
        }

        return program_group_table;
    }

    OptixShader(Device::Impl *device,
                const ocarina::string &ptx,
                const Function &f) : CUDAShader(device, f) {
        device_->init_optix_context();
        init_module(ptx);
        string raygen_entry = function_.func_name();
        ProgramName entries{
            raygen_entry.c_str(),
            "__closesthit__closest",
            "__closesthit__occlusion"};
        program_group_table_ = create_program_groups(device_->optix_device_context(), entries);
        build_sbt(program_group_table_);
        optix_pipeline_ = build_pipeline(device_->optix_device_context());
    }

    void launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept override {
        // Stream handles can identify their context for some CUDA APIs, but
        // event creation and lazy OptiX pipeline creation require it current.
        device_->use_context([&] { launch_on_current_context(stream, cmd); });
    }

    void launch_on_current_context(handle_ty stream, ShaderDispatchCommand *cmd) noexcept {
        auto dim = cmd->dispatch_dim();
        if (!valid_dim(dim)) return;
        uint x = dim.x;
        uint y = dim.y;
        uint z = dim.z;
        auto cu_stream = reinterpret_cast<CUstream>(stream);
        auto arguments = cmd->argument_data();
        CUdeviceptr params = 0;
        OptixPipeline pipeline{};
        CUevent completed{};
        {
            // Upload and launch are ordered within each stream, but different
            // streams must never overwrite the same launch-parameter storage.
            // Cache ordinary allocations to avoid async-allocation requirements
            // and allocator/host-callback overhead on steady-state launches.
            std::lock_guard lock(params_mutex_);
            const bool first_stream = stream_params_.empty();
            auto &state = stream_params_[stream];
            if (!state.pipeline) {
                // Concurrent OptiX launches require distinct pipelines, while
                // compiled modules, program groups and immutable SBT can share.
                state.pipeline = first_stream ? optix_pipeline_ : build_pipeline(device_->optix_device_context());
                OC_CU_CHECK(cuEventCreate(&state.completed, CU_EVENT_DISABLE_TIMING));
            }
            auto &storage = state.params;
            if (!storage.valid() || storage.size() < arguments.size()) {
                storage = Buffer<std::byte>(device_, arguments.size(), "OptixShader::stream_params");
            }
            params = storage.handle();
            pipeline = state.pipeline;
            completed = state.completed;
        }
        OC_CU_CHECK(cuMemcpyHtoDAsync(params, arguments.data(),
                                      arguments.size(), cu_stream));

        OC_OPTIX_CHECK(optixLaunch(pipeline,
                                   cu_stream,
                                   params,
                                   arguments.size(),
                                   &sbt_,
                                   x, y, z));
        OC_CU_CHECK(cuEventRecord(completed, cu_stream));
    }
    ~OptixShader() override {
        device_->use_context([&] {
            // Native pipelines and launch parameters remain live until their
            // last GPU use, including when the public shader is released first.
            for (auto &[stream, state] : stream_params_) {
                OC_CU_CHECK(cuEventSynchronize(state.completed));
                OC_CU_CHECK(cuEventDestroy(state.completed));
                if (state.pipeline != optix_pipeline_) OC_OPTIX_CHECK(optixPipelineDestroy(state.pipeline));
            }
            OC_OPTIX_CHECK(optixPipelineDestroy(optix_pipeline_));
            program_group_table_.clear();
            OC_OPTIX_CHECK(optixModuleDestroy(optix_module_));
            stream_params_.clear();
            sbt_records_.destroy();
        });
    }
};

CUDAShader *CUDAShader::create(Device::Impl *device, const string &ptx, const Function &f) {
    switch_profile::Scope profile{"CUDAShader::create", "backend"};
    if (f.is_raytracing()) {
        return ocarina::new_with_allocator<OptixShader>(device, ptx, f);
    } else {
        return ocarina::new_with_allocator<CUDASimpleShader>(device, ptx, f);
    }
}

}// namespace ocarina
