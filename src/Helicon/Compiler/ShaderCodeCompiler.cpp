
#include "ShaderCodeCompiler.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

#include <Codegen/AST/Parser.hpp>

#include "ShaderHardcodeManager.h"
#include "ShaderLanguageConverter.h"
#include <shared_mutex>


namespace EmbeddedShader
{
    std::shared_mutex threadMutex;

    std::string enumToString(ShaderLanguage language) {
        switch (language)
        {
            case ShaderLanguage::GLSL:
                return "GLSL";
            case ShaderLanguage::HLSL:
                return "HLSL";
            case ShaderLanguage::SpirV:
                return "SpirV";
            case ShaderLanguage::Slang:
                return "Slang";
            default:break;
        }
        return "Unknown";
    }

    std::string enumToString(ShaderStage stage)
    {
        switch (stage)
        {
            case ShaderStage::VertexShader:
                return "VertexShader";
            case ShaderStage::FragmentShader:
                return "FragmentShader";
            case ShaderStage::ComputeShader:
                return "ComputeShader";
            default:break;
        }
        return "Unknown";
    }

    ShaderCodeCompiler::ShaderCodeCompiler(const std::string& shaderCode, ShaderStage inputStage,
        ShaderLanguage language, CompilerOption option, const std::source_location& sourceLocation)
    {
        sourceLocationStr = ShaderHardcodeManager::getSourceLocationString(sourceLocation);
        stage = enumToString(inputStage);
        compile(shaderCode,inputStage,language,option);
    }

    ShaderCodeModule ShaderCodeCompiler::getShaderCode(ShaderLanguage language, bool bindless) const
    {
        std::shared_lock<std::shared_mutex> lock(threadMutex);

        std::string bindlessStr = bindless ? "_Bindless" : "";
        ShaderCodeModule result;
        auto languageStr = enumToString(language);

        auto codeKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr);
        auto reflKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + "_Reflection" + bindlessStr);

#if CABBAGE_ENGINE_DEBUG
        // Read from per-instance compiled outputs
        if (auto it = compiledOutputs_.find(codeKey); it != compiledOutputs_.end())
            result.shaderCode = std::get<1>(it->second);
        if (auto it = compiledOutputs_.find(reflKey); it != compiledOutputs_.end())
            result.shaderResources = std::get<0>(it->second);
#else
        result.shaderCode = std::get<1>(ShaderHardcodeManager::getHardcodeShader(stage, codeKey));
        result.shaderResources = std::get<0>(ShaderHardcodeManager::getHardcodeShader(stage, reflKey));
#endif
        return result;
    }

    void ShaderCodeCompiler::compile(const std::string& shaderCode, ShaderStage inputStage, ShaderLanguage language,
        CompilerOption option) const
    {
#if CABBAGE_ENGINE_DEBUG
        std::string bindlessStr = Ast::Parser::getBindless() ? "_Bindless" : "";
        bool isNeedLinkLib = option.spvLinkBinary && !option.spvLinkBinary->empty();
        std::vector<uint32_t> codeSpirV = {};
#ifdef WIN32
        std::vector<uint32_t> codeDXIL = {};
        std::vector<uint32_t> codeDXBC = {};
#endif

        if (Ast::Parser::getBindless())
            option.compileDXBC = false; // DXBC不支持sm6.6 bindless

        if (language != ShaderLanguage::Slang && language != ShaderLanguage::HLSL && !option.compileHLSL)
        {
            //slang可以直接编译到dxil和dxbc，但其他情况需要依赖HLSL，所以如果不需要HLSL就直接跳过
            option.compileDXIL = false;
            option.compileDXBC = false;
        }

        std::string codeGLSL;
        std::string codeHLSL;
        std::string codeSlang;
        std::vector<ShaderCodeModule::ShaderResources> reflections;

        switch (language)
        {
            case ShaderLanguage::Slang:
            {
                codeSlang = shaderCode;

                std::vector<std::string> outputs;
                std::vector<std::vector<uint32_t>> binaryOutputs;
                std::vector<ShaderLanguage> binaryLanguages;
                std::vector<ShaderLanguage> languages;
                if (option.compileSpirV)
                    binaryLanguages.push_back(ShaderLanguage::SpirV);
                if (!isNeedLinkLib)
                {
#ifdef WIN32
                    if (option.compileDXIL)
                        binaryLanguages.push_back(ShaderLanguage::DXIL);
                    if (option.compileDXBC)
                        binaryLanguages.push_back(ShaderLanguage::DXBC);
#endif
                    if (option.compileGLSL)
                        languages.push_back(ShaderLanguage::GLSL);
                    if (option.compileHLSL)
                        languages.push_back(ShaderLanguage::HLSL);
                }


                reflections = ShaderLanguageConverter::slangCompiler(codeSlang, binaryLanguages, languages, binaryOutputs, outputs, true,!isNeedLinkLib);
                size_t index = 0;
                if (option.compileSpirV)
                    codeSpirV = binaryOutputs[index++];
                if (!isNeedLinkLib)
                {
#ifdef WIN32
                    if (option.compileDXIL)
                        codeDXIL = binaryOutputs[index++];
                    if (option.compileDXBC)
                        codeDXBC = binaryOutputs[index++];
#endif

                    index = 0;
                    if (option.compileGLSL)
                        codeGLSL = outputs[index++];
                    if (option.compileHLSL)
                        codeHLSL = outputs[index++];
                }
                break;
            }
            case ShaderLanguage::GLSL:
                codeGLSL = shaderCode;
                if (option.compileSpirV)
                    codeSpirV = ShaderLanguageConverter::glslangSpirvCompiler(codeGLSL, language, inputStage);
                if (option.compileGLSL)
                    codeGLSL = ShaderLanguageConverter::spirvCrossConverter(codeSpirV, ShaderLanguage::GLSL);
                if (option.compileHLSL)
                    codeHLSL = ShaderLanguageConverter::spirvCrossConverter(codeSpirV, ShaderLanguage::HLSL);
#ifdef WIN32
                // codeDXIL = ShaderLanguageConverter::dxilCompiler(codeHLSL, inputStage);
                // if (!bindless)
                //     codeDXBC = ShaderLanguageConverter::dxbcCompiler(codeHLSL, inputStage);
#endif
                break;
            case ShaderLanguage::HLSL:
                codeHLSL = shaderCode;
                if (option.compileSpirV)
                    codeSpirV = ShaderLanguageConverter::glslangSpirvCompiler(codeHLSL, language, inputStage);
                if (option.compileGLSL)
                    codeGLSL = ShaderLanguageConverter::spirvCrossConverter(codeSpirV, ShaderLanguage::GLSL);
                if (option.compileHLSL)
                    codeHLSL = ShaderLanguageConverter::spirvCrossConverter(codeSpirV, ShaderLanguage::HLSL);
#ifdef WIN32
                if (option.compileDXIL)
                    codeDXIL = ShaderLanguageConverter::dxilCompiler(codeHLSL, inputStage);
                if (option.compileDXBC)
                    codeDXBC = ShaderLanguageConverter::dxbcCompiler(codeHLSL, inputStage);
#endif
                break;
                //case ShaderLanguage::SpirV:
                //    codeSpirV = shaderCode;
                //    codeGLSL = ShaderLanguageConverter::spirvCrossConverter(codeSpirV, ShaderLanguage::GLSL);
                //    codeHLSL = ShaderLanguageConverter::spirvCrossConverter(codeSpirV, ShaderLanguage::HLSL);
                //    break;
            default:
                break;
        }

        if (isNeedLinkLib && !codeSpirV.empty())
        {
            std::vector<std::vector<uint32_t>> src = *option.spvLinkBinary;
            src.push_back(codeSpirV);
            codeSpirV = ShaderLanguageConverter::spirvLinker(src);
        }

        if (isNeedLinkLib && !codeSpirV.empty())
        {
#ifdef WIN32
            if (option.compileDXIL)
                codeDXIL = ShaderLanguageConverter::dxilCompiler(codeHLSL, inputStage);
            if (option.compileDXBC)
                codeDXBC = ShaderLanguageConverter::dxbcCompiler(codeHLSL, inputStage);
#endif
            if (option.compileGLSL)
                codeGLSL = ShaderLanguageConverter::spirvCrossConverter(codeSpirV, ShaderLanguage::GLSL);
            if (option.compileHLSL)
                codeHLSL = ShaderLanguageConverter::spirvCrossConverter(codeSpirV, ShaderLanguage::HLSL);
        }
        
        //auto functionSignatures = ShaderLanguageConverter::spirvCrossGetFunctionSignatures(codeSpirV);


        // support mutil-thread
        {
            std::unique_lock<std::shared_mutex> lock(threadMutex);

        // Pre-compute spirv-cross reflection once for all formats that need it
        ShaderCodeModule::ShaderResources spirvCrossReflection;
        if (!codeSpirV.empty())
            spirvCrossReflection = ShaderLanguageConverter::spirvCrossReflectedBindInfo(codeSpirV, ShaderLanguage::HLSL);

        // Helper lambdas: store locally AND write to file for release pre-generation
        auto storeCode = [&](const auto& code, const std::string& itemName) {
            compiledOutputs_[itemName] = code;
            ShaderHardcodeManager::addTarget(code, stage, itemName);
        };
        auto storeReflection = [&](const ShaderCodeModule::ShaderResources& res, const std::string& itemName) {
            compiledOutputs_[itemName] = res;
            ShaderHardcodeManager::addTarget(res, stage, itemName);
        };

        size_t index = 0;
        if (!codeSpirV.empty())
        {
            storeCode(codeSpirV, ShaderHardcodeManager::getItemName(sourceLocationStr, "SpirV" + bindlessStr));
            // SpirV 目标始终使用 spirv-cross 反射：Slang 反射不处理 push_constant_buffers，
            // 导致 pushConstantSize/pushConstantName/pushConstantMembers 全部缺失
            storeReflection(spirvCrossReflection, ShaderHardcodeManager::getItemName(sourceLocationStr, "SpirV_Reflection" + bindlessStr));
            if (!reflections.empty()) index++;
        }
        if (!codeGLSL.empty())
        {
            storeCode(codeGLSL, ShaderHardcodeManager::getItemName(sourceLocationStr, "GLSL" + bindlessStr));
            if (!reflections.empty()) storeReflection(reflections[index++], ShaderHardcodeManager::getItemName(sourceLocationStr, "GLSL_Reflection" + bindlessStr));
            else storeReflection(spirvCrossReflection, ShaderHardcodeManager::getItemName(sourceLocationStr, "GLSL_Reflection" + bindlessStr));
        }
        if (!codeHLSL.empty())
        {
            storeCode(codeHLSL, ShaderHardcodeManager::getItemName(sourceLocationStr, "HLSL" + bindlessStr));
            if (!reflections.empty()) storeReflection(reflections[index++], ShaderHardcodeManager::getItemName(sourceLocationStr, "HLSL_Reflection" + bindlessStr));
            else storeReflection(spirvCrossReflection, ShaderHardcodeManager::getItemName(sourceLocationStr, "HLSL_Reflection" + bindlessStr));
        }
        if (!codeSlang.empty())
        {
            storeCode(codeSlang, ShaderHardcodeManager::getItemName(sourceLocationStr, "Slang" + bindlessStr));
            if (!reflections.empty()) storeReflection(reflections[0], ShaderHardcodeManager::getItemName(sourceLocationStr, "Slang_Reflection" + bindlessStr));
            else storeReflection(spirvCrossReflection, ShaderHardcodeManager::getItemName(sourceLocationStr, "Slang_Reflection" + bindlessStr));
        }
#ifdef WIN32
        if (!codeDXIL.empty())
        {
            storeCode(codeDXIL, ShaderHardcodeManager::getItemName(sourceLocationStr, "DXIL" + bindlessStr));
            if (!reflections.empty()) storeReflection(reflections[index++], ShaderHardcodeManager::getItemName(sourceLocationStr, "DXIL_Reflection" + bindlessStr));
            else storeReflection(spirvCrossReflection, ShaderHardcodeManager::getItemName(sourceLocationStr, "DXIL_Reflection" + bindlessStr));
        }
        if (!codeDXBC.empty())
        {
            storeCode(codeDXBC, ShaderHardcodeManager::getItemName(sourceLocationStr, "DXBC"));
            if (!reflections.empty()) storeReflection(reflections[index++], ShaderHardcodeManager::getItemName(sourceLocationStr, "DXBC_Reflection" + bindlessStr));
            else storeReflection(spirvCrossReflection, ShaderHardcodeManager::getItemName(sourceLocationStr, "DXBC_Reflection" + bindlessStr));
        }
#endif
        }
#endif
    }
}