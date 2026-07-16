#include  <horizon.h>
#include "Codegen/BuiltinVariate.h"
#include "Codegen/ComputePipelineObject.h"
#include "Codegen/TypeAlias.h"

#include <example_branch_pruning/example_branch_pruning.h>
#include <Codegen/ControlFlows.h>

void run_example_branch_pruning()
{
    using namespace EmbeddedShader;
    int a = 1;
    Int a2 = 1;
    Texture2D<ktm::fvec4> texture;
    auto shader = [&]()
    {
        $IF(a2 < 0)
        {
            texture[dispatchThreadID()->xy()] = Float4(1,2,3,4);
        }
        $ELSE $IF(a == 1)
        {
            Float v = Float(dispatchThreadID()->x);
            texture[dispatchThreadID()->xy()] = Float4(1,1,1,v);
        }
        $ELSE
        {
            $IF(a == 0)
            {
                texture[dispatchThreadID()->xy()] = Float4(1,1,1,2);
            }
            $ELSE
            {
                texture[dispatchThreadID()->xy()] = Float4();
            }
        }
    };

    CompilerOption option;
    option.compileDXBC = false;
    option.compileDXIL = false;
    option.compileGLSL = false;
    option.compileSpirV = true;
    option.compileHLSL = true;
    option.enableBindless = false;
    auto compute = ComputePipelineObject::compile(shader,ktm::uvec3(8,8,1),option);
    a = 0;
    std::cout << std::get<1>(compute.compute->getShaderCode(ShaderLanguage::HLSL).shaderCode) << "\n";
}
