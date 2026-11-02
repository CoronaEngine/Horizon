#include <horizon.h>
#include <Codegen/BuiltinVariate.h>
#include <Codegen/ControlFlows.h>
#include <Codegen/RasterizedPipelineObject.h>
#include <Codegen/TypeAlias.h>
#include <example_edsl_compile/example_edsl_compile.h>

void run_example_edsl_compile()
{
    using namespace EmbeddedShader;
    Texture2D<ktm::fvec4> in;
    Texture2D<ktm::fvec4> out;
    bool isAdd = true;
    auto compute = [&]() {
        $IF (isAdd)
            out[dispatchThreadID()->xy()] += in[dispatchThreadID()->xy()];
        $ELSE
            out[dispatchThreadID()->xy()] -= in[dispatchThreadID()->xy()];
    };

    auto cp = ComputePipelineObject::compile(compute);
    std::cout << std::get<1>(cp.compute->getShaderCode(ShaderLanguage::HLSL, true).shaderCode) << std::endl;
}