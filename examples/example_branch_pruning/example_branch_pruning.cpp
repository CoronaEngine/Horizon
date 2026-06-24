#include  <horizon.h>
#include "Codegen/BuiltinVariate.h"
#include "Codegen/ComputePipelineObject.h"
#include "Codegen/TypeAlias.h"

#include <example_branch_pruning/example_branch_pruning.h>
#include <Codegen/ControlFlows.h>

void run_example_branch_pruning()
{
    using namespace EmbeddedShader;
    auto branch0 = R"(
float branch();)";
    auto branch = R"(
float branch() {
    return 1.5;
})";
    auto entrypoint = R"(
import branch;
RWTexture2D<float4> texture;
[shader("compute")]
void main() {
    texture[uint2(0,0)].x = branch();
})";
    SlangModuleCompileArgs args;
    args.sourceLanguage = ShaderLanguage::Slang;
    args.shaderCode = branch;
    args.moduleName = "branch";
    auto branchResult = ShaderLanguageConverter::slangModuleCompiler(args);
    args.shaderCode = branch0;
    args.moduleName = "branch";
    auto branch0Result = ShaderLanguageConverter::slangModuleCompiler(args);
    args.shaderCode = entrypoint;
    args.moduleName = "entrypoint";
    args.deps = {&branch0Result};
    auto entrypointResult = ShaderLanguageConverter::slangModuleCompiler(args);

    SlangCompileArgs2 compileArgs;
    compileArgs.module = &entrypointResult;
    compileArgs.sourceLanguage = ShaderLanguage::Slang;
    compileArgs.deps = {&branchResult};
    compileArgs.targetLanguages = {ShaderLanguage::HLSL};
    std::cout << ShaderLanguageConverter::slangCompilerWithModules(compileArgs).stringTargets[ShaderLanguage::HLSL] << "\n";
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

        /*for (GPU_IF TheIfElseStatementMustBeGuidedByIf;TheIfElseStatementMustBeGuidedByIf.index < 1; ++TheIfElseStatementMustBeGuidedByIf.index)
            while (TheIfElseStatementMustBeGuidedByIf.currentIndex = 0,TheIfElseStatementMustBeGuidedByIf.lastMaxIndex < TheIfElseStatementMustBeGuidedByIf.maxCount)
                if (TheIfElseStatementMustBeGuidedByIf.currentIndex++ == TheIfElseStatementMustBeGuidedByIf.maxCount ? (++TheIfElseStatementMustBeGuidedByIf.maxCount,true) : (++TheIfElseStatementMustBeGuidedByIf.lastMaxIndex,false))
                    for (GPU_IF_BRANCH gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3{a2 < 0 , [&]() { if(a2 < 0) return true; return false; }};gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index < 1; ++gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index)
                        {
                            texture[dispatchThreadID()->xy()] = Float4(1,2,3,4);
                        }
                else for (uint32_t varJ6hF4rT9mK2zV8cX5bN1pQ3{}; varJ6hF4rT9mK2zV8cX5bN1pQ3 < 1 && (TheIfElseStatementMustBeGuidedByIf.currentIndex++ == TheIfElseStatementMustBeGuidedByIf.maxCount ? (++TheIfElseStatementMustBeGuidedByIf.maxCount,true) : (++TheIfElseStatementMustBeGuidedByIf.lastMaxIndex,false)); ++varJ6hF4rT9mK2zV8cX5bN1pQ3)
                    for (GPU_ELSE_BRANCH gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3; gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index < 1; ++gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index)
                        for (GPU_IF TheIfElseStatementMustBeGuidedByIf;TheIfElseStatementMustBeGuidedByIf.index < 1; ++TheIfElseStatementMustBeGuidedByIf.index)
                            while (TheIfElseStatementMustBeGuidedByIf.currentIndex = 0,TheIfElseStatementMustBeGuidedByIf.lastMaxIndex < TheIfElseStatementMustBeGuidedByIf.maxCount) if (TheIfElseStatementMustBeGuidedByIf.currentIndex++ == TheIfElseStatementMustBeGuidedByIf.maxCount ? (++TheIfElseStatementMustBeGuidedByIf.maxCount,true) : (++TheIfElseStatementMustBeGuidedByIf.lastMaxIndex,false))
                                for (GPU_IF_BRANCH gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3{a == 1 , [&]() { if(a == 1) return true; return false; }};gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index < 1; ++gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index)
                                {
                                    texture[dispatchThreadID()->xy()] = Float4(1,1,1,1);
                                }
                else for (uint32_t varJ6hF4rT9mK2zV8cX5bN1pQ3{}; varJ6hF4rT9mK2zV8cX5bN1pQ3 < 1 && (TheIfElseStatementMustBeGuidedByIf.currentIndex++ == TheIfElseStatementMustBeGuidedByIf.maxCount ? (++TheIfElseStatementMustBeGuidedByIf.maxCount,true) : (++TheIfElseStatementMustBeGuidedByIf.lastMaxIndex,false)); ++varJ6hF4rT9mK2zV8cX5bN1pQ3)
                    for (GPU_ELSE_BRANCH gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3; gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index < 1; ++gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index)
                        for (GPU_IF TheIfElseStatementMustBeGuidedByIf;TheIfElseStatementMustBeGuidedByIf.index < 1; ++TheIfElseStatementMustBeGuidedByIf.index)
                            while (TheIfElseStatementMustBeGuidedByIf.currentIndex = 0,TheIfElseStatementMustBeGuidedByIf.lastMaxIndex < TheIfElseStatementMustBeGuidedByIf.maxCount) if (TheIfElseStatementMustBeGuidedByIf.currentIndex++ == TheIfElseStatementMustBeGuidedByIf.maxCount ? (++TheIfElseStatementMustBeGuidedByIf.maxCount,true) : (++TheIfElseStatementMustBeGuidedByIf.lastMaxIndex,false))
                                for (GPU_IF_BRANCH gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3{a2 == 2 , [&]() { if(a2 == 2) return true; return false; }};gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index < 1; ++gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index)
                                {
                                    texture[dispatchThreadID()->xy()] = Float4(1,1,1,3);
                                }
                else for (uint32_t varJ6hF4rT9mK2zV8cX5bN1pQ3{}; varJ6hF4rT9mK2zV8cX5bN1pQ3 < 1 && (TheIfElseStatementMustBeGuidedByIf.currentIndex++ == TheIfElseStatementMustBeGuidedByIf.maxCount ? (++TheIfElseStatementMustBeGuidedByIf.maxCount,true) : (++TheIfElseStatementMustBeGuidedByIf.lastMaxIndex,false)); ++varJ6hF4rT9mK2zV8cX5bN1pQ3)
                    for (GPU_ELSE_BRANCH gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3; gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index < 1; ++gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3.index)
                    {
                        texture[dispatchThreadID()->xy()] = Float4();
                    }*/
    };

    CompilerOption option;
    option.compileDXBC = false;
    option.compileDXIL = false;
    option.compileGLSL = false;
    option.compileSpirV = true;
    option.compileHLSL = true;
    option.enableBindless = false;
    auto compute = ComputePipelineObject::compile(shader,ktm::uvec3(1),option);
    std::cout << std::get<1>(compute.compute->getShaderCode(ShaderLanguage::HLSL).shaderCode) << "\n";
}
