#include <horizon.h>
#include <Codegen/BuiltinVariate.h>
#include <Codegen/ControlFlows.h>
#include <Codegen/RasterizedPipelineObject.h>
#include <Codegen/TypeAlias.h>
#include <example_edsl_compile/example_edsl_compile.h>
#include <slang.h>
#include <slang-com-ptr.h>

void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
{
    if (diagnosticsBlob != nullptr)
    {
        std::cout << static_cast<const char*>(diagnosticsBlob->getBufferPointer()) << std::endl;
    }
}

auto loadModule(const Slang::ComPtr<slang::ISession>& session,std::string_view name, std::string_view shader)
{
    Slang::ComPtr<slang::IModule> slangModule{};
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        slangModule = session->loadModuleFromSourceString(name.data(), "", shader.data(), diagnosticsBlob.writeRef());
        diagnoseIfNeeded(diagnosticsBlob);
        if (!slangModule)
        {
            throw std::runtime_error("Failed to load slang module");
        }
    }
    return slangModule;
}

using namespace EmbeddedShader;

void run_example_edsl_compile()
{
    // Texture2D<ktm::fvec4> in;
    // Texture2D<ktm::fvec4> out;
    // bool isAdd = true;
    // auto compute = [&]() {
    //     $IF (isAdd)
    //         out[dispatchThreadID()->xy()] += in[dispatchThreadID()->xy()];
    //     $ELSE
    //         out[dispatchThreadID()->xy()] -= in[dispatchThreadID()->xy()];
    // };
    //
    // auto cp = ComputePipelineObject::compile(compute);

    auto core = R"([vk::binding(0, 0)]
__DynamicResource<__DynamicResourceKind.General> combinedTextureSamplerHandles[];

[vk::binding(0, 1)]
__DynamicResource<__DynamicResourceKind.General> bufferHandles[];

[vk::binding(0, 2)]
__DynamicResource<__DynamicResourceKind.General> textureHandles[];

export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle) where T : IOpaqueDescriptor
{
        __target_switch
        {
                case spirv:
                case glsl:
                if (T.kind == DescriptorKind.CombinedTextureSampler)
                        return combinedTextureSamplerHandles[((uint2)handle).x].asOpaqueDescriptor<T>();
                else if (T.kind == DescriptorKind.Buffer)
                        return bufferHandles[((uint2)handle).x].asOpaqueDescriptor<T>();
                else if (T.kind == DescriptorKind.Texture)
                        return textureHandles[((uint2)handle).x].asOpaqueDescriptor<T>();
                else
                        return defaultGetDescriptorFromHandle(handle);
                default:
                return defaultGetDescriptorFromHandle(handle);
        }
}
import type_header;
extern void branch_0([[vk::push_constant]] ConstantBuffer<global_push_constant_struct> global_push_constant,in compute_input input);
[shader("compute")]
[numthreads(1,1,1)]
void main(compute_input input) {

        branch_0(global_push_constant,input);
}
)";

    auto typeHeader = R"(struct global_push_constant_struct {
        Texture2D<float4>.Handle global_var_0;
        RWTexture2D<float4>.Handle global_var_1;
}
[[vk::push_constant]] [[vk::push_constant]] ConstantBuffer<global_push_constant_struct> global_push_constant;
struct compute_input {
        uint3 dispatch_thread_id_input : SV_DispatchThreadID;
}
)";
    auto trueBranch = R"(import type_header;
export void branch_0([[vk::push_constant]] ConstantBuffer<global_push_constant_struct> global_push_constant,in compute_input input)
{
                global_push_constant.global_var_1[input.dispatch_thread_id_input.xy] = (global_push_constant.global_var_1[input.dispatch_thread_id_input.xy] + global_push_constant.global_var_0[input.dispatch_thread_id_input.xy]);
}
)";
    auto globalSession = ShaderLanguageConverter::getGlobalSession();
        std::vector<slang::TargetDesc> compileTargets;
        auto sm_6_6 = globalSession->findProfile("sm_6_6");
        compileTargets.push_back(slang::TargetDesc{.format = SLANG_HLSL, .profile = sm_6_6});
        std::array<slang::CompilerOptionEntry, 1> options = {
            {slang::CompilerOptionName::EmitSpirvDirectly,
             {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}}};
        auto session = ShaderLanguageConverter::createSession(globalSession, compileTargets, options);
        std::string_view srcStr = "slang";
        options = {
            {slang::CompilerOptionName::Language,
             {slang::CompilerOptionValueKind::String, 0, 0, srcStr.data(), nullptr}}
        };
        compileTargets.clear();
        compileTargets.push_back(slang::TargetDesc{.format = SLANG_CPP_SOURCE});

        auto modSession = ShaderLanguageConverter::createSession(globalSession, compileTargets, options);

    auto typeHeaderMod2 = ShaderLanguageConverter::convertSlangModule(ShaderLanguageConverter::loadModule(modSession, "type_header",typeHeader));
    auto trueBranchMod2 = ShaderLanguageConverter::convertSlangModule(ShaderLanguageConverter::loadModule(modSession, "true_branch",trueBranch));
    auto coreMod2 = ShaderLanguageConverter::convertSlangModule(ShaderLanguageConverter::loadModule(modSession, "source",core));

    auto typeHeaderMod = ShaderLanguageConverter::loadModule(session, typeHeaderMod2);
    auto trueBranchMod = ShaderLanguageConverter::loadModule(session, trueBranchMod2);
    auto coreMod = ShaderLanguageConverter::loadModule(session, coreMod2);

    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        coreMod->findEntryPointByName("main", entryPoint.writeRef());
        if (!entryPoint)
        {
            std::cout << "Error getting entry point" << std::endl;
            throw std::runtime_error("Error");
        }
    }

    std::array<slang::IComponentType*,4> componentTypes =
        {
        coreMod,
        entryPoint,
        typeHeaderMod,
        trueBranchMod,
    };

    Slang::ComPtr<slang::IComponentType> linkedProgram = ShaderLanguageConverter::getLinkedProgram(session, componentTypes);

    Slang::ComPtr<slang::IBlob> hlslCode = ShaderLanguageConverter::getFinalCode(linkedProgram, 0);

    std::cout << static_cast<const char*>(hlslCode->getBufferPointer()) << std::endl;
}